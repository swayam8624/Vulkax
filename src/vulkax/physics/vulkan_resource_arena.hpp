#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vulkax/physics/physics_ir.hpp"
#include "vulkax/physics/vulkan_resource_layout.hpp"

namespace vulkax::physics {

struct VulkanBarrierSummary {
  uint32_t bufferBarriers = 0;
  uint32_t imageBarriers = 0;
};

struct VulkanMemoryArenaStats {
  uint32_t blockCount = 0;
  uint32_t suballocationCount = 0;
  VkDeviceSize reservedBytes = 0;
  VkDeviceSize resourceBytes = 0;
};

// Imports one externally owned history slot into an arena. Imported handles are
// written into descriptors and synchronized by the arena, but never destroyed
// or mapped through it. This lets multiple reflected pass layouts share a field
// without duplicating its Vulkan allocation.
struct VulkanResourceImport {
  uint32_t binding = 0;
  uint32_t historySlot = 0;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkImageView imageView = VK_NULL_HANDLE;
  VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// Owns the Vulkan resources implied by a reflected resource plan. Descriptor
// array element zero always names the current history slot; later elements walk
// backward through history. A descriptor set is rewritten only when its frame
// slot is explicitly rotated, so callers can avoid updating in-flight sets.
class VulkanResourceArena {
 public:
  VulkanResourceArena(
      VkPhysicalDevice physicalDevice,
      VkDevice device,
      VulkanResourcePlan plan,
      std::span<const VulkanResourceImport> imports = {},
      std::span<const VkPushConstantRange> pushConstants = {},
      const VkAllocationCallbacks* allocator = nullptr);
  ~VulkanResourceArena();

  VulkanResourceArena(const VulkanResourceArena&) = delete;
  VulkanResourceArena& operator=(const VulkanResourceArena&) = delete;

  [[nodiscard]] VkDescriptorSetLayout descriptorSetLayout() const { return descriptorSetLayout_; }
  [[nodiscard]] VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }
  [[nodiscard]] VkDescriptorSet descriptorSet(uint32_t frameIndex = 0) const;
  [[nodiscard]] VkBuffer buffer(uint32_t binding, uint32_t historyAge = 0) const;
  [[nodiscard]] VkImage image(uint32_t binding, uint32_t historyAge = 0) const;
  [[nodiscard]] VkImageView imageView(uint32_t binding, uint32_t historyAge = 0) const;
  [[nodiscard]] uint32_t physicalHistorySlot(
      uint32_t binding, uint32_t historyAge = 0) const;
  [[nodiscard]] const VulkanResourcePlan& plan() const { return plan_; }
  [[nodiscard]] const VulkanMemoryArenaStats& memoryStats() const { return memoryStats_; }

  void uploadBuffer(
      uint32_t binding,
      std::span<const std::byte> bytes,
      uint32_t historyAge = 0,
      VkDeviceSize offset = 0);
  void downloadBuffer(
      uint32_t binding,
      std::span<std::byte> bytes,
      uint32_t historyAge = 0,
      VkDeviceSize offset = 0) const;

  // Records UNDEFINED-to-GENERAL transitions for every reflected storage image.
  // Call once before the first pass that accesses the arena's images.
  void recordInitialTransitions(VkCommandBuffer commandBuffer);

  // Resets dependency tracking for a new graph execution. recordPassBarrier()
  // then emits only write-after/read or write-after-write dependencies required
  // by the reflected resource bindings.
  void beginPassSequence();
  [[nodiscard]] VulkanBarrierSummary recordPassBarrier(
      VkCommandBuffer commandBuffer,
      const ResourceLayout& layout,
      uint32_t passIndex);

  // Advances all multi-slot resources and updates one reusable descriptor set.
  // The caller must ensure that frameIndex is no longer referenced by the GPU.
  void rotateHistory(uint32_t frameIndex = 0);

 private:
  struct Slot {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize memoryOffset = 0;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool owned = true;
  };

  struct MemoryBlock {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDeviceSize used = 0;
    uint32_t memoryTypeIndex = 0;
    bool imageMemory = false;
  };

  struct Suballocation {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
  };

  [[nodiscard]] const VulkanResourceBinding& binding(uint32_t bindingIndex) const;
  [[nodiscard]] Slot& slot(uint32_t bindingIndex, uint32_t historyAge);
  [[nodiscard]] const Slot& slot(uint32_t bindingIndex, uint32_t historyAge) const;
  [[nodiscard]] uint32_t findMemoryType(
      uint32_t typeMask, VkMemoryPropertyFlags properties) const;
  [[nodiscard]] Suballocation allocateMemory(
      const VkMemoryRequirements& requirements,
      VkMemoryPropertyFlags properties,
      bool imageMemory);
  void createResources();
  void createDescriptors(std::span<const VkPushConstantRange> pushConstants);
  void updateDescriptorSet(uint32_t frameIndex);
  void destroy() noexcept;

  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  const VkAllocationCallbacks* allocator_ = nullptr;
  VulkanResourcePlan plan_{};
  std::vector<VulkanResourceImport> imports_;
  std::vector<std::vector<Slot>> slots_;
  std::vector<MemoryBlock> memoryBlocks_;
  VulkanMemoryArenaStats memoryStats_{};
  std::vector<uint32_t> historyCursor_;
  std::vector<bool> previousWrite_;
  std::vector<VkDescriptorSet> descriptorSets_;
  VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
};

}  // namespace vulkax::physics
