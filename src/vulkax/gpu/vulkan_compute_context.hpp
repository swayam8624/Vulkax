#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <functional>
#include <span>
#include <string>

namespace vulkax::gpu {

class VulkanBuffer {
 public:
  VulkanBuffer() = default;
  VulkanBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize size) noexcept;
  ~VulkanBuffer();

  VulkanBuffer(const VulkanBuffer&) = delete;
  VulkanBuffer& operator=(const VulkanBuffer&) = delete;
  VulkanBuffer(VulkanBuffer&& other) noexcept;
  VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

  [[nodiscard]] VkBuffer handle() const noexcept { return buffer_; }
  [[nodiscard]] VkDeviceMemory memory() const noexcept { return memory_; }
  [[nodiscard]] VkDeviceSize size() const noexcept { return size_; }
  [[nodiscard]] explicit operator bool() const noexcept { return buffer_ != VK_NULL_HANDLE; }

  void write(std::span<const std::byte> bytes, VkDeviceSize offset = 0);
  void read(std::span<std::byte> bytes, VkDeviceSize offset = 0) const;

 private:
  void destroy() noexcept;

  VkDevice device_ = VK_NULL_HANDLE;
  VkBuffer buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;
  VkDeviceSize size_ = 0;
};

// Small presentation-independent Vulkan substrate for compute tools and
// numerical runtimes. It owns instance/device/compute queue plus one reusable
// command buffer/fence, including MoltenVK portability enumeration.
class VulkanComputeContext {
 public:
  explicit VulkanComputeContext(std::string applicationName = "Vulkax Compute");
  ~VulkanComputeContext();

  VulkanComputeContext(const VulkanComputeContext&) = delete;
  VulkanComputeContext& operator=(const VulkanComputeContext&) = delete;

  [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
  [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
  [[nodiscard]] VkDevice device() const noexcept { return device_; }
  [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
  [[nodiscard]] uint32_t queueFamily() const noexcept { return queueFamily_; }
  [[nodiscard]] const VkPhysicalDeviceProperties& properties() const noexcept { return properties_; }
  [[nodiscard]] std::string deviceName() const { return properties_.deviceName; }

  [[nodiscard]] VulkanBuffer createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage) const;
  void submitAndWait(const std::function<void(VkCommandBuffer)>& record);

 private:
  [[nodiscard]] uint32_t memoryType(
      uint32_t typeMask, VkMemoryPropertyFlags requiredProperties) const;
  void destroy() noexcept;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties properties_{};
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queueFamily_ = 0;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
  VkFence submitFence_ = VK_NULL_HANDLE;
};

}  // namespace vulkax::gpu
