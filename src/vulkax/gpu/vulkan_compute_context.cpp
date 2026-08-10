#include "vulkax/gpu/vulkan_compute_context.hpp"

#include "vulkax/gpu/vk_result.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace vulkax::gpu {
namespace {

bool hasInstanceExtension(const char* name) {
  uint32_t count = 0;
  checkVk(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
          "enumerate Vulkan instance extension count");
  std::vector<VkExtensionProperties> extensions(count);
  checkVk(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()),
          "enumerate Vulkan instance extensions");
  return std::any_of(extensions.begin(), extensions.end(), [&](const VkExtensionProperties& item) {
    return std::strcmp(item.extensionName, name) == 0;
  });
}

bool hasDeviceExtension(VkPhysicalDevice physicalDevice, const char* name) {
  uint32_t count = 0;
  checkVk(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr),
          "enumerate Vulkan device extension count");
  std::vector<VkExtensionProperties> extensions(count);
  checkVk(vkEnumerateDeviceExtensionProperties(
              physicalDevice, nullptr, &count, extensions.data()),
          "enumerate Vulkan device extensions");
  return std::any_of(extensions.begin(), extensions.end(), [&](const VkExtensionProperties& item) {
    return std::strcmp(item.extensionName, name) == 0;
  });
}

}  // namespace

VulkanBuffer::VulkanBuffer(
    VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize size) noexcept
    : device_{device}, buffer_{buffer}, memory_{memory}, size_{size} {}

VulkanBuffer::~VulkanBuffer() { destroy(); }

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept { *this = std::move(other); }

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
  if (this == &other) return *this;
  destroy();
  device_ = std::exchange(other.device_, VK_NULL_HANDLE);
  buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
  memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
  size_ = std::exchange(other.size_, 0);
  return *this;
}

void VulkanBuffer::write(std::span<const std::byte> bytes, VkDeviceSize offset) {
  if (offset > size_ || bytes.size() > size_ - offset) {
    throw std::out_of_range("VulkanBuffer::write exceeds allocation");
  }
  void* mapped = nullptr;
  checkVk(vkMapMemory(device_, memory_, offset, bytes.size(), 0, &mapped), "map Vulkan host buffer");
  std::memcpy(mapped, bytes.data(), bytes.size());
  vkUnmapMemory(device_, memory_);
}

void VulkanBuffer::read(std::span<std::byte> bytes, VkDeviceSize offset) const {
  if (offset > size_ || bytes.size() > size_ - offset) {
    throw std::out_of_range("VulkanBuffer::read exceeds allocation");
  }
  void* mapped = nullptr;
  checkVk(vkMapMemory(device_, memory_, offset, bytes.size(), 0, &mapped), "map Vulkan host buffer");
  std::memcpy(bytes.data(), mapped, bytes.size());
  vkUnmapMemory(device_, memory_);
}

void VulkanBuffer::destroy() noexcept {
  if (device_ != VK_NULL_HANDLE && buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, buffer_, nullptr);
  if (device_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, memory_, nullptr);
  device_ = VK_NULL_HANDLE;
  buffer_ = VK_NULL_HANDLE;
  memory_ = VK_NULL_HANDLE;
  size_ = 0;
}

