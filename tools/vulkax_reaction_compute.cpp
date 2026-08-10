#include "vulkax/sim/simulation_graph.hpp"
#include "runtime_paths.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct alignas(16) ReactionParameters {
  uint32_t width;
  uint32_t height;
  float timestep;
  float padding;
  float diffusionA;
  float diffusionB;
  float feed;
  float kill;
};
static_assert(sizeof(ReactionParameters) == 32);

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
};

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}

uint32_t hostVisibleMemoryType(VkPhysicalDevice physical, uint32_t mask) {
  VkPhysicalDeviceMemoryProperties properties{};
  vkGetPhysicalDeviceMemoryProperties(physical, &properties);
  for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
    const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if ((mask & (1u << index)) && (properties.memoryTypes[index].propertyFlags & required) == required) return index;
  }
  throw std::runtime_error("host-visible coherent memory unavailable");
}

Buffer makeBuffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage) {
  VkBufferCreateInfo createInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  createInfo.size = size;
  createInfo.usage = usage;
  createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  Buffer result{};
  check(vkCreateBuffer(device, &createInfo, nullptr, &result.buffer), "vkCreateBuffer");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, result.buffer, &requirements);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = hostVisibleMemoryType(physical, requirements.memoryTypeBits);
  check(vkAllocateMemory(device, &allocation, nullptr, &result.memory), "vkAllocateMemory");
  check(vkBindBufferMemory(device, result.buffer, result.memory, 0), "vkBindBufferMemory");
  return result;
}

void writeBuffer(VkDevice device, VkDeviceMemory memory, const void* values, size_t bytes) {
  void* mapped = nullptr;
  check(vkMapMemory(device, memory, 0, bytes, 0, &mapped), "vkMapMemory");
  std::memcpy(mapped, values, bytes);
  vkUnmapMemory(device, memory);
}

std::vector<char> readBinary(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("shader missing: " + path.string());
  std::vector<char> result(static_cast<size_t>(file.tellg()));
  file.seekg(0);
  file.read(result.data(), static_cast<std::streamsize>(result.size()));
  return result;
}

bool supportsPortabilityEnumeration() {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
  return std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
    return std::string(extension.extensionName) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
  });
}

