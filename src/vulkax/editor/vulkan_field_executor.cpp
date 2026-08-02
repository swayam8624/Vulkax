#include "vulkax/editor/vulkan_field_executor.hpp"
#include "vulkax/physics/vulkan_resource_arena.hpp"
#include "vulkax/runtime/runtime_contract.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace vulkax::editor {
namespace {

struct alignas(16) WaveParameters {
  uint32_t width = 0;
  uint32_t height = 0;
  float time = 0.0f;
  float amplitude = 1.0f;
  float wavenumber = 2.0f;
  float angularFrequency = 3.0f;
  float padding[2]{};
};
static_assert(sizeof(WaveParameters) == 32);

struct alignas(16) ReactionParameters {
  uint32_t width = 0;
  uint32_t height = 0;
  float timestep = 1.0f / 60.0f;
  float padding = 0.0f;
  float diffusionA = 1.0f;
  float diffusionB = 0.5f;
  float feed = 0.0367f;
  float kill = 0.0649f;
};
static_assert(sizeof(ReactionParameters) == 32);

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
  }
}

std::vector<char> readBinary(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("could not read compute shader: " + path.string());
  const std::streamsize size = file.tellg();
  if (size <= 0) throw std::runtime_error("compute shader is empty: " + path.string());
  std::vector<char> bytes(static_cast<size_t>(size));
  file.seekg(0);
  file.read(bytes.data(), size);
  return bytes;
}

bool hasPortabilityEnumeration() {
  uint32_t count = 0;
  if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) return false;
  std::vector<VkExtensionProperties> extensions(count);
  if (vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) != VK_SUCCESS) return false;
  return std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
    return std::string(extension.extensionName) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
  });
}

bool hasDeviceExtension(VkPhysicalDevice physical, const char* name) {
  uint32_t count = 0;
  if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) != VK_SUCCESS)
    return false;
  std::vector<VkExtensionProperties> extensions(count);
  if (vkEnumerateDeviceExtensionProperties(
          physical, nullptr, &count, extensions.data()) != VK_SUCCESS)
    return false;
  return std::any_of(extensions.begin(), extensions.end(), [&](const auto& extension) {
    return std::string(extension.extensionName) == name;
  });
}

uint32_t findMemoryType(VkPhysicalDevice physical, uint32_t typeMask) {
  VkPhysicalDeviceMemoryProperties properties{};
  vkGetPhysicalDeviceMemoryProperties(physical, &properties);
  for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
    const bool compatible = (typeMask & (1u << index)) != 0;
    const auto flags = properties.memoryTypes[index].propertyFlags;
    if (compatible && (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                          (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      return index;
    }
  }
  throw std::runtime_error("no coherent host-visible Vulkan memory type");
}

float halfToFloat(uint16_t value) {
  const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16;
  int exponent = (value >> 10) & 0x1fu;
  uint32_t mantissa = value & 0x03ffu;
  uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {
      exponent = 1;
      while ((mantissa & 0x0400u) == 0) {
        mantissa <<= 1;
        --exponent;
      }
      mantissa &= 0x03ffu;
      bits = sign | (static_cast<uint32_t>(exponent + 112) << 23) | (mantissa << 13);
    }
  } else if (exponent == 31) {
    bits = sign | 0x7f800000u | (mantissa << 13);
  } else {
    bits = sign | (static_cast<uint32_t>(exponent + 112) << 23) | (mantissa << 13);
  }
  float result = 0.0f;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

}  // namespace