VulkanComputeContext::VulkanComputeContext(std::string applicationName) {
  VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  application.pApplicationName = applicationName.c_str();
  application.pEngineName = "Vulkax";
  application.apiVersion = VK_API_VERSION_1_1;

  std::vector<const char*> instanceExtensions;
  VkInstanceCreateFlags instanceFlags = 0;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
  if (hasInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
#endif

  VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instanceInfo.flags = instanceFlags;
  instanceInfo.pApplicationInfo = &application;
  instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
  instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
  checkVk(vkCreateInstance(&instanceInfo, nullptr, &instance_), "create Vulkan compute instance");

  try {
    uint32_t physicalCount = 0;
    checkVk(vkEnumeratePhysicalDevices(instance_, &physicalCount, nullptr),
            "enumerate Vulkan physical device count");
    if (physicalCount == 0) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    checkVk(vkEnumeratePhysicalDevices(instance_, &physicalCount, physicalDevices.data()),
            "enumerate Vulkan physical devices");

    bool foundQueue = false;
    for (VkPhysicalDevice candidate : physicalDevices) {
      uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
      for (uint32_t family = 0; family < familyCount; ++family) {
        if ((families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
          physicalDevice_ = candidate;
          queueFamily_ = family;
          foundQueue = true;
          break;
        }
      }
      if (foundQueue) break;
    }
    if (!foundQueue) throw std::runtime_error("no Vulkan compute queue family");
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties_);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    std::vector<const char*> deviceExtensions;
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    if (hasDeviceExtension(physicalDevice_, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
      deviceExtensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }
#endif
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    checkVk(vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_),
            "create Vulkan compute device");
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    checkVk(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
            "create Vulkan compute command pool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool_;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    checkVk(vkAllocateCommandBuffers(device_, &commandInfo, &commandBuffer_),
            "allocate Vulkan compute command buffer");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    checkVk(vkCreateFence(device_, &fenceInfo, nullptr, &submitFence_),
            "create Vulkan compute submit fence");
  } catch (...) {
    destroy();
    throw;
  }
}

VulkanComputeContext::~VulkanComputeContext() { destroy(); }

uint32_t VulkanComputeContext::memoryType(
    uint32_t typeMask, VkMemoryPropertyFlags requiredProperties) const {
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memory);
  for (uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
    if ((typeMask & (1u << index)) != 0 &&
        (memory.memoryTypes[index].propertyFlags & requiredProperties) == requiredProperties) {
      return index;
    }
  }
  throw std::runtime_error("no Vulkan memory type satisfies compute buffer requirements");
}

VulkanBuffer VulkanComputeContext::createHostBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage) const {
  if (size == 0) throw std::invalid_argument("Vulkan compute buffer size must be positive");
  VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkBuffer buffer = VK_NULL_HANDLE;
  checkVk(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer), "create Vulkan compute buffer");

  VkDeviceMemory memory = VK_NULL_HANDLE;
  try {
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checkVk(vkAllocateMemory(device_, &allocation, nullptr, &memory),
            "allocate Vulkan compute buffer memory");
    checkVk(vkBindBufferMemory(device_, buffer, memory, 0), "bind Vulkan compute buffer memory");
  } catch (...) {
    if (memory != VK_NULL_HANDLE) vkFreeMemory(device_, memory, nullptr);
    vkDestroyBuffer(device_, buffer, nullptr);
    throw;
  }
  return VulkanBuffer{device_, buffer, memory, size};
}

void VulkanComputeContext::submitAndWait(const std::function<void(VkCommandBuffer)>& record) {
  checkVk(vkResetFences(device_, 1, &submitFence_), "reset Vulkan compute submit fence");
  checkVk(vkResetCommandBuffer(commandBuffer_, 0), "reset Vulkan compute command buffer");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  checkVk(vkBeginCommandBuffer(commandBuffer_, &begin), "begin Vulkan compute command buffer");
  record(commandBuffer_);
  checkVk(vkEndCommandBuffer(commandBuffer_), "end Vulkan compute command buffer");
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &commandBuffer_;
  checkVk(vkQueueSubmit(queue_, 1, &submit, submitFence_), "submit Vulkan compute command buffer");
  checkVk(vkWaitForFences(device_, 1, &submitFence_, VK_TRUE, UINT64_MAX),
          "wait for Vulkan compute submission");
}

void VulkanComputeContext::destroy() noexcept {
  if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
  if (device_ != VK_NULL_HANDLE && submitFence_ != VK_NULL_HANDLE)
    vkDestroyFence(device_, submitFence_, nullptr);
  if (device_ != VK_NULL_HANDLE && commandPool_ != VK_NULL_HANDLE)
    vkDestroyCommandPool(device_, commandPool_, nullptr);
  if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
  if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
  submitFence_ = VK_NULL_HANDLE;
  commandBuffer_ = VK_NULL_HANDLE;
  commandPool_ = VK_NULL_HANDLE;
  queue_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
  physicalDevice_ = VK_NULL_HANDLE;
  instance_ = VK_NULL_HANDLE;
}

}  // namespace vulkax::gpu
