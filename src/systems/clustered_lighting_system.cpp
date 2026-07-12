#include "systems/clustered_lighting_system.hpp"

#include <cassert>
#include <fstream>
#include <stdexcept>

#ifndef ENGINE_DIR
#define ENGINE_DIR "../"
#endif

namespace lve {

ClusteredLightingSystem::ClusteredLightingSystem(
    LveDevice& device, VkDescriptorSetLayout globalSetLayout)
    : lveDevice{device} {
  createPipelineLayout(globalSetLayout);
  createPipeline();
}

ClusteredLightingSystem::~ClusteredLightingSystem() {
  vkDestroyShaderModule(lveDevice.device(), shaderModule, nullptr);
  vkDestroyPipeline(lveDevice.device(), pipeline, nullptr);
  vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
}

std::vector<char> ClusteredLightingSystem::readFile(const std::string& filepath) {
  std::string enginePath = ENGINE_DIR + filepath;
  std::ifstream file{enginePath, std::ios::ate | std::ios::binary};
  if (!file.is_open()) {
    throw std::runtime_error("failed to open file: " + enginePath);
  }

  size_t fileSize = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  return buffer;
}

void ClusteredLightingSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(ClusterBuildPushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &globalSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  if (vkCreatePipelineLayout(lveDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create clustered compute pipeline layout!");
  }
}

void ClusteredLightingSystem::createPipeline() {
  assert(pipelineLayout != VK_NULL_HANDLE && "compute pipeline layout must be created first");
  auto shaderCode = readFile("shaders/cluster_build.comp.spv");
  createShaderModule(shaderCode, &shaderModule);

  VkPipelineShaderStageCreateInfo shaderStage{};
  shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shaderStage.module = shaderModule;
  shaderStage.pName = "main";

  VkComputePipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.stage = shaderStage;
  pipelineInfo.layout = pipelineLayout;

  if (vkCreateComputePipelines(
          lveDevice.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create clustered compute pipeline!");
  }
}

void ClusteredLightingSystem::createShaderModule(
    const std::vector<char>& code, VkShaderModule* outShaderModule) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  if (vkCreateShaderModule(lveDevice.device(), &createInfo, nullptr, outShaderModule) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create clustered compute shader module");
  }
}

void ClusteredLightingSystem::dispatch(
    VkCommandBuffer commandBuffer,
    VkDescriptorSet globalDescriptorSet,
    const ClusterBuildPushConstants& push) {
  if (push.clusterCount == 0) return;

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(
      commandBuffer,
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipelineLayout,
      0,
      1,
      &globalDescriptorSet,
      0,
      nullptr);
  vkCmdPushConstants(
      commandBuffer,
      pipelineLayout,
      VK_SHADER_STAGE_COMPUTE_BIT,
      0,
      sizeof(ClusterBuildPushConstants),
      &push);

  uint32_t groups = (push.clusterCount + 63u) / 64u;
  vkCmdDispatch(commandBuffer, groups, 1, 1);

  VkBufferMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = VK_NULL_HANDLE;
  barrier.offset = 0;
  barrier.size = VK_WHOLE_SIZE;

  VkMemoryBarrier memoryBarrier{};
  memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(
      commandBuffer,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      1,
      &memoryBarrier,
      0,
      nullptr,
      0,
      nullptr);
}

}  // namespace lve
