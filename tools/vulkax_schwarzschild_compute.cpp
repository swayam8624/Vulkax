#include <vulkan/vulkan.h>
#include "runtime_paths.hpp"

#include "vulkax/relativity/schwarzschild_lensing.hpp"

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

struct alignas(16) Parameters {
  uint32_t rayCount = 0;
  uint32_t maximumSteps = 0;
  float mass = 1.0f;
  float observerRadius = 50.0f;
  float affineStep = 0.01f;
  float raySpread = 0.35f;
  float padding[2]{};
};
static_assert(sizeof(Parameters) == 32);

struct alignas(16) GpuResult {
  float minimumRadius = 0.0f;
  float status = 0.0f;
  float relativeEnergyDrift = 0.0f;
  float azimuth = 0.0f;
};
static_assert(sizeof(GpuResult) == 16);

vulkax::relativity::SchwarzschildVec3 directionFor(uint32_t index, uint32_t rayCount, float spread) {
  const uint32_t columns = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(rayCount))));
  const uint32_t row = index / columns;
  const uint32_t column = index % columns;
  const uint32_t rows = (rayCount + columns - 1) / columns;
  const double x = ((static_cast<double>(column) + 0.5) / columns * 2.0 - 1.0) * spread;
  const double y = ((static_cast<double>(row) + 0.5) / rows * 2.0 - 1.0) * spread;
  return {-1.0, x, y};
}

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) throw std::runtime_error(std::string{operation} + " failed: " + std::to_string(result));
}

uint32_t memoryType(VkPhysicalDevice device, uint32_t mask, VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(device, &memory);
  for (uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
    if ((mask & (1u << index)) && (memory.memoryTypes[index].propertyFlags & flags) == flags) return index;
  }
  throw std::runtime_error("no compatible Vulkan memory type");
}

struct Buffer { VkBuffer handle = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; };

Buffer makeBuffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage) {
  VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  create.size = size;
  create.usage = usage;
  create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  Buffer buffer{};
  check(vkCreateBuffer(device, &create, nullptr, &buffer.handle), "vkCreateBuffer");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = memoryType(
      physical, requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  check(vkAllocateMemory(device, &allocation, nullptr, &buffer.memory), "vkAllocateMemory");
  check(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0), "vkBindBufferMemory");
  return buffer;
}

std::vector<char> readFile(const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  if (!file) throw std::runtime_error("could not read shader: " + path.string());
  const auto size = file.tellg();
  std::vector<char> data(static_cast<size_t>(size));
  file.seekg(0);
  file.read(data.data(), size);
  return data;
}

bool hasPortabilityEnumeration() {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
  return std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
    return std::string{extension.extensionName} == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
  });
}

}  // namespace

