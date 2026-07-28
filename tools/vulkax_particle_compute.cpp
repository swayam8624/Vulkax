#include "vulkax/sim/particle_system.hpp"

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

struct alignas(16) ParticleStateGpu {
  float positionX;
  float positionY;
  float mass;
  float positionPadding;
  float velocityX;
  float velocityY;
  float velocityPadding0;
  float velocityPadding1;
};
static_assert(sizeof(ParticleStateGpu) == 32);

struct alignas(16) ParticleParameters {
  uint32_t count;
  uint32_t phase;
  float countPhasePadding[2];
  float timestep;
  float gravitationalConstant;
  float softening;
  float physicalPadding;
};
static_assert(sizeof(ParticleParameters) == 32);

struct Buffer { VkBuffer buffer = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; };

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}

uint32_t coherentMemoryType(VkPhysicalDevice physical, uint32_t mask) {
  VkPhysicalDeviceMemoryProperties properties{};
  vkGetPhysicalDeviceMemoryProperties(physical, &properties);
  const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
    if ((mask & (1u << index)) && (properties.memoryTypes[index].propertyFlags & required) == required) return index;
  }
  throw std::runtime_error("host-visible coherent memory unavailable");
}

Buffer makeBuffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage) {
  VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  create.size = size;
  create.usage = usage;
  create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  Buffer result{};
  check(vkCreateBuffer(device, &create, nullptr, &result.buffer), "vkCreateBuffer");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, result.buffer, &requirements);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = coherentMemoryType(physical, requirements.memoryTypeBits);
  check(vkAllocateMemory(device, &allocation, nullptr, &result.memory), "vkAllocateMemory");
  check(vkBindBufferMemory(device, result.buffer, result.memory, 0), "vkBindBufferMemory");
  return result;
}

void writeBuffer(VkDevice device, VkDeviceMemory memory, const void* source, size_t bytes) {
  void* mapped = nullptr;
  check(vkMapMemory(device, memory, 0, bytes, 0, &mapped), "vkMapMemory");
  std::memcpy(mapped, source, bytes);
  vkUnmapMemory(device, memory);
}

std::vector<char> binary(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) throw std::runtime_error("shader missing: " + path.string());
  std::vector<char> result(static_cast<size_t>(stream.tellg()));
  stream.seekg(0);
  stream.read(result.data(), static_cast<std::streamsize>(result.size()));
  return result;
}

bool portabilityEnumeration() {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
  return std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
    return std::string(extension.extensionName) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
  });
}

