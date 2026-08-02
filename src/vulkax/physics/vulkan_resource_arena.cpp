#include "vulkax/physics/vulkan_resource_arena.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

namespace vulkax::physics {
namespace {

void require(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string{"failed to "} + operation +
                             " (VkResult " + std::to_string(result) + ")");
  }
}

bool isBufferDescriptor(VkDescriptorType type) {
  return type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
         type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

bool isImageDescriptor(VkDescriptorType type) {
  return type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
}

}  // namespace

VulkanResourceArena::VulkanResourceArena(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VulkanResourcePlan plan,
    std::span<const VkPushConstantRange> pushConstants,
    const VkAllocationCallbacks* allocator)
    : physicalDevice_{physicalDevice},
      device_{device},
      allocator_{allocator},
      plan_{std::move(plan)} {
  if (physicalDevice_ == VK_NULL_HANDLE || device_ == VK_NULL_HANDLE) {
    throw std::invalid_argument("reflected Vulkan arena requires a physical device and device");
  }
  if (plan_.descriptorSetCount == 0) {
    throw std::invalid_argument("reflected Vulkan arena requires at least one descriptor set");
  }
  std::sort(
      plan_.bindings.begin(), plan_.bindings.end(),
      [](const auto& left, const auto& right) { return left.binding < right.binding; });
  std::map<VkDescriptorType, uint32_t> descriptorCounts;
  for (uint32_t index = 0; index < plan_.bindings.size(); ++index) {
    const VulkanResourceBinding& resource = plan_.bindings[index];
    if (resource.binding != index || resource.historyLength == 0) {
      throw std::invalid_argument(
          "reflected Vulkan arena bindings must be dense with positive history");
    }
    if (!isBufferDescriptor(resource.descriptorType) &&
        !isImageDescriptor(resource.descriptorType)) {
      throw std::invalid_argument("reflected Vulkan arena received an unsupported descriptor type");
    }
    descriptorCounts[resource.descriptorType] +=
        resource.historyLength * plan_.descriptorSetCount;
  }
  plan_.poolSizes.clear();
  for (const auto& [type, count] : descriptorCounts) plan_.poolSizes.push_back({type, count});
  slots_.resize(plan_.bindings.size());
  historyCursor_.assign(plan_.bindings.size(), 0u);
  previousWrite_.assign(plan_.bindings.size(), false);
  try {
    createResources();
    createDescriptors(pushConstants);
  } catch (...) {
    destroy();
    throw;
  }
}

VulkanResourceArena::~VulkanResourceArena() { destroy(); }

const VulkanResourceBinding& VulkanResourceArena::binding(uint32_t bindingIndex) const {
  if (bindingIndex >= plan_.bindings.size()) {
    throw std::out_of_range("reflected Vulkan binding index is out of range");
  }
  return plan_.bindings[bindingIndex];
}

uint32_t VulkanResourceArena::physicalHistorySlot(
    uint32_t bindingIndex, uint32_t historyAge) const {
  const uint32_t historyLength = binding(bindingIndex).historyLength;
  if (historyAge >= historyLength) {
    throw std::out_of_range("reflected Vulkan history age is out of range");
  }
  return (historyCursor_[bindingIndex] + historyLength - historyAge) % historyLength;
}

VulkanResourceArena::Slot& VulkanResourceArena::slot(
    uint32_t bindingIndex, uint32_t historyAge) {
  return slots_[bindingIndex][physicalHistorySlot(bindingIndex, historyAge)];
}

const VulkanResourceArena::Slot& VulkanResourceArena::slot(
    uint32_t bindingIndex, uint32_t historyAge) const {
  return slots_[bindingIndex][physicalHistorySlot(bindingIndex, historyAge)];
}

VkDescriptorSet VulkanResourceArena::descriptorSet(uint32_t frameIndex) const {
  if (frameIndex >= descriptorSets_.size()) {
    throw std::out_of_range("reflected Vulkan descriptor frame is out of range");
  }
  return descriptorSets_[frameIndex];
}

VkBuffer VulkanResourceArena::buffer(uint32_t bindingIndex, uint32_t historyAge) const {
  if (!isBufferDescriptor(binding(bindingIndex).descriptorType)) {
    throw std::invalid_argument("requested buffer handle from a reflected image binding");
  }
  return slot(bindingIndex, historyAge).buffer;
}

VkImage VulkanResourceArena::image(uint32_t bindingIndex, uint32_t historyAge) const {
  if (!isImageDescriptor(binding(bindingIndex).descriptorType)) {
    throw std::invalid_argument("requested image handle from a reflected buffer binding");
  }
  return slot(bindingIndex, historyAge).image;
}