int main(int argc, char** argv) {
  try {
    uint32_t rays = 16, steps = 30000;
    std::filesystem::path output = "docs/results/physics_studio_current/gpu_schwarzschild";
    for (int index = 1; index < argc; ++index) {
      const std::string option{argv[index]};
      if (option == "--rays" && index + 1 < argc) rays = std::stoul(argv[++index]);
      else if (option == "--steps" && index + 1 < argc) steps = std::stoul(argv[++index]);
      else if (option == "--output" && index + 1 < argc) output = argv[++index];
      else throw std::invalid_argument("usage: vulkax-schwarzschild-compute [--rays N --steps N --output PATH]");
    }
    if (rays == 0 || steps == 0) throw std::invalid_argument("ray count and steps must be positive");

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Vulkax Schwarzschild Compute";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    std::vector<const char*> extensions;
    if (hasPortabilityEnumeration()) {
      extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    VkInstance instance = VK_NULL_HANDLE;
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    uint32_t deviceCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");
    if (deviceCount == 0) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    check(vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data()), "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t family = 0;
    for (const auto candidate : physicalDevices) {
      uint32_t count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
      std::vector<VkQueueFamilyProperties> families(count);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
      for (uint32_t queue = 0; queue < count; ++queue) {
        if (families[queue].queueFlags & VK_QUEUE_COMPUTE_BIT) { physical = candidate; family = queue; break; }
      }
      if (physical) break;
    }
    if (!physical) throw std::runtime_error("no Vulkan compute queue");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device = VK_NULL_HANDLE;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    VkQueue queue{}; vkGetDeviceQueue(device, family, 0, &queue);

    const Parameters parameters{rays, steps, 1.0f, 50.0f, 0.01f, 0.35f, {0.0f, 0.0f}};
    const VkDeviceSize valuesBytes = static_cast<VkDeviceSize>(rays) * sizeof(GpuResult);
    Buffer field = makeBuffer(physical, device, valuesBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer uniform = makeBuffer(physical, device, sizeof(Parameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    void* mapped = nullptr;
    check(vkMapMemory(device, uniform.memory, 0, sizeof(Parameters), 0, &mapped), "vkMapMemory uniform");
    std::memcpy(mapped, &parameters, sizeof(parameters));
    vkUnmapMemory(device, uniform.memory);

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2; layoutInfo.pBindings = bindings;
    VkDescriptorSetLayout descriptorLayout{};
    check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout), "vkCreateDescriptorSetLayout");
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1; pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    VkPipelineLayout pipelineLayout{};
    check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout");
    const auto code = readFile(lve::resolveRuntimeResource("shaders/vulkax_schwarzschild_geodesic.comp.spv"));
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = code.size(); shaderInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shader{}; check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule");
    VkComputePipelineCreateInfo computeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    computeInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", nullptr};
    computeInfo.layout = pipelineLayout;
    VkPipeline pipeline{}; check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &pipeline), "vkCreateComputePipelines");

    VkDescriptorPoolSize poolSizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1; poolInfo.poolSizeCount = 2; poolInfo.pPoolSizes = poolSizes;
    VkDescriptorPool pool{}; check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = pool; setInfo.descriptorSetCount = 1; setInfo.pSetLayouts = &descriptorLayout;
    VkDescriptorSet set{}; check(vkAllocateDescriptorSets(device, &setInfo, &set), "vkAllocateDescriptorSets");
    VkDescriptorBufferInfo fieldInfo{field.handle, 0, valuesBytes};
    VkDescriptorBufferInfo uniformInfo{uniform.handle, 0, sizeof(Parameters)};
    VkWriteDescriptorSet writes[2]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &fieldInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uniformInfo, nullptr};
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = family;
    VkCommandPool commandPool{}; check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool; commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandInfo.commandBufferCount = 1;
    VkCommandBuffer command{}; check(vkAllocateCommandBuffers(device, &commandInfo, &command), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
    vkCmdDispatch(command, (rays + 63) / 64, 1, 1);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence{}; check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
    check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    std::vector<GpuResult> values(rays);
    check(vkMapMemory(device, field.memory, 0, valuesBytes, 0, &mapped), "vkMapMemory field");
    std::memcpy(values.data(), mapped, static_cast<size_t>(valuesBytes)); vkUnmapMemory(device, field.memory);
    double maximumRadiusError = 0.0;
    double maximumDriftError = 0.0;
    uint32_t statusMismatches = 0;
    for (uint32_t index = 0; index < rays; ++index) {
      vulkax::relativity::SchwarzschildGeodesicConfig referenceConfig{};
      referenceConfig.mass = parameters.mass;
      referenceConfig.affineStep = parameters.affineStep;
      referenceConfig.minimumAffineStep = parameters.affineStep;
      referenceConfig.maximumAffineStep = parameters.affineStep;
      referenceConfig.relativeTolerance = 1.0;
      referenceConfig.maximumAffineDistance = parameters.observerRadius * 4.0;
      referenceConfig.maximumSteps = parameters.maximumSteps;
      const auto reference = vulkax::relativity::integrateSchwarzschildGeodesic(
          referenceConfig, {parameters.observerRadius, 0.0, 0.0}, directionFor(index, rays, parameters.raySpread));
      const uint32_t expectedStatus = reference.captured ? 1u : (reference.escaped ? 2u : 0u);
      const uint32_t gpuStatus = static_cast<uint32_t>(std::lround(values[index].status));
      statusMismatches += gpuStatus == expectedStatus ? 0u : 1u;
      maximumRadiusError = std::max(maximumRadiusError,
          std::abs(static_cast<double>(values[index].minimumRadius) - reference.minimumRadius));
      maximumDriftError = std::max(maximumDriftError,
          std::abs(static_cast<double>(values[index].relativeEnergyDrift) - reference.maximumRelativeEnergyDrift));
    }
    std::filesystem::create_directories(output);
    std::ofstream report{output / "gpu_schwarzschild_agreement.json"};
    report << "{\n  \"measurement_class\": \"vulkan_schwarzschild_geodesic_compute_readback\""
           << ",\n  \"device\": \"" << properties.deviceName
           << "\",\n  \"rays\": " << rays << ",\n  \"steps\": " << steps
           << ",\n  \"status_mismatches\": " << statusMismatches
           << ",\n  \"maximum_radius_error\": " << maximumRadiusError
           << ",\n  \"maximum_energy_drift_error\": " << maximumDriftError << "\n}\n";
    std::cout << "Vulkan Schwarzschild compute device: " << properties.deviceName
              << "\nstatus mismatches=" << statusMismatches
              << " min-radius error=" << maximumRadiusError
              << " drift error=" << maximumDriftError << '\n';
    vkDestroyFence(device, fence, nullptr); vkDestroyCommandPool(device, commandPool, nullptr); vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr); vkDestroyShaderModule(device, shader, nullptr); vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr); vkDestroyBuffer(device, field.handle, nullptr); vkFreeMemory(device, field.memory, nullptr);
    vkDestroyBuffer(device, uniform.handle, nullptr); vkFreeMemory(device, uniform.memory, nullptr); vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    return statusMismatches == 0 && maximumRadiusError < 5e-3 && maximumDriftError < 5e-5 ? 0 : 3;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-schwarzschild-compute: " << error.what() << '\n'; return 1;
  }
}