struct VulkanFieldExecutor::Impl {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  std::unique_ptr<physics::VulkanResourceArena> fieldResources;
  std::unique_ptr<physics::VulkanResourceArena> hdrResources;
  VkDeviceSize outputCapacity = 0;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkExtent2D hdrExtent{};
  bool hdrImageInitialized = false;
  VkBuffer hdrReadback = VK_NULL_HANDLE;
  VkDeviceMemory hdrReadbackMemory = VK_NULL_HANDLE;
  VkDeviceSize hdrReadbackCapacity = 0;
  VkPipeline hdrPipeline = VK_NULL_HANDLE;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer command = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkQueryPool timestampPool = VK_NULL_HANDLE;
  VkBuffer reactionA[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkDeviceMemory reactionAMemory[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkBuffer reactionB[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkDeviceMemory reactionBMemory[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkDeviceMemory reactionUniformMemory = VK_NULL_HANDLE;
  VkBuffer reactionUniform = VK_NULL_HANDLE;
  VkDeviceSize reactionCapacity = 0;
  VkDescriptorSetLayout reactionDescriptorLayout = VK_NULL_HANDLE;
  VkPipelineLayout reactionPipelineLayout = VK_NULL_HANDLE;
  VkPipeline reactionPipeline = VK_NULL_HANDLE;
  VkDescriptorPool reactionDescriptorPool = VK_NULL_HANDLE;
  // One descriptor set for each ping-pong direction. Keeping both persistent
  // lets one command buffer record many Gray-Scott substeps without mutating a
  // descriptor set between submissions.
  VkDescriptorSet reactionDescriptorSet[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
  uint32_t reactionCurrent = 0;
  GpuReactionConfig reactionConfig{};
  bool reactionStateReady = false;
  float timestampPeriod = 0.0f;
  bool timestamps = false;
  bool ready = false;
  std::string diagnostic;
  std::string deviceName;
  uint64_t frameIndex = 0;
  VulkaxRuntimeCapabilities capabilities{};
  VulkaxFrameTelemetry telemetry{};

  ~Impl() { destroy(); }

  void destroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) {
    if (device != VK_NULL_HANDLE && buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
    if (device != VK_NULL_HANDLE && memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
  }

  void destroy() {
    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
    if (device != VK_NULL_HANDLE && timestampPool != VK_NULL_HANDLE) vkDestroyQueryPool(device, timestampPool, nullptr);
    if (device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
    if (device != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
    if (device != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
    if (device != VK_NULL_HANDLE && hdrPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, hdrPipeline, nullptr);
    if (device != VK_NULL_HANDLE && reactionDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, reactionDescriptorPool, nullptr);
    if (device != VK_NULL_HANDLE && reactionPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, reactionPipeline, nullptr);
    if (device != VK_NULL_HANDLE && reactionPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, reactionPipelineLayout, nullptr);
    if (device != VK_NULL_HANDLE && reactionDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, reactionDescriptorLayout, nullptr);
    for (uint32_t index = 0; index < 2; ++index) {
      destroyBuffer(reactionA[index], reactionAMemory[index]);
      destroyBuffer(reactionB[index], reactionBMemory[index]);
    }
    destroyBuffer(reactionUniform, reactionUniformMemory);
    hdrResources.reset();
    fieldResources.reset();
    outputCapacity = 0;
    hdrExtent = {};
    hdrImageInitialized = false;
    destroyBuffer(hdrReadback, hdrReadbackMemory);
    hdrReadbackCapacity = 0;
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
  }

  void allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size = size;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &create, nullptr, &buffer), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(physical, requirements.memoryTypeBits);
    check(vkAllocateMemory(device, &allocation, nullptr, &memory), "vkAllocateMemory");
    check(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");
  }

  void ensureFieldResources(uint32_t width, uint32_t height) {
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * sizeof(float);
    if (fieldResources && hdrResources && outputCapacity == bytes &&
        hdrExtent.width == width && hdrExtent.height == height) return;

    const bool rebuildPipelines = pipeline != VK_NULL_HANDLE || hdrPipeline != VK_NULL_HANDLE;
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
    if (hdrPipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, hdrPipeline, nullptr);
      hdrPipeline = VK_NULL_HANDLE;
    }
    hdrResources.reset();
    fieldResources.reset();
    const VkMemoryPropertyFlags hostVisible =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    physics::VulkanResourcePlan fieldPlan{};
    fieldPlan.canonicalHash = 0x4649454c44574156ull;
    fieldPlan.bindings.resize(2);
    fieldPlan.bindings[0].binding = 0;
    fieldPlan.bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    fieldPlan.bindings[0].bufferBytes = bytes;
    fieldPlan.bindings[0].memoryProperties = hostVisible;
    fieldPlan.bindings[1].binding = 1;
    fieldPlan.bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    fieldPlan.bindings[1].bufferBytes = sizeof(WaveParameters);
    fieldPlan.bindings[1].bufferUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    fieldPlan.bindings[1].memoryProperties = hostVisible;
    fieldResources = std::make_unique<physics::VulkanResourceArena>(
        physical, device, std::move(fieldPlan));

    physics::VulkanResourcePlan hdrPlan{};
    hdrPlan.canonicalHash = 0x4844524649454c44ull;
    hdrPlan.bindings.resize(3);
    hdrPlan.bindings[0].binding = 0;
    hdrPlan.bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    hdrPlan.bindings[0].bufferBytes = bytes;
    hdrPlan.bindings[1].binding = 1;
    hdrPlan.bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    hdrPlan.bindings[1].imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    hdrPlan.bindings[1].extent = {width, height, 1};
    hdrPlan.bindings[1].imageUsage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    hdrPlan.bindings[2].binding = 2;
    hdrPlan.bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    hdrPlan.bindings[2].bufferBytes = sizeof(WaveParameters);
    hdrPlan.bindings[2].bufferUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    const std::array imports{
        physics::VulkanResourceImport{0, 0, fieldResources->buffer(0)},
        physics::VulkanResourceImport{2, 0, fieldResources->buffer(1)}};
    hdrResources = std::make_unique<physics::VulkanResourceArena>(
        physical, device, std::move(hdrPlan), imports);
    outputCapacity = bytes;
    hdrExtent = {width, height};
    hdrImageInitialized = false;
    if (rebuildPipelines) createFieldPipelines();
  }

  void createFieldPipelines() {
    const auto shaderBytes =
        readBinary(std::filesystem::path(ENGINE_DIR) / "shaders/vulkax_wave_field.comp.spv");
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = shaderBytes.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(shaderBytes.data());
    VkShaderModule shader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule");
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                          VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", nullptr};
    pipelineInfo.layout = fieldResources->pipelineLayout();
    const VkResult pipelineResult =
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device, shader, nullptr);
    check(pipelineResult, "vkCreateComputePipelines");

    const auto hdrShaderBytes = readBinary(
        std::filesystem::path(ENGINE_DIR) / "shaders/vulkax_field_visualize.comp.spv");
    VkShaderModuleCreateInfo hdrShaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    hdrShaderInfo.codeSize = hdrShaderBytes.size();
    hdrShaderInfo.pCode = reinterpret_cast<const uint32_t*>(hdrShaderBytes.data());
    VkShaderModule hdrShader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &hdrShaderInfo, nullptr, &hdrShader),
          "vkCreateShaderModule HDR preview");
    VkComputePipelineCreateInfo hdrPipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    hdrPipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                             VK_SHADER_STAGE_COMPUTE_BIT, hdrShader, "main", nullptr};
    hdrPipelineInfo.layout = hdrResources->pipelineLayout();
    const VkResult hdrPipelineResult = vkCreateComputePipelines(
        device, VK_NULL_HANDLE, 1, &hdrPipelineInfo, nullptr, &hdrPipeline);
    vkDestroyShaderModule(device, hdrShader, nullptr);
    check(hdrPipelineResult, "vkCreateComputePipelines HDR preview");
  }

  void ensureHdrReadbackCapacity(VkDeviceSize bytes) {
    if (bytes <= hdrReadbackCapacity) return;
    destroyBuffer(hdrReadback, hdrReadbackMemory);
    allocateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, hdrReadback, hdrReadbackMemory);
    hdrReadbackCapacity = bytes;
  }

  void ensureReactionCapacity(VkDeviceSize bytes) {
    if (bytes <= reactionCapacity) return;
    for (uint32_t index = 0; index < 2; ++index) {
      destroyBuffer(reactionA[index], reactionAMemory[index]);
      destroyBuffer(reactionB[index], reactionBMemory[index]);
      allocateBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, reactionA[index], reactionAMemory[index]);
      allocateBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, reactionB[index], reactionBMemory[index]);
    }
    if (reactionUniform == VK_NULL_HANDLE) {
      allocateBuffer(sizeof(ReactionParameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     reactionUniform, reactionUniformMemory);
    }
    reactionCapacity = bytes;
  }

  void updateReactionDescriptors(uint32_t sourceIndex) {
    const uint32_t next = 1 - sourceIndex;
    VkDescriptorBufferInfo infos[5]{
        {reactionA[sourceIndex], 0, reactionCapacity},
        {reactionB[sourceIndex], 0, reactionCapacity},
        {reactionA[next], 0, reactionCapacity},
        {reactionB[next], 0, reactionCapacity},
        {reactionUniform, 0, sizeof(ReactionParameters)}};
    VkWriteDescriptorSet writes[5]{};
    for (uint32_t index = 0; index < 5; ++index) {
      writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, reactionDescriptorSet[sourceIndex], index, 0, 1,
                       index == 4 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                       nullptr, &infos[index], nullptr};
    }
    vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
  }