VkImageView VulkanResourceArena::imageView(uint32_t bindingIndex, uint32_t historyAge) const {
  if (!isImageDescriptor(binding(bindingIndex).descriptorType)) {
    throw std::invalid_argument("requested image view from a reflected buffer binding");
  }
  return slot(bindingIndex, historyAge).imageView;
}

uint32_t VulkanResourceArena::findMemoryType(
    uint32_t typeMask, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memoryProperties{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
  for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
    if ((typeMask & (1u << index)) != 0 &&
        (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties) {
      return index;
    }
  }
  throw std::runtime_error("no Vulkan memory type satisfies reflected resource requirements");
}

void VulkanResourceArena::createResources() {
  for (uint32_t bindingIndex = 0; bindingIndex < plan_.bindings.size(); ++bindingIndex) {
    const VulkanResourceBinding& resource = plan_.bindings[bindingIndex];
    slots_[bindingIndex].resize(resource.historyLength);
    for (Slot& resourceSlot : slots_[bindingIndex]) {
      if (isBufferDescriptor(resource.descriptorType)) {
        if (resource.bufferBytes == 0) {
          throw std::invalid_argument("reflected Vulkan buffer size must be positive");
        }
        const VkBufferCreateInfo bufferInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            resource.bufferBytes,
            resource.bufferUsage,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr};
        require(vkCreateBuffer(device_, &bufferInfo, allocator_, &resourceSlot.buffer),
                "create reflected Vulkan buffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, resourceSlot.buffer, &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            requirements.size,
            findMemoryType(requirements.memoryTypeBits, resource.memoryProperties)};
        require(vkAllocateMemory(device_, &allocation, allocator_, &resourceSlot.memory),
                "allocate reflected Vulkan buffer memory");
        require(vkBindBufferMemory(device_, resourceSlot.buffer, resourceSlot.memory, 0),
                "bind reflected Vulkan buffer memory");
      } else {
        if (resource.imageFormat == VK_FORMAT_UNDEFINED || resource.extent.width == 0 ||
            resource.extent.height == 0 || resource.extent.depth == 0) {
          throw std::invalid_argument("reflected Vulkan image requires a format and non-zero extent");
        }
        const VkImageType imageType = resource.extent.depth > 1
            ? VK_IMAGE_TYPE_3D
            : (resource.extent.height > 1 ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_1D);
        const VkImageViewType viewType = resource.extent.depth > 1
            ? VK_IMAGE_VIEW_TYPE_3D
            : (resource.extent.height > 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_1D);
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = imageType;
        imageInfo.format = resource.imageFormat;
        imageInfo.extent = resource.extent;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = resource.imageUsage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        require(vkCreateImage(device_, &imageInfo, allocator_, &resourceSlot.image),
                "create reflected Vulkan image");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, resourceSlot.image, &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            requirements.size,
            findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        require(vkAllocateMemory(device_, &allocation, allocator_, &resourceSlot.memory),
                "allocate reflected Vulkan image memory");
        require(vkBindImageMemory(device_, resourceSlot.image, resourceSlot.memory, 0),
                "bind reflected Vulkan image memory");
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = resourceSlot.image;
        viewInfo.viewType = viewType;
        viewInfo.format = resource.imageFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        require(vkCreateImageView(device_, &viewInfo, allocator_, &resourceSlot.imageView),
                "create reflected Vulkan image view");
      }
    }
  }
}

void VulkanResourceArena::createDescriptors(
    std::span<const VkPushConstantRange> pushConstants) {
  descriptorSetLayout_ = createVulkanDescriptorSetLayout(device_, plan_, allocator_);
  descriptorPool_ = createVulkanDescriptorPool(device_, plan_, allocator_);
  descriptorSets_.resize(plan_.descriptorSetCount);
  std::vector<VkDescriptorSetLayout> layouts(plan_.descriptorSetCount, descriptorSetLayout_);
  const VkDescriptorSetAllocateInfo allocateInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      nullptr,
      descriptorPool_,
      plan_.descriptorSetCount,
      layouts.data()};
  require(vkAllocateDescriptorSets(device_, &allocateInfo, descriptorSets_.data()),
          "allocate reflected Vulkan descriptor sets");
  VkPipelineLayoutCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineInfo.setLayoutCount = 1;
  pipelineInfo.pSetLayouts = &descriptorSetLayout_;
  pipelineInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
  pipelineInfo.pPushConstantRanges = pushConstants.data();
  require(vkCreatePipelineLayout(device_, &pipelineInfo, allocator_, &pipelineLayout_),
          "create reflected Vulkan pipeline layout");
  for (uint32_t frame = 0; frame < descriptorSets_.size(); ++frame) updateDescriptorSet(frame);
}

