#include "vulkax/editor/vulkan_field_executor.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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

uint32_t findDeviceLocalMemoryType(VkPhysicalDevice physical, uint32_t typeMask) {
  VkPhysicalDeviceMemoryProperties properties{};
  vkGetPhysicalDeviceMemoryProperties(physical, &properties);
  for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
    const bool compatible = (typeMask & (1u << index)) != 0;
    if (compatible && (properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
      return index;
    }
  }
  throw std::runtime_error("no device-local Vulkan memory type");
}

}  // namespace

struct VulkanFieldExecutor::Impl {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkBuffer output = VK_NULL_HANDLE;
  VkDeviceMemory outputMemory = VK_NULL_HANDLE;
  VkBuffer uniform = VK_NULL_HANDLE;
  VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
  VkDeviceSize outputCapacity = 0;
  VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  VkImage hdrImage = VK_NULL_HANDLE;
  VkDeviceMemory hdrImageMemory = VK_NULL_HANDLE;
  VkImageView hdrImageView = VK_NULL_HANDLE;
  VkExtent2D hdrExtent{};
  bool hdrImageInitialized = false;
  VkDescriptorSetLayout hdrDescriptorLayout = VK_NULL_HANDLE;
  VkPipelineLayout hdrPipelineLayout = VK_NULL_HANDLE;
  VkPipeline hdrPipeline = VK_NULL_HANDLE;
  VkDescriptorPool hdrDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet hdrDescriptorSet = VK_NULL_HANDLE;
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

  ~Impl() { destroy(); }