  void writeMemory(VkDeviceMemory memory, const void* values, size_t bytes, const char* operation) {
    void* mapped = nullptr;
    check(vkMapMemory(device, memory, 0, bytes, 0, &mapped), operation);
    std::memcpy(mapped, values, bytes);
    vkUnmapMemory(device, memory);
  }

  void readMemory(VkDeviceMemory memory, void* values, size_t bytes, const char* operation) {
    void* mapped = nullptr;
    check(vkMapMemory(device, memory, 0, bytes, 0, &mapped), operation);
    std::memcpy(values, mapped, bytes);
    vkUnmapMemory(device, memory);
  }

  void initialize() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Vulkax Physics Studio Compute";
    app.apiVersion = VK_API_VERSION_1_1;
    std::vector<const char*> extensions;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    if (hasPortabilityEnumeration()) {
      extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      instanceInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    uint32_t physicalCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr), "vkEnumeratePhysicalDevices");
    if (physicalCount == 0) throw std::runtime_error("no Vulkan physical device available");
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, physicalDevices.data()), "vkEnumeratePhysicalDevices");
    for (const auto candidate : physicalDevices) {
      uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
      for (uint32_t index = 0; index < familyCount; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
          physical = candidate;
          queueFamily = index;
          break;
        }
      }
      if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) throw std::runtime_error("no Vulkan compute queue available");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    deviceName = properties.deviceName;
    capabilities.abiVersion = VULKAX_RUNTIME_ABI_VERSION;
    capabilities.backend = VULKAX_RUNTIME_BACKEND_VULKAN;
    capabilities.maximumFramesInFlight = 1;
    capabilities.gpuResidentHdr = 1;
    capabilities.asynchronousSubmission = 0;
    telemetry.abiVersion = VULKAX_RUNTIME_ABI_VERSION;
    telemetry.backend = VULKAX_RUNTIME_BACKEND_VULKAN;
    telemetry.framesInFlight = 1;
    timestampPeriod = properties.limits.timestampPeriod;
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
    timestamps = properties.apiVersion >= VK_API_VERSION_1_2 &&
                 families[queueFamily].timestampValidBits != 0;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    std::vector<const char*> deviceExtensions;
    if (hasDeviceExtension(physical, "VK_KHR_portability_subset")) {
      deviceExtensions.push_back("VK_KHR_portability_subset");
    }
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    ensureFieldResources(1, 1);
    createFieldPipelines();
    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = queueFamily;
    check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device, &commandInfo, &command), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    if (timestamps) {
      VkQueryPoolCreateInfo timestampInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      timestampInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
      timestampInfo.queryCount = 2;
      check(vkCreateQueryPool(device, &timestampInfo, nullptr, &timestampPool), "vkCreateQueryPool");
    }

    VkDescriptorSetLayoutBinding reactionBindings[5]{};
    for (uint32_t index = 0; index < 4; ++index) {
      reactionBindings[index] = {index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    reactionBindings[4] = {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo reactionLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    reactionLayoutInfo.bindingCount = 5;
    reactionLayoutInfo.pBindings = reactionBindings;
    check(vkCreateDescriptorSetLayout(device, &reactionLayoutInfo, nullptr, &reactionDescriptorLayout),
          "vkCreateDescriptorSetLayout reaction");
    VkPipelineLayoutCreateInfo reactionPipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    reactionPipelineLayoutInfo.setLayoutCount = 1;
    reactionPipelineLayoutInfo.pSetLayouts = &reactionDescriptorLayout;
    check(vkCreatePipelineLayout(device, &reactionPipelineLayoutInfo, nullptr, &reactionPipelineLayout),
          "vkCreatePipelineLayout reaction");
    const auto reactionShaderBytes = readBinary(
        std::filesystem::path(ENGINE_DIR) / "shaders/vulkax_reaction_diffusion_step.comp.spv");
    VkShaderModuleCreateInfo reactionShaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    reactionShaderInfo.codeSize = reactionShaderBytes.size();
    reactionShaderInfo.pCode = reinterpret_cast<const uint32_t*>(reactionShaderBytes.data());
    VkShaderModule reactionShader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &reactionShaderInfo, nullptr, &reactionShader),
          "vkCreateShaderModule reaction");
    VkComputePipelineCreateInfo reactionPipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    reactionPipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                  VK_SHADER_STAGE_COMPUTE_BIT, reactionShader, "main", nullptr};
    reactionPipelineInfo.layout = reactionPipelineLayout;
    const VkResult reactionPipelineResult = vkCreateComputePipelines(
        device, VK_NULL_HANDLE, 1, &reactionPipelineInfo, nullptr, &reactionPipeline);
    vkDestroyShaderModule(device, reactionShader, nullptr);
    check(reactionPipelineResult, "vkCreateComputePipelines reaction");
    VkDescriptorPoolSize reactionPoolSizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8},
                                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}};
    VkDescriptorPoolCreateInfo reactionPoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    reactionPoolInfo.maxSets = 2;
    reactionPoolInfo.poolSizeCount = 2;
    reactionPoolInfo.pPoolSizes = reactionPoolSizes;
    check(vkCreateDescriptorPool(device, &reactionPoolInfo, nullptr, &reactionDescriptorPool),
          "vkCreateDescriptorPool reaction");
    VkDescriptorSetAllocateInfo reactionSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    reactionSetInfo.descriptorPool = reactionDescriptorPool;
    const VkDescriptorSetLayout reactionLayouts[2]{reactionDescriptorLayout, reactionDescriptorLayout};
    reactionSetInfo.descriptorSetCount = 2;
    reactionSetInfo.pSetLayouts = reactionLayouts;
    check(vkAllocateDescriptorSets(device, &reactionSetInfo, reactionDescriptorSet),
          "vkAllocateDescriptorSets reaction");
    ready = true;
    diagnostic = "Persistent Vulkan compute: " + deviceName;
  }
};