void VulkanResourceArena::updateDescriptorSet(uint32_t frameIndex) {
  const VkDescriptorSet set = descriptorSet(frameIndex);
  for (uint32_t bindingIndex = 0; bindingIndex < plan_.bindings.size(); ++bindingIndex) {
    const VulkanResourceBinding& resource = plan_.bindings[bindingIndex];
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = resource.binding;
    write.descriptorCount = resource.historyLength;
    write.descriptorType = resource.descriptorType;
    if (isBufferDescriptor(resource.descriptorType)) {
      std::vector<VkDescriptorBufferInfo> infos;
      infos.reserve(resource.historyLength);
      for (uint32_t age = 0; age < resource.historyLength; ++age) {
        infos.push_back({buffer(bindingIndex, age), 0, resource.bufferBytes});
      }
      write.pBufferInfo = infos.data();
      vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    } else {
      std::vector<VkDescriptorImageInfo> infos;
      infos.reserve(resource.historyLength);
      for (uint32_t age = 0; age < resource.historyLength; ++age) {
        infos.push_back(
            {VK_NULL_HANDLE, imageView(bindingIndex, age), VK_IMAGE_LAYOUT_GENERAL});
      }
      write.pImageInfo = infos.data();
      vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
  }
}

void VulkanResourceArena::uploadBuffer(
    uint32_t bindingIndex,
    std::span<const std::byte> bytes,
    uint32_t historyAge,
    VkDeviceSize offset) {
  const VulkanResourceBinding& resource = binding(bindingIndex);
  if (!isBufferDescriptor(resource.descriptorType) ||
      (resource.memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
    throw std::invalid_argument("reflected Vulkan upload requires a host-visible buffer");
  }
  if (offset + bytes.size() > resource.bufferBytes) {
    throw std::out_of_range("reflected Vulkan upload exceeds buffer capacity");
  }
  Slot& target = slot(bindingIndex, historyAge);
  void* mapped = nullptr;
  require(vkMapMemory(device_, target.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
          "map reflected Vulkan upload buffer");
  std::memcpy(static_cast<std::byte*>(mapped) + offset, bytes.data(), bytes.size());
  if ((resource.memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
    const VkMappedMemoryRange range{
        VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, target.memory, 0, VK_WHOLE_SIZE};
    require(vkFlushMappedMemoryRanges(device_, 1, &range),
            "flush reflected Vulkan upload buffer");
  }
  vkUnmapMemory(device_, target.memory);
}

void VulkanResourceArena::downloadBuffer(
    uint32_t bindingIndex,
    std::span<std::byte> bytes,
    uint32_t historyAge,
    VkDeviceSize offset) const {
  const VulkanResourceBinding& resource = binding(bindingIndex);
  if (!isBufferDescriptor(resource.descriptorType) ||
      (resource.memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
    throw std::invalid_argument("reflected Vulkan download requires a host-visible buffer");
  }
  if (offset + bytes.size() > resource.bufferBytes) {
    throw std::out_of_range("reflected Vulkan download exceeds buffer capacity");
  }
  const Slot& source = slot(bindingIndex, historyAge);
  void* mapped = nullptr;
  require(vkMapMemory(device_, source.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
          "map reflected Vulkan download buffer");
  if ((resource.memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
    const VkMappedMemoryRange range{
        VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, source.memory, 0, VK_WHOLE_SIZE};
    require(vkInvalidateMappedMemoryRanges(device_, 1, &range),
            "invalidate reflected Vulkan download buffer");
  }
  std::memcpy(bytes.data(), static_cast<const std::byte*>(mapped) + offset, bytes.size());
  vkUnmapMemory(device_, source.memory);
}

void VulkanResourceArena::recordInitialTransitions(VkCommandBuffer commandBuffer) {
  if (commandBuffer == VK_NULL_HANDLE) {
    throw std::invalid_argument("initial reflected resource transition needs a command buffer");
  }
  std::vector<VkImageMemoryBarrier> barriers;
  for (auto& bindingSlots : slots_) {
    for (Slot& resourceSlot : bindingSlots) {
      if (resourceSlot.image == VK_NULL_HANDLE ||
          resourceSlot.imageLayout != VK_IMAGE_LAYOUT_UNDEFINED) continue;
      VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = resourceSlot.image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = 1;
      barriers.push_back(barrier);
      resourceSlot.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
  }
  if (!barriers.empty()) {
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        static_cast<uint32_t>(barriers.size()),
        barriers.data());
  }
}

void VulkanResourceArena::beginPassSequence() {
  std::fill(previousWrite_.begin(), previousWrite_.end(), false);
}

VulkanBarrierSummary VulkanResourceArena::recordPassBarrier(
    VkCommandBuffer commandBuffer,
    const ResourceLayout& layout,
    uint32_t passIndex) {
  if (commandBuffer == VK_NULL_HANDLE || passIndex >= layout.passes.size()) {
    throw std::out_of_range("invalid reflected Vulkan pass barrier request");
  }
  if (layout.canonicalHash != plan_.canonicalHash) {
    throw std::invalid_argument("reflected Vulkan pass layout does not match resource arena");
  }
  const ReflectedPass& pass = layout.passes[passIndex];
  std::vector<bool> touched(plan_.bindings.size(), false);
  std::vector<bool> writes(plan_.bindings.size(), false);
  for (uint32_t bindingIndex : pass.readBindings) {
    if (bindingIndex >= touched.size()) throw std::out_of_range("reflected pass read binding");
    touched[bindingIndex] = true;
  }
  for (uint32_t bindingIndex : pass.writeBindings) {
    if (bindingIndex >= touched.size()) throw std::out_of_range("reflected pass write binding");
    touched[bindingIndex] = true;
    writes[bindingIndex] = true;
  }
  std::vector<VkBufferMemoryBarrier> bufferBarriers;
  std::vector<VkImageMemoryBarrier> imageBarriers;
  for (uint32_t bindingIndex = 0; bindingIndex < touched.size(); ++bindingIndex) {
    if (!touched[bindingIndex] || !previousWrite_[bindingIndex]) continue;
    const VulkanResourceBinding& resource = binding(bindingIndex);
    if (isBufferDescriptor(resource.descriptorType)) {
      VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
      barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.buffer = buffer(bindingIndex);
      barrier.offset = 0;
      barrier.size = resource.bufferBytes;
      bufferBarriers.push_back(barrier);
    } else {
      const Slot& resourceSlot = slot(bindingIndex, 0);
      if (resourceSlot.imageLayout != VK_IMAGE_LAYOUT_GENERAL) {
        throw std::logic_error("reflected image must be transitioned before pass barriers");
      }
      VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = resourceSlot.image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = 1;
      imageBarriers.push_back(barrier);
    }
    previousWrite_[bindingIndex] = false;
  }
  if (!bufferBarriers.empty() || !imageBarriers.empty()) {
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        static_cast<uint32_t>(bufferBarriers.size()),
        bufferBarriers.data(),
        static_cast<uint32_t>(imageBarriers.size()),
        imageBarriers.data());
  }
  for (uint32_t bindingIndex = 0; bindingIndex < writes.size(); ++bindingIndex) {
    if (writes[bindingIndex]) previousWrite_[bindingIndex] = true;
  }
  return {
      static_cast<uint32_t>(bufferBarriers.size()),
      static_cast<uint32_t>(imageBarriers.size())};
}

void VulkanResourceArena::rotateHistory(uint32_t frameIndex) {
  if (frameIndex >= descriptorSets_.size()) {
    throw std::out_of_range("reflected Vulkan descriptor frame is out of range");
  }
  for (uint32_t bindingIndex = 0; bindingIndex < plan_.bindings.size(); ++bindingIndex) {
    const uint32_t historyLength = plan_.bindings[bindingIndex].historyLength;
    if (historyLength > 1) {
      historyCursor_[bindingIndex] = (historyCursor_[bindingIndex] + 1u) % historyLength;
    }
  }
  updateDescriptorSet(frameIndex);
  beginPassSequence();
}

void VulkanResourceArena::destroy() noexcept {
  if (device_ == VK_NULL_HANDLE) return;
  if (pipelineLayout_ != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device_, pipelineLayout_, allocator_);
    pipelineLayout_ = VK_NULL_HANDLE;
  }
  if (descriptorPool_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device_, descriptorPool_, allocator_);
    descriptorPool_ = VK_NULL_HANDLE;
  }
  if (descriptorSetLayout_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, allocator_);
    descriptorSetLayout_ = VK_NULL_HANDLE;
  }
  for (auto& bindingSlots : slots_) {
    for (Slot& resourceSlot : bindingSlots) {
      if (resourceSlot.imageView != VK_NULL_HANDLE)
        vkDestroyImageView(device_, resourceSlot.imageView, allocator_);
      if (resourceSlot.image != VK_NULL_HANDLE)
        vkDestroyImage(device_, resourceSlot.image, allocator_);
      if (resourceSlot.buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device_, resourceSlot.buffer, allocator_);
      if (resourceSlot.memory != VK_NULL_HANDLE)
        vkFreeMemory(device_, resourceSlot.memory, allocator_);
      resourceSlot = {};
    }
  }
}

}  // namespace vulkax::physics