void destroy(VkDevice device, const Buffer& value) {
  vkDestroyBuffer(device, value.buffer, nullptr);
  vkFreeMemory(device, value.memory, nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    uint32_t steps = 64;
    std::filesystem::path output = "docs/results/physics_studio_current/gpu_reaction_diffusion";
    for (int index = 1; index < argc; ++index) {
      const std::string argument(argv[index]);
      if (argument == "--steps" && index + 1 < argc) steps = std::stoul(argv[++index]);
      else if (argument == "--output" && index + 1 < argc) output = argv[++index];
      else throw std::invalid_argument("usage: vulkax-reaction-compute [--steps N] [--output PATH]");
    }

    constexpr uint32_t width = 64;
    constexpr uint32_t height = 64;
    constexpr float timestep = 1.0f / 60.0f;
    vulkax::sim::SimulationGraph reference{{vulkax::sim::SimulationKind::ReactionDiffusion, width, height, timestep, 1.0f, 1337}};
    const auto initialA = reference.primaryField();
    const auto initialB = reference.secondaryField();
    reference.step(steps);

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Vulkax GPU Reaction Diffusion";
    application.apiVersion = VK_API_VERSION_1_1;
    std::vector<const char*> instanceExtensions;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application;
    if (supportsPortabilityEnumeration()) {
      instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      instanceInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
    VkInstance instance = VK_NULL_HANDLE;
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    uint32_t physicalCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr), "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, physicalDevices.data()), "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    for (const auto candidate : physicalDevices) {
      uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
      for (uint32_t index = 0; index < familyCount; ++index) {
        if (families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) { physical = candidate; queueFamily = index; break; }
      }
      if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) throw std::runtime_error("compute queue unavailable");
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physical, &deviceProperties);
    const bool hasComputeTimestamps = deviceProperties.limits.timestampComputeAndGraphics == VK_TRUE;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device = VK_NULL_HANDLE;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    const VkDeviceSize fieldBytes = static_cast<VkDeviceSize>(width) * height * sizeof(float);
    Buffer currentA = makeBuffer(physical, device, fieldBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer currentB = makeBuffer(physical, device, fieldBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer nextA = makeBuffer(physical, device, fieldBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer nextB = makeBuffer(physical, device, fieldBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer parameters = makeBuffer(physical, device, sizeof(ReactionParameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    writeBuffer(device, currentA.memory, initialA.data(), fieldBytes);
    writeBuffer(device, currentB.memory, initialB.data(), fieldBytes);
    const ReactionParameters values{width, height, timestep, 0.0f, 1.0f, 0.5f, 0.0367f, 0.0649f};
    writeBuffer(device, parameters.memory, &values, sizeof(values));

    VkDescriptorSetLayoutBinding bindings[5]{};
    for (uint32_t index = 0; index < 4; ++index) bindings[index] = {index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout), "vkCreateDescriptorSetLayout");
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout");

    const auto shaderBytes = readBinary(lve::resolveRuntimeResource("shaders/vulkax_reaction_diffusion_step.comp.spv"));
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = shaderBytes.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(shaderBytes.data());
    VkShaderModule shader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule");
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", nullptr};
    pipelineInfo.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "vkCreateComputePipelines");

    VkDescriptorPoolSize poolSizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = pool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &descriptorLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &setInfo, &set), "vkAllocateDescriptorSets");

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = queueFamily;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device, &commandInfo, &command), "vkAllocateCommandBuffers");
    VkQueryPool timestampPool = VK_NULL_HANDLE;
    if (hasComputeTimestamps) {
      VkQueryPoolCreateInfo timestampInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      timestampInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
      timestampInfo.queryCount = steps * 2;
      check(vkCreateQueryPool(device, &timestampInfo, nullptr, &timestampPool), "vkCreateQueryPool");
    }

    for (uint32_t step = 0; step < steps; ++step) {
      VkDescriptorBufferInfo bufferInfos[5]{{currentA.buffer, 0, fieldBytes}, {currentB.buffer, 0, fieldBytes}, {nextA.buffer, 0, fieldBytes}, {nextB.buffer, 0, fieldBytes}, {parameters.buffer, 0, sizeof(values)}};
      VkWriteDescriptorSet writes[5]{};
      for (uint32_t index = 0; index < 5; ++index) {
        writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, index, 0, 1,
            index == 4 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufferInfos[index], nullptr};
      }
      vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
      VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
      vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
      if (hasComputeTimestamps) {
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampPool, step * 2);
      }
      vkCmdDispatch(command, (width + 15) / 16, (height + 15) / 16, 1);
      if (hasComputeTimestamps) {
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampPool, step * 2 + 1);
      }
      check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
      VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      VkFence fence = VK_NULL_HANDLE;
      check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &command;
      check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
      check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
      vkDestroyFence(device, fence, nullptr);
      check(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");
      std::swap(currentA, nextA);
      std::swap(currentB, nextB);
    }

    std::vector<float> resultA(width * height), resultB(width * height);
    void* mapped = nullptr;
    check(vkMapMemory(device, currentA.memory, 0, fieldBytes, 0, &mapped), "vkMapMemory currentA");
    std::memcpy(resultA.data(), mapped, fieldBytes);
    vkUnmapMemory(device, currentA.memory);
    check(vkMapMemory(device, currentB.memory, 0, fieldBytes, 0, &mapped), "vkMapMemory currentB");
    std::memcpy(resultB.data(), mapped, fieldBytes);
    vkUnmapMemory(device, currentB.memory);
    double mseA = 0.0, mseB = 0.0, maximum = 0.0;
    for (size_t index = 0; index < resultA.size(); ++index) {
      const double errorA = std::abs(resultA[index] - reference.primaryField()[index]);
      const double errorB = std::abs(resultB[index] - reference.secondaryField()[index]);
      mseA += errorA * errorA;
      mseB += errorB * errorB;
      maximum = std::max({maximum, errorA, errorB});
    }
    mseA /= resultA.size();
    mseB /= resultB.size();
    double gpuDispatchMilliseconds = -1.0;
    if (hasComputeTimestamps) {
      std::vector<uint64_t> timestamps(steps * 2);
      check(vkGetQueryPoolResults(device, timestampPool, 0, steps * 2,
              timestamps.size() * sizeof(uint64_t), timestamps.data(), sizeof(uint64_t),
              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "vkGetQueryPoolResults");
      uint64_t elapsedTicks = 0;
      for (uint32_t step = 0; step < steps; ++step) elapsedTicks += timestamps[step * 2 + 1] - timestamps[step * 2];
      gpuDispatchMilliseconds = static_cast<double>(elapsedTicks) * deviceProperties.limits.timestampPeriod / 1'000'000.0;
    }
    std::filesystem::create_directories(output);
    std::ofstream report(output / "gpu_reaction_diffusion_agreement.json");
    report << "{\n  \"measurement_class\": \"vulkan_ping_pong_compute_readback\",\n"
           << "  \"device\": \"" << deviceProperties.deviceName << "\",\n"
           << "  \"steps\": " << steps << ",\n  \"mse_a\": " << mseA
           << ",\n  \"mse_b\": " << mseB << ",\n  \"max_error\": " << maximum
           << ",\n  \"gpu_dispatch_ms\": " << gpuDispatchMilliseconds << "\n}\n";
    std::cout << "Vulkan reaction-diffusion " << deviceProperties.deviceName << " steps=" << steps
              << " MSE(A)=" << mseA << " MSE(B)=" << mseB << " max error=" << maximum;
    if (gpuDispatchMilliseconds >= 0.0) std::cout << " gpu_dispatch_ms=" << gpuDispatchMilliseconds;
    std::cout << '\n';

    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, shader, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    vkDestroyQueryPool(device, timestampPool, nullptr);
    destroy(device, currentA); destroy(device, currentB); destroy(device, nextA); destroy(device, nextB); destroy(device, parameters);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return maximum < 1e-5 ? 0 : 3;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-reaction-compute: " << error.what() << '\n';
    return 1;
  }
}