void destroy(VkDevice device, const Buffer& buffer) {
  vkDestroyBuffer(device, buffer.buffer, nullptr);
  vkFreeMemory(device, buffer.memory, nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    uint32_t steps = 128;
    std::filesystem::path output = "docs/results/physics_studio_current/gpu_particle_gravity";
    for (int index = 1; index < argc; ++index) {
      const std::string argument(argv[index]);
      if (argument == "--steps" && index + 1 < argc) steps = std::stoul(argv[++index]);
      else if (argument == "--output" && index + 1 < argc) output = argv[++index];
      else throw std::invalid_argument("usage: vulkax-particle-compute [--steps N] [--output PATH]");
    }

    const vulkax::sim::ParticleGravityConfig config{6, 100.0, 1.0, 1.0, 0.05, 1.0 / 960.0, 1337};
    vulkax::sim::ParticleGravitySystem reference(config);
    reference.step(steps);
    std::vector<ParticleStateGpu> initial;
    initial.reserve(reference.particles().size());
    vulkax::sim::ParticleGravitySystem initialSystem(config);
    for (const auto& particle : initialSystem.particles()) {
      initial.push_back({static_cast<float>(particle.position.x), static_cast<float>(particle.position.y),
          static_cast<float>(particle.mass), 0.0f, static_cast<float>(particle.velocity.x),
          static_cast<float>(particle.velocity.y), 0.0f, 0.0f});
    }

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Vulkax GPU Particle Gravity";
    application.apiVersion = VK_API_VERSION_1_1;
    std::vector<const char*> instanceExtensions;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application;
    if (portabilityEnumeration()) {
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
    uint32_t family = 0;
    for (const auto candidate : physicalDevices) {
      uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
      for (uint32_t index = 0; index < familyCount; ++index) {
        if (families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) { physical = candidate; family = index; break; }
      }
      if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) throw std::runtime_error("compute queue unavailable");
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physical, &deviceProperties);
    const bool hasTimestamps = deviceProperties.limits.timestampComputeAndGraphics == VK_TRUE;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device = VK_NULL_HANDLE;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, family, 0, &queue);

    const VkDeviceSize stateBytes = initial.size() * sizeof(ParticleStateGpu);
    Buffer current = makeBuffer(physical, device, stateBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer intermediate = makeBuffer(physical, device, stateBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer next = makeBuffer(physical, device, stateBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer firstParameters = makeBuffer(physical, device, sizeof(ParticleParameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Buffer secondParameters = makeBuffer(physical, device, sizeof(ParticleParameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    writeBuffer(device, current.memory, initial.data(), stateBytes);
    const ParticleParameters phase0{static_cast<uint32_t>(initial.size()), 0, {0.0f, 0.0f},
        static_cast<float>(config.timestepSeconds), static_cast<float>(config.gravitationalConstant),
        static_cast<float>(config.softening), 0.0f};
    const ParticleParameters phase1{static_cast<uint32_t>(initial.size()), 1, {0.0f, 0.0f},
        static_cast<float>(config.timestepSeconds), static_cast<float>(config.gravitationalConstant),
        static_cast<float>(config.softening), 0.0f};
    writeBuffer(device, firstParameters.memory, &phase0, sizeof(phase0));
    writeBuffer(device, secondParameters.memory, &phase1, sizeof(phase1));

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorLayoutInfo.bindingCount = 3;
    descriptorLayoutInfo.pBindings = bindings;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    check(vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &descriptorLayout), "vkCreateDescriptorSetLayout");
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout");
    const auto shaderBytes = binary(std::filesystem::path(ENGINE_DIR) / "shaders/vulkax_particle_gravity_step.comp.spv");
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

    VkDescriptorPoolSize poolSizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool");
    VkDescriptorSetLayout layouts[2]{descriptorLayout, descriptorLayout};
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = pool;
    setInfo.descriptorSetCount = 2;
    setInfo.pSetLayouts = layouts;
    VkDescriptorSet sets[2]{};
    check(vkAllocateDescriptorSets(device, &setInfo, sets), "vkAllocateDescriptorSets");

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = family;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device, &commandInfo, &command), "vkAllocateCommandBuffers");
    VkQueryPool timestampPool = VK_NULL_HANDLE;
    if (hasTimestamps) {
      VkQueryPoolCreateInfo timestampInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      timestampInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
      timestampInfo.queryCount = steps * 2;
      check(vkCreateQueryPool(device, &timestampInfo, nullptr, &timestampPool), "vkCreateQueryPool");
    }

    for (uint32_t step = 0; step < steps; ++step) {
      VkDescriptorBufferInfo phase0Infos[3]{{current.buffer, 0, stateBytes}, {intermediate.buffer, 0, stateBytes}, {firstParameters.buffer, 0, sizeof(phase0)}};
      VkDescriptorBufferInfo phase1Infos[3]{{intermediate.buffer, 0, stateBytes}, {next.buffer, 0, stateBytes}, {secondParameters.buffer, 0, sizeof(phase1)}};
      VkWriteDescriptorSet writes[6]{};
      for (uint32_t binding = 0; binding < 3; ++binding) {
        writes[binding] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sets[0], binding, 0, 1,
            binding == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &phase0Infos[binding], nullptr};
        writes[binding + 3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sets[1], binding, 0, 1,
            binding == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &phase1Infos[binding], nullptr};
      }
      vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
      VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
      vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      if (hasTimestamps) vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampPool, step * 2);
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &sets[0], 0, nullptr);
      vkCmdDispatch(command, (static_cast<uint32_t>(initial.size()) + 63) / 64, 1, 1);
      VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
      barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.buffer = intermediate.buffer;
      barrier.offset = 0;
      barrier.size = stateBytes;
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          0, 0, nullptr, 1, &barrier, 0, nullptr);
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &sets[1], 0, nullptr);
      vkCmdDispatch(command, (static_cast<uint32_t>(initial.size()) + 63) / 64, 1, 1);
      if (hasTimestamps) vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampPool, step * 2 + 1);
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
      std::swap(current, next);
    }

    std::vector<ParticleStateGpu> result(initial.size());
    void* mapped = nullptr;
    check(vkMapMemory(device, current.memory, 0, stateBytes, 0, &mapped), "vkMapMemory result");
    std::memcpy(result.data(), mapped, stateBytes);
    vkUnmapMemory(device, current.memory);
    double mse = 0.0;
    double maximum = 0.0;
    for (size_t index = 0; index < result.size(); ++index) {
      const auto& expected = reference.particles()[index];
      const double errors[] = {result[index].positionX - expected.position.x, result[index].positionY - expected.position.y,
          result[index].velocityX - expected.velocity.x, result[index].velocityY - expected.velocity.y};
      for (const double error : errors) { mse += error * error; maximum = std::max(maximum, std::abs(error)); }
    }
    mse /= static_cast<double>(result.size() * 4);
    double gpuDispatchMilliseconds = -1.0;
    if (hasTimestamps) {
      std::vector<uint64_t> timestamps(steps * 2);
      check(vkGetQueryPoolResults(device, timestampPool, 0, steps * 2, timestamps.size() * sizeof(uint64_t), timestamps.data(), sizeof(uint64_t),
          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "vkGetQueryPoolResults");
      uint64_t elapsed = 0;
      for (uint32_t step = 0; step < steps; ++step) elapsed += timestamps[step * 2 + 1] - timestamps[step * 2];
      gpuDispatchMilliseconds = static_cast<double>(elapsed) * deviceProperties.limits.timestampPeriod / 1'000'000.0;
    }
    std::filesystem::create_directories(output);
    std::ofstream report(output / "gpu_particle_gravity_agreement.json");
    report << "{\n  \"measurement_class\": \"vulkan_two_pass_velocity_verlet_readback\",\n"
           << "  \"device\": \"" << deviceProperties.deviceName << "\",\n  \"particles\": " << result.size()
           << ",\n  \"steps\": " << steps << ",\n  \"mse\": " << mse << ",\n  \"max_error\": " << maximum
           << ",\n  \"gpu_dispatch_ms\": " << gpuDispatchMilliseconds << "\n}\n";
    std::cout << "Vulkan particle gravity " << deviceProperties.deviceName << " steps=" << steps << " MSE=" << mse
              << " max error=" << maximum;
    if (gpuDispatchMilliseconds >= 0.0) std::cout << " gpu_dispatch_ms=" << gpuDispatchMilliseconds;
    std::cout << '\n';

    vkDestroyQueryPool(device, timestampPool, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, shader, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    destroy(device, current); destroy(device, intermediate); destroy(device, next); destroy(device, firstParameters); destroy(device, secondParameters);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return maximum < 5e-4 ? 0 : 3;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-particle-compute: " << error.what() << '\n';
    return 1;
  }
}
