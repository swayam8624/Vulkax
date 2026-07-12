#pragma once

#include "lve_device.hpp"
#include "lve_frame_info.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace lve {

struct ClusterBuildPushConstants {
  uint32_t clusterCount = 0;
  uint32_t lightCount = 0;
  uint32_t maxLightsPerCluster = 0;
  uint32_t lightIndexCapacity = 0;
  glm::vec4 worldMin{};
  glm::vec4 worldMax{};
  glm::uvec4 gridSize{};
};

class ClusteredLightingSystem {
 public:
  ClusteredLightingSystem(LveDevice& device, VkDescriptorSetLayout globalSetLayout);
  ~ClusteredLightingSystem();

  ClusteredLightingSystem(const ClusteredLightingSystem&) = delete;
  ClusteredLightingSystem& operator=(const ClusteredLightingSystem&) = delete;

  void dispatch(VkCommandBuffer commandBuffer, VkDescriptorSet globalDescriptorSet, const ClusterBuildPushConstants& push);

 private:
  static std::vector<char> readFile(const std::string& filepath);
  void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
  void createPipeline();
  void createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);

  LveDevice& lveDevice;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkShaderModule shaderModule = VK_NULL_HANDLE;
};

}  // namespace lve
