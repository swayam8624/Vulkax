#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include "vulkax/physics/physics_ir.hpp"

namespace vulkax::physics {

struct VulkanResourceBinding {
  uint32_t binding = 0;
  VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
  VkFormat imageFormat = VK_FORMAT_UNDEFINED;
  VkExtent3D extent{1, 1, 1};
  VkDeviceSize bufferBytes = 0;
  VkBufferUsageFlags bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkMemoryPropertyFlags memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_STORAGE_BIT |
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  uint32_t historyLength = 1;
  VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT;
};

struct VulkanResourcePlan {
  std::vector<VulkanResourceBinding> bindings;
  std::vector<VkDescriptorPoolSize> poolSizes;
  uint32_t descriptorSetCount = 1;
  uint64_t canonicalHash = 0;
};

// Converts backend-neutral Physics IR reflection into the exact Vulkan resource
// contract. Images are storage images and structured resources are storage
// buffers. History resources contribute one descriptor per history slot.
[[nodiscard]] VulkanResourcePlan makeVulkanResourcePlan(const ResourceLayout& layout);

// Creates the descriptor-set layout described by a reflected graph. The caller
// owns the returned handle and must destroy it with vkDestroyDescriptorSetLayout.
[[nodiscard]] VkDescriptorSetLayout createVulkanDescriptorSetLayout(
    VkDevice device,
    const VulkanResourcePlan& plan,
    const VkAllocationCallbacks* allocator = nullptr);

// Creates a pool large enough to allocate descriptorSetCount sets from the
// reflected layout, including every history slot.
[[nodiscard]] VkDescriptorPool createVulkanDescriptorPool(
    VkDevice device,
    const VulkanResourcePlan& plan,
    const VkAllocationCallbacks* allocator = nullptr);

}  // namespace vulkax::physics
