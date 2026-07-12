#include "beacon/gpu_profiler.hpp"

#include <stdexcept>

namespace lve::beacon {

GpuProfiler::GpuProfiler(LveDevice& device) : device{device} {
  auto capabilities = device.getCapabilities();
  if (!capabilities.timestampQueries) {
    return;
  }
  timestampPeriod = capabilities.properties.limits.timestampPeriod;
  VkQueryPoolCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
  createInfo.queryCount = 128;
  if (vkCreateQueryPool(device.device(), &createInfo, nullptr, &queryPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create BEACON timestamp query pool");
  }
}

GpuProfiler::~GpuProfiler() {
  if (queryPool != VK_NULL_HANDLE) {
    vkDestroyQueryPool(device.device(), queryPool, nullptr);
  }
}

void GpuProfiler::reset(VkCommandBuffer commandBuffer) {
  if (!available()) return;
  nextQuery = 0;
  labels.clear();
  vkCmdResetQueryPool(commandBuffer, queryPool, 0, 128);
}

void GpuProfiler::writeTimestamp(VkCommandBuffer commandBuffer, const std::string& label) {
  if (!available() || nextQuery >= 128) return;
  labels.push_back(label);
  vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, nextQuery++);
}

std::unordered_map<std::string, double> GpuProfiler::collectMilliseconds() {
  std::unordered_map<std::string, double> result;
  if (!available() || nextQuery == 0) return result;
  std::vector<uint64_t> timestamps(nextQuery);
  VkResult status = vkGetQueryPoolResults(
      device.device(),
      queryPool,
      0,
      nextQuery,
      sizeof(uint64_t) * timestamps.size(),
      timestamps.data(),
      sizeof(uint64_t),
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
  if (status != VK_SUCCESS) return result;
  for (uint32_t i = 1; i < nextQuery && i < labels.size(); ++i) {
    double ns = static_cast<double>(timestamps[i] - timestamps[i - 1]) * timestampPeriod;
    result[labels[i - 1] + "_to_" + labels[i]] = ns / 1000000.0;
  }
  return result;
}

}  // namespace lve::beacon
