#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace lve {
class LveBuffer;
class LveDevice;
}

namespace vulkax::relativity {

class KerrLiveQueue {
 public:
  KerrLiveQueue(
      lve::LveDevice& device,
      VkImageView outputImage,
      VkExtent2D outputExtent,
      float spin);
  ~KerrLiveQueue();

  KerrLiveQueue(const KerrLiveQueue&) = delete;
  KerrLiveQueue& operator=(const KerrLiveQueue&) = delete;

  void record(VkCommandBuffer commandBuffer, uint32_t integrationDispatches, uint32_t frameIndex);

  [[nodiscard]] uint32_t rayCount() const { return rayCount_; }
  [[nodiscard]] uint32_t queueWidth() const { return queueWidth_; }
  [[nodiscard]] uint32_t queueHeight() const { return queueHeight_; }

 private:
  void createBuffers();
  void initializeRays();
  void createDescriptors();
  void createPipelines();
  void destroyVulkanObjects();

  lve::LveDevice& device_;
  VkImageView outputImage_ = VK_NULL_HANDLE;
  VkExtent2D outputExtent_{};
  float spin_ = 0.0f;
  uint32_t queueWidth_ = 0;
  uint32_t queueHeight_ = 0;
  uint32_t rayCount_ = 0;
  uint32_t groupCount_ = 0;
  uint32_t queueIteration_ = 0;

  std::vector<std::unique_ptr<lve::LveBuffer>> buffers_;
  VkDescriptorSetLayout queueDescriptorLayout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout shadeDescriptorLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  VkDescriptorSet queueDescriptorSet_ = VK_NULL_HANDLE;
  VkDescriptorSet shadeDescriptorSet_ = VK_NULL_HANDLE;
  VkPipelineLayout queuePipelineLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout shadePipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline queuePipeline_ = VK_NULL_HANDLE;
  VkPipeline shadePipeline_ = VK_NULL_HANDLE;
};

}  // namespace vulkax::relativity
