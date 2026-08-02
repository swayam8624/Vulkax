#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>

namespace lve {
class LveBuffer;
class LveDevice;
}

namespace vulkax::sim {

class MacLiveVolume {
 public:
  MacLiveVolume(lve::LveDevice& device, VkImageView outputImage, VkExtent2D outputExtent);
  ~MacLiveVolume();

  MacLiveVolume(const MacLiveVolume&) = delete;
  MacLiveVolume& operator=(const MacLiveVolume&) = delete;

  void record(VkCommandBuffer commandBuffer, float deltaSeconds);

 private:
  void createBuffers();
  void initializeFields();
  void createDescriptors();
  void createPipelines();
  void destroyVulkanObjects();

  lve::LveDevice& device_;
  VkImageView outputImage_ = VK_NULL_HANDLE;
  VkExtent2D outputExtent_{};
  std::array<std::unique_ptr<lve::LveBuffer>, 32> buffers_{};
  VkDescriptorSetLayout solverDescriptorLayout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout presentDescriptorLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  VkDescriptorSet solverDescriptorSet_ = VK_NULL_HANDLE;
  VkDescriptorSet presentDescriptorSet_ = VK_NULL_HANDLE;
  VkPipelineLayout solverPipelineLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout presentPipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline solverPipeline_ = VK_NULL_HANDLE;
  VkPipeline presentPipeline_ = VK_NULL_HANDLE;
};

}  // namespace vulkax::sim
