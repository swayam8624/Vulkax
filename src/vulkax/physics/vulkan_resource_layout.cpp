#include "vulkax/physics/vulkan_resource_layout.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace vulkax::physics {
namespace {

VkFormat imageFormat(ResourceFormat format) {
  switch (format) {
    case ResourceFormat::R16Float:
      return VK_FORMAT_R16_SFLOAT;
    case ResourceFormat::R32Float:
      return VK_FORMAT_R32_SFLOAT;
    case ResourceFormat::Rgba16Float:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case ResourceFormat::R32Uint:
      return VK_FORMAT_R32_UINT;
    case ResourceFormat::StructuredBuffer:
      return VK_FORMAT_UNDEFINED;
  }
  throw std::invalid_argument("unknown reflected resource format");
}

void requireDevice(VkDevice device) {
  if (device == VK_NULL_HANDLE) throw std::invalid_argument("Vulkan device must not be null");
}

}  // namespace

VulkanResourcePlan makeVulkanResourcePlan(const ResourceLayout& layout) {
  VulkanResourcePlan plan{};
  plan.canonicalHash = layout.canonicalHash;
  std::map<VkDescriptorType, uint32_t> descriptorCounts;
  for (const ReflectedResource& resource : layout.resources) {
    if (resource.historyLength == 0) {
      throw std::invalid_argument("reflected resource history length must be positive");
    }
    VulkanResourceBinding binding{};
    binding.binding = resource.binding;
    binding.extent = {resource.extent[0], resource.extent[1], resource.extent[2]};
    binding.historyLength = resource.historyLength;
    if (resource.format == ResourceFormat::StructuredBuffer) {
      binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      binding.bufferBytes = std::max<VkDeviceSize>(resource.estimatedBytes, 16);
    } else {
      binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      binding.imageFormat = imageFormat(resource.format);
    }
    descriptorCounts[binding.descriptorType] += binding.historyLength * plan.descriptorSetCount;
    plan.bindings.push_back(binding);
  }
  std::sort(
      plan.bindings.begin(),
      plan.bindings.end(),
      [](const VulkanResourceBinding& left, const VulkanResourceBinding& right) {
        return left.binding < right.binding;
      });
  for (size_t index = 0; index < plan.bindings.size(); ++index) {
    if (plan.bindings[index].binding != index) {
      throw std::invalid_argument("reflected Vulkan bindings must be dense and zero-based");
    }
  }
  for (const auto& [type, count] : descriptorCounts) {
    plan.poolSizes.push_back({type, count});
  }
  return plan;
}

VkDescriptorSetLayout createVulkanDescriptorSetLayout(
    VkDevice device, const VulkanResourcePlan& plan, const VkAllocationCallbacks* allocator) {
  requireDevice(device);
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  bindings.reserve(plan.bindings.size());
  for (const VulkanResourceBinding& reflected : plan.bindings) {
    bindings.push_back(
        {reflected.binding,
         reflected.descriptorType,
         reflected.historyLength,
         reflected.stages,
         nullptr});
  }
  const VkDescriptorSetLayoutCreateInfo createInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      nullptr,
      0,
      static_cast<uint32_t>(bindings.size()),
      bindings.data()};
  VkDescriptorSetLayout result = VK_NULL_HANDLE;
  if (vkCreateDescriptorSetLayout(device, &createInfo, allocator, &result) != VK_SUCCESS) {
    throw std::runtime_error("failed to create reflected Vulkan descriptor-set layout");
  }
  return result;
}

VkDescriptorPool createVulkanDescriptorPool(
    VkDevice device, const VulkanResourcePlan& plan, const VkAllocationCallbacks* allocator) {
  requireDevice(device);
  if (plan.descriptorSetCount == 0) {
    throw std::invalid_argument("Vulkan descriptor pool must allocate at least one set");
  }
  const VkDescriptorPoolCreateInfo createInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      nullptr,
      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      plan.descriptorSetCount,
      static_cast<uint32_t>(plan.poolSizes.size()),
      plan.poolSizes.data()};
  VkDescriptorPool result = VK_NULL_HANDLE;
  if (vkCreateDescriptorPool(device, &createInfo, allocator, &result) != VK_SUCCESS) {
    throw std::runtime_error("failed to create reflected Vulkan descriptor pool");
  }
  return result;
}

}  // namespace vulkax::physics