  void destroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) {
    if (device != VK_NULL_HANDLE && buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
    if (device != VK_NULL_HANDLE && memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
  }

  void destroyHdrImage() {
    if (device != VK_NULL_HANDLE && hdrImageView != VK_NULL_HANDLE) vkDestroyImageView(device, hdrImageView, nullptr);
    if (device != VK_NULL_HANDLE && hdrImage != VK_NULL_HANDLE) vkDestroyImage(device, hdrImage, nullptr);
    if (device != VK_NULL_HANDLE && hdrImageMemory != VK_NULL_HANDLE) vkFreeMemory(device, hdrImageMemory, nullptr);
    hdrImage = VK_NULL_HANDLE;
    hdrImageMemory = VK_NULL_HANDLE;
    hdrImageView = VK_NULL_HANDLE;
    hdrExtent = {};
    hdrImageInitialized = false;
  }

  void destroy() {
    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
    if (device != VK_NULL_HANDLE && timestampPool != VK_NULL_HANDLE) vkDestroyQueryPool(device, timestampPool, nullptr);
    if (device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
    if (device != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
    if (device != VK_NULL_HANDLE && descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (device != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
    if (device != VK_NULL_HANDLE && pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (device != VK_NULL_HANDLE && descriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    if (device != VK_NULL_HANDLE && hdrDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, hdrDescriptorPool, nullptr);
    if (device != VK_NULL_HANDLE && hdrPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, hdrPipeline, nullptr);
    if (device != VK_NULL_HANDLE && hdrPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, hdrPipelineLayout, nullptr);
    if (device != VK_NULL_HANDLE && hdrDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, hdrDescriptorLayout, nullptr);
    if (device != VK_NULL_HANDLE && reactionDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, reactionDescriptorPool, nullptr);
    if (device != VK_NULL_HANDLE && reactionPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, reactionPipeline, nullptr);
    if (device != VK_NULL_HANDLE && reactionPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, reactionPipelineLayout, nullptr);
    if (device != VK_NULL_HANDLE && reactionDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, reactionDescriptorLayout, nullptr);
    for (uint32_t index = 0; index < 2; ++index) {
      destroyBuffer(reactionA[index], reactionAMemory[index]);
      destroyBuffer(reactionB[index], reactionBMemory[index]);
    }
    destroyBuffer(reactionUniform, reactionUniformMemory);
    destroyHdrImage();
    destroyBuffer(output, outputMemory);
    destroyBuffer(uniform, uniformMemory);
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

  void updateDescriptors() {
    VkDescriptorBufferInfo outputInfo{output, 0, outputCapacity};
    VkDescriptorBufferInfo uniformInfo{uniform, 0, sizeof(WaveParameters)};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1,
                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outputInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uniformInfo, nullptr};
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  void ensureOutputCapacity(VkDeviceSize bytes) {
    if (bytes <= outputCapacity) return;
    if (output != VK_NULL_HANDLE) destroyBuffer(output, outputMemory);
    allocateBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, output, outputMemory);
    outputCapacity = bytes;
    updateDescriptors();
  }

  void updateHdrDescriptors() {
    VkDescriptorBufferInfo outputInfo{output, 0, outputCapacity};
    VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, hdrImageView, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo uniformInfo{uniform, 0, sizeof(WaveParameters)};
    std::array<VkWriteDescriptorSet, 3> writes{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, hdrDescriptorSet, 0, 0, 1,
                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outputInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, hdrDescriptorSet, 1, 0, 1,
                 VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imageInfo, nullptr, nullptr};
    writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, hdrDescriptorSet, 2, 0, 1,
                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uniformInfo, nullptr};
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  void ensureHdrImage(uint32_t width, uint32_t height) {
    if (hdrImage != VK_NULL_HANDLE && hdrExtent.width == width && hdrExtent.height == height) return;
    destroyHdrImage();
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    create.extent = {width, height, 1};
    create.mipLevels = 1;
    create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device, &create, nullptr, &hdrImage), "vkCreateImage HDR preview");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, hdrImage, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findDeviceLocalMemoryType(physical, requirements.memoryTypeBits);
    check(vkAllocateMemory(device, &allocation, nullptr, &hdrImageMemory), "vkAllocateMemory HDR preview");
    check(vkBindImageMemory(device, hdrImage, hdrImageMemory, 0), "vkBindImageMemory HDR preview");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = hdrImage;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = create.format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    check(vkCreateImageView(device, &view, nullptr, &hdrImageView), "vkCreateImageView HDR preview");
    hdrExtent = {width, height};
    updateHdrDescriptors();
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
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    allocateBuffer(sizeof(WaveParameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform, uniformMemory);
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout), "vkCreateDescriptorSetLayout");
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout");

    const auto shaderBytes = readBinary(std::filesystem::path(ENGINE_DIR) / "shaders/vulkax_wave_field.comp.spv");
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = shaderBytes.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(shaderBytes.data());
    VkShaderModule shader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule");
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                          VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", nullptr};
    pipelineInfo.layout = pipelineLayout;
    const VkResult pipelineResult = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device, shader, nullptr);
    check(pipelineResult, "vkCreateComputePipelines");

    VkDescriptorPoolSize poolSizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = descriptorPool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &descriptorLayout;
    check(vkAllocateDescriptorSets(device, &setInfo, &descriptorSet), "vkAllocateDescriptorSets");

    VkDescriptorSetLayoutBinding hdrBindings[3]{};
    hdrBindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    hdrBindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    hdrBindings[2] = {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo hdrLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    hdrLayoutInfo.bindingCount = 3;
    hdrLayoutInfo.pBindings = hdrBindings;
    check(vkCreateDescriptorSetLayout(device, &hdrLayoutInfo, nullptr, &hdrDescriptorLayout),
          "vkCreateDescriptorSetLayout HDR preview");
    VkPipelineLayoutCreateInfo hdrPipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    hdrPipelineLayoutInfo.setLayoutCount = 1;
    hdrPipelineLayoutInfo.pSetLayouts = &hdrDescriptorLayout;
    check(vkCreatePipelineLayout(device, &hdrPipelineLayoutInfo, nullptr, &hdrPipelineLayout),
          "vkCreatePipelineLayout HDR preview");
    const auto hdrShaderBytes = readBinary(std::filesystem::path(ENGINE_DIR) / "shaders/vulkax_field_visualize.comp.spv");
    VkShaderModuleCreateInfo hdrShaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    hdrShaderInfo.codeSize = hdrShaderBytes.size();
    hdrShaderInfo.pCode = reinterpret_cast<const uint32_t*>(hdrShaderBytes.data());
    VkShaderModule hdrShader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &hdrShaderInfo, nullptr, &hdrShader), "vkCreateShaderModule HDR preview");
    VkComputePipelineCreateInfo hdrPipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    hdrPipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                             VK_SHADER_STAGE_COMPUTE_BIT, hdrShader, "main", nullptr};
    hdrPipelineInfo.layout = hdrPipelineLayout;
    const VkResult hdrPipelineResult = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &hdrPipelineInfo, nullptr, &hdrPipeline);
    vkDestroyShaderModule(device, hdrShader, nullptr);
    check(hdrPipelineResult, "vkCreateComputePipelines HDR preview");
    VkDescriptorPoolSize hdrPoolSizes[3]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo hdrPoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    hdrPoolInfo.maxSets = 1;
    hdrPoolInfo.poolSizeCount = 3;
    hdrPoolInfo.pPoolSizes = hdrPoolSizes;
    check(vkCreateDescriptorPool(device, &hdrPoolInfo, nullptr, &hdrDescriptorPool),
          "vkCreateDescriptorPool HDR preview");
    VkDescriptorSetAllocateInfo hdrSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    hdrSetInfo.descriptorPool = hdrDescriptorPool;
    hdrSetInfo.descriptorSetCount = 1;
    hdrSetInfo.pSetLayouts = &hdrDescriptorLayout;
    check(vkAllocateDescriptorSets(device, &hdrSetInfo, &hdrDescriptorSet), "vkAllocateDescriptorSets HDR preview");

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
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

GpuFieldResult VulkanFieldExecutor::evaluateWave(const GpuFieldRequest& request) {
  if (!available()) throw std::runtime_error(impl_->diagnostic);
  if (request.width == 0 || request.height == 0) throw std::invalid_argument("GPU field extent must be positive");
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(request.width) * request.height * sizeof(float);
  try {
    impl_->ensureOutputCapacity(bytes);
    impl_->ensureHdrImage(request.width, request.height);
    const WaveParameters parameters{request.width, request.height, request.time, request.amplitude,
                                    request.wavenumber, request.angularFrequency, {0.0f, 0.0f}};
    void* mapped = nullptr;
    check(vkMapMemory(impl_->device, impl_->uniformMemory, 0, sizeof(parameters), 0, &mapped), "vkMapMemory uniform");
    std::memcpy(mapped, &parameters, sizeof(parameters));
    vkUnmapMemory(impl_->device, impl_->uniformMemory);

    check(vkResetFences(impl_->device, 1, &impl_->fence), "vkResetFences");
    check(vkResetCommandBuffer(impl_->command, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(impl_->command, &begin), "vkBeginCommandBuffer");
    if (impl_->timestamps) {
      vkCmdResetQueryPool(impl_->command, impl_->timestampPool, 0, 2);
      vkCmdWriteTimestamp(impl_->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, impl_->timestampPool, 0);
    }
    vkCmdBindPipeline(impl_->command, VK_PIPELINE_BIND_POINT_COMPUTE, impl_->pipeline);
    vkCmdBindDescriptorSets(impl_->command, VK_PIPELINE_BIND_POINT_COMPUTE, impl_->pipelineLayout, 0, 1,
                            &impl_->descriptorSet, 0, nullptr);
    vkCmdDispatch(impl_->command, (request.width + 15) / 16, (request.height + 15) / 16, 1);
    VkBufferMemoryBarrier fieldBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    fieldBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    fieldBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    fieldBarrier.buffer = impl_->output;
    fieldBarrier.size = bytes;
    VkImageMemoryBarrier imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    imageBarrier.srcAccessMask = impl_->hdrImageInitialized ? VK_ACCESS_SHADER_WRITE_BIT : 0;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageBarrier.oldLayout = impl_->hdrImageInitialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageBarrier.image = impl_->hdrImage;
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(impl_->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &fieldBarrier, 1, &imageBarrier);
    vkCmdBindPipeline(impl_->command, VK_PIPELINE_BIND_POINT_COMPUTE, impl_->hdrPipeline);
    vkCmdBindDescriptorSets(impl_->command, VK_PIPELINE_BIND_POINT_COMPUTE, impl_->hdrPipelineLayout, 0, 1,
                            &impl_->hdrDescriptorSet, 0, nullptr);
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
    check(vkMapMemory(impl_->device, impl_->outputMemory, 0, bytes, 0, &mapped), "vkMapMemory output");
    std::memcpy(result.values.data(), mapped, static_cast<size_t>(bytes));
    vkUnmapMemory(impl_->device, impl_->outputMemory);
    if (impl_->timestamps) {
      std::array<uint64_t, 2> ticks{};
      check(vkGetQueryPoolResults(impl_->device, impl_->timestampPool, 0, 2, sizeof(ticks), ticks.data(),
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
            "vkGetQueryPoolResults");
      result.dispatchMilliseconds = static_cast<double>(ticks[1] - ticks[0]) * impl_->timestampPeriod / 1'000'000.0;
    }
    return result;
  } catch (const std::exception& error) {
    impl_->ready = false;
    impl_->diagnostic = std::string("GPU preview failed; CPU fallback: ") + error.what();
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