VulkanFieldExecutor::VulkanFieldExecutor() : impl_(new Impl) {
  try {
    impl_->initialize();
  } catch (const std::exception& error) {
    impl_->destroy();
    impl_->diagnostic = std::string("GPU preview unavailable; CPU fallback: ") + error.what();
  }
}

VulkanFieldExecutor::~VulkanFieldExecutor() { delete impl_; }

bool VulkanFieldExecutor::available() const { return impl_ != nullptr && impl_->ready; }
const std::string& VulkanFieldExecutor::diagnostic() const { return impl_->diagnostic; }
const std::string& VulkanFieldExecutor::deviceName() const { return impl_->deviceName; }
VulkaxRuntimeCapabilities VulkanFieldExecutor::runtimeCapabilities() const {
  return impl_ == nullptr ? VulkaxRuntimeCapabilities{} : impl_->capabilities;
}
VulkaxFrameTelemetry VulkanFieldExecutor::latestTelemetry() const {
  return impl_ == nullptr ? VulkaxFrameTelemetry{} : impl_->telemetry;
}

GpuFieldResult VulkanFieldExecutor::evaluateWave(const GpuFieldRequest& request) {
  if (!available()) throw std::runtime_error(impl_->diagnostic);
  const std::array<float, 3> runtimeParameters{
      request.amplitude, request.wavenumber, request.angularFrequency};
  VulkaxFrameRequest frame{};
  frame.abiVersion = VULKAX_RUNTIME_ABI_VERSION;
  frame.visualization = VULKAX_VISUALIZATION_SCALAR_FIELD;
  frame.drawableWidth = request.width;
  frame.drawableHeight = request.height;
  frame.frameIndex = impl_->frameIndex++;
  frame.timelineSeconds = request.time;
  frame.deltaSeconds = 0.0f;
  frame.renderScale = 1.0f;
  frame.parameterCount = static_cast<uint32_t>(runtimeParameters.size());
  frame.parameterHash = 0x776176652d666965ull;
  runtime::validateFrameRequest(frame, runtimeParameters);
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(request.width) * request.height * sizeof(float);
  try {
    impl_->ensureFieldResources(request.width, request.height);
    const WaveParameters parameters{request.width, request.height, request.time, request.amplitude,
                                    request.wavenumber, request.angularFrequency, {0.0f, 0.0f}};
    impl_->fieldResources->uploadBuffer(
        1, std::as_bytes(std::span{&parameters, 1u}));

    check(vkResetFences(impl_->device, 1, &impl_->fence), "vkResetFences");
    check(vkResetCommandBuffer(impl_->command, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(impl_->command, &begin), "vkBeginCommandBuffer");
    if (impl_->timestamps) {
      vkCmdResetQueryPool(impl_->command, impl_->timestampPool, 0, 2);
      vkCmdWriteTimestamp(impl_->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, impl_->timestampPool, 0);
    }
    vkCmdBindPipeline(impl_->command, VK_PIPELINE_BIND_POINT_COMPUTE, impl_->pipeline);
    const VkDescriptorSet fieldSet = impl_->fieldResources->descriptorSet();
    vkCmdBindDescriptorSets(
        impl_->command, VK_PIPELINE_BIND_POINT_COMPUTE,
        impl_->fieldResources->pipelineLayout(), 0, 1, &fieldSet, 0, nullptr);
    vkCmdDispatch(impl_->command, (request.width + 15) / 16, (request.height + 15) / 16, 1);
    if (!impl_->hdrImageInitialized) {
      impl_->hdrResources->recordInitialTransitions(impl_->command);
    }
    VkBufferMemoryBarrier fieldBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    fieldBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    fieldBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    fieldBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fieldBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fieldBarrier.buffer = impl_->fieldResources->buffer(0);
    fieldBarrier.size = bytes;
    VkImageMemoryBarrier imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    imageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = impl_->hdrResources->image(1);
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(impl_->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &fieldBarrier,
                         impl_->hdrImageInitialized ? 1u : 0u,
                         impl_->hdrImageInitialized ? &imageBarrier : nullptr);
    vkCmdBindPipeline(impl_->command, VK_PIPELINE_BIND_POINT_COMPUTE, impl_->hdrPipeline);
    const VkDescriptorSet hdrSet = impl_->hdrResources->descriptorSet();
    vkCmdBindDescriptorSets(
        impl_->command, VK_PIPELINE_BIND_POINT_COMPUTE,
        impl_->hdrResources->pipelineLayout(), 0, 1, &hdrSet, 0, nullptr);
    vkCmdDispatch(impl_->command, (request.width + 15) / 16, (request.height + 15) / 16, 1);
    impl_->hdrImageInitialized = true;
    if (impl_->timestamps) {
      vkCmdWriteTimestamp(impl_->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, impl_->timestampPool, 1);
    }
    check(vkEndCommandBuffer(impl_->command), "vkEndCommandBuffer");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl_->command;
    check(vkQueueSubmit(impl_->queue, 1, &submit, impl_->fence), "vkQueueSubmit");
    check(vkWaitForFences(impl_->device, 1, &impl_->fence, VK_TRUE, std::numeric_limits<uint64_t>::max()), "vkWaitForFences");

    GpuFieldResult result{};
    result.hdrFrameProduced = true;
    result.values.resize(static_cast<size_t>(request.width) * request.height);
    impl_->fieldResources->downloadBuffer(
        0, std::as_writable_bytes(std::span{result.values}));
    if (impl_->timestamps) {
      std::array<uint64_t, 2> ticks{};
      check(vkGetQueryPoolResults(impl_->device, impl_->timestampPool, 0, 2, sizeof(ticks), ticks.data(),
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
            "vkGetQueryPoolResults");
      result.dispatchMilliseconds = static_cast<double>(ticks[1] - ticks[0]) * impl_->timestampPeriod / 1'000'000.0;
    }
    impl_->telemetry.frameIndex = frame.frameIndex;
    impl_->telemetry.simulationMilliseconds = std::max(0.0, result.dispatchMilliseconds);
    impl_->telemetry.renderingMilliseconds = 0.0;
    impl_->telemetry.historySamples = 0;
    impl_->telemetry.frameSubmitted = 1;
    impl_->telemetry.framePresented = 0;
    return result;
  } catch (const std::exception& error) {
    impl_->ready = false;
    impl_->diagnostic = std::string("GPU preview failed; CPU fallback: ") + error.what();
    throw;
  }
}

GpuHdrFrame VulkanFieldExecutor::readHdrFrame() {
  if (!available() || !impl_->hdrImageInitialized || impl_->hdrExtent.width == 0 || impl_->hdrExtent.height == 0) {
    throw std::runtime_error("Vulkan HDR frame is unavailable for export");
  }
  try {
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(impl_->hdrExtent.width) * impl_->hdrExtent.height *
        4u * sizeof(uint16_t);
    impl_->ensureHdrReadbackCapacity(bytes);
    check(vkResetFences(impl_->device, 1, &impl_->fence), "vkResetFences HDR readback");
    check(vkResetCommandBuffer(impl_->command, 0), "vkResetCommandBuffer HDR readback");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(impl_->command, &begin), "vkBeginCommandBuffer HDR readback");
    VkImageMemoryBarrier toCopy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toCopy.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toCopy.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy.image = impl_->hdrResources->image(1);
    toCopy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toCopy.subresourceRange.levelCount = 1;
    toCopy.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(impl_->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toCopy);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {impl_->hdrExtent.width, impl_->hdrExtent.height, 1};
    vkCmdCopyImageToBuffer(impl_->command, impl_->hdrResources->image(1), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           impl_->hdrReadback, 1, &copy);
    VkImageMemoryBarrier restore{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    restore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    restore.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    restore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    restore.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    restore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    restore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    restore.image = impl_->hdrResources->image(1);
    restore.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    restore.subresourceRange.levelCount = 1;
    restore.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(impl_->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &restore);
    check(vkEndCommandBuffer(impl_->command), "vkEndCommandBuffer HDR readback");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl_->command;
    check(vkQueueSubmit(impl_->queue, 1, &submit, impl_->fence), "vkQueueSubmit HDR readback");
    check(vkWaitForFences(impl_->device, 1, &impl_->fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
          "vkWaitForFences HDR readback");
    std::vector<uint16_t> encoded(static_cast<size_t>(bytes / sizeof(uint16_t)));
    void* mapped = nullptr;
    check(vkMapMemory(impl_->device, impl_->hdrReadbackMemory, 0, bytes, 0, &mapped), "vkMapMemory HDR readback");
    std::memcpy(encoded.data(), mapped, static_cast<size_t>(bytes));
    vkUnmapMemory(impl_->device, impl_->hdrReadbackMemory);
    GpuHdrFrame result{};
    result.width = impl_->hdrExtent.width;
    result.height = impl_->hdrExtent.height;
    result.rgba.resize(encoded.size());
    std::transform(encoded.begin(), encoded.end(), result.rgba.begin(), halfToFloat);
    return result;
  } catch (const std::exception& error) {
    impl_->diagnostic = std::string("GPU HDR export failed: ") + error.what();
    throw;
  }
}

void VulkanFieldExecutor::resetReaction(
    const GpuReactionConfig& config,
    const std::vector<float>& initialPrimary,
    const std::vector<float>& initialSecondary) {
  if (!available()) throw std::runtime_error(impl_->diagnostic);
  if (config.width < 3 || config.height < 3 || config.timestep <= 0.0f || config.diffusionA < 0.0f ||
      config.diffusionB < 0.0f || config.feed < 0.0f || config.kill < 0.0f) {
    throw std::invalid_argument("invalid Gray-Scott GPU configuration");
  }
  const size_t count = static_cast<size_t>(config.width) * config.height;
  if (initialPrimary.size() != count || initialSecondary.size() != count) {
    throw std::invalid_argument("Gray-Scott seed fields do not match the requested extent");
  }
  try {
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(count) * sizeof(float);
    impl_->ensureReactionCapacity(bytes);
    impl_->reactionConfig = config;
    impl_->reactionCurrent = 0;
    impl_->writeMemory(impl_->reactionAMemory[0], initialPrimary.data(), static_cast<size_t>(bytes),
                       "vkMapMemory Gray-Scott primary seed");
    impl_->writeMemory(impl_->reactionBMemory[0], initialSecondary.data(), static_cast<size_t>(bytes),
                       "vkMapMemory Gray-Scott secondary seed");
    const ReactionParameters parameters{config.width, config.height, config.timestep, 0.0f,
                                        config.diffusionA, config.diffusionB, config.feed, config.kill};
    impl_->writeMemory(impl_->reactionUniformMemory, &parameters, sizeof(parameters),
                       "vkMapMemory Gray-Scott parameters");
    impl_->updateReactionDescriptors(0);
    impl_->updateReactionDescriptors(1);
    impl_->reactionStateReady = true;
  } catch (const std::exception& error) {
    impl_->reactionStateReady = false;
    impl_->diagnostic = std::string("GPU reaction preview failed; CPU fallback: ") + error.what();
    throw;
  }
}

GpuReactionResult VulkanFieldExecutor::stepReaction(uint32_t steps) {
  if (!available() || !impl_->reactionStateReady) {
    throw std::runtime_error("GPU reaction state is unavailable");
  }
  try {
    if (steps == 0) {
      const size_t count = static_cast<size_t>(impl_->reactionConfig.width) * impl_->reactionConfig.height;
      GpuReactionResult result{};
      result.primary.resize(count);
      result.secondary.resize(count);
      const size_t bytes = count * sizeof(float);
      impl_->readMemory(impl_->reactionAMemory[impl_->reactionCurrent], result.primary.data(), bytes,
                        "vkMapMemory Gray-Scott primary readback");
      impl_->readMemory(impl_->reactionBMemory[impl_->reactionCurrent], result.secondary.data(), bytes,
                        "vkMapMemory Gray-Scott secondary readback");
      return result;
    }
    check(vkResetFences(impl_->device, 1, &impl_->fence), "vkResetFences Gray-Scott");
    check(vkResetCommandBuffer(impl_->command, 0), "vkResetCommandBuffer Gray-Scott");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(impl_->command, &begin), "vkBeginCommandBuffer Gray-Scott");
    if (impl_->timestamps) {
      vkCmdResetQueryPool(impl_->command, impl_->timestampPool, 0, 2);
      vkCmdWriteTimestamp(impl_->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, impl_->timestampPool, 0);
    }
    for (uint32_t step = 0; step < steps; ++step) {
      vkCmdBindPipeline(impl_->command, VK_PIPELINE_BIND_POINT_COMPUTE, impl_->reactionPipeline);
      vkCmdBindDescriptorSets(impl_->command, VK_PIPELINE_BIND_POINT_COMPUTE, impl_->reactionPipelineLayout, 0, 1,
                              &impl_->reactionDescriptorSet[impl_->reactionCurrent], 0, nullptr);
      vkCmdDispatch(impl_->command, (impl_->reactionConfig.width + 15) / 16,
                    (impl_->reactionConfig.height + 15) / 16, 1);
      if (step + 1 < steps) {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(impl_->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
      }
      impl_->reactionCurrent = 1 - impl_->reactionCurrent;
    }
    if (impl_->timestamps) {
      vkCmdWriteTimestamp(impl_->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, impl_->timestampPool, 1);
    }
    check(vkEndCommandBuffer(impl_->command), "vkEndCommandBuffer Gray-Scott");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl_->command;
    check(vkQueueSubmit(impl_->queue, 1, &submit, impl_->fence), "vkQueueSubmit Gray-Scott");
    check(vkWaitForFences(impl_->device, 1, &impl_->fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
          "vkWaitForFences Gray-Scott");
    const size_t count = static_cast<size_t>(impl_->reactionConfig.width) * impl_->reactionConfig.height;
    GpuReactionResult result{};
    result.primary.resize(count);
    result.secondary.resize(count);
    const size_t bytes = count * sizeof(float);
    impl_->readMemory(impl_->reactionAMemory[impl_->reactionCurrent], result.primary.data(), bytes,
                      "vkMapMemory Gray-Scott primary readback");
    impl_->readMemory(impl_->reactionBMemory[impl_->reactionCurrent], result.secondary.data(), bytes,
                      "vkMapMemory Gray-Scott secondary readback");
    if (impl_->timestamps) {
      std::array<uint64_t, 2> ticks{};
      check(vkGetQueryPoolResults(impl_->device, impl_->timestampPool, 0, 2, sizeof(ticks), ticks.data(),
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
            "vkGetQueryPoolResults Gray-Scott");
      result.dispatchMilliseconds = static_cast<double>(ticks[1] - ticks[0]) * impl_->timestampPeriod / 1'000'000.0;
    } else {
      result.dispatchMilliseconds = -1.0;
    }
    return result;
  } catch (const std::exception& error) {
    impl_->reactionStateReady = false;
    impl_->diagnostic = std::string("GPU reaction preview failed; CPU fallback: ") + error.what();
    throw;
  }
}

bool VulkanFieldExecutor::reactionReady() const {
  return available() && impl_->reactionStateReady;
}

}  // namespace vulkax::editor
