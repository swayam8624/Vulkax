#pragma once

#include "lve_device.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lve::beacon {

class GpuProfiler {
 public:
  explicit GpuProfiler(LveDevice& device);
  ~GpuProfiler();

  GpuProfiler(const GpuProfiler&) = delete;
  GpuProfiler& operator=(const GpuProfiler&) = delete;

  bool available() const { return queryPool != VK_NULL_HANDLE; }
  void reset(VkCommandBuffer commandBuffer);
  void writeTimestamp(VkCommandBuffer commandBuffer, const std::string& label);
  std::unordered_map<std::string, double> collectMilliseconds();

 private:
  LveDevice& device;
  VkQueryPool queryPool = VK_NULL_HANDLE;
  float timestampPeriod = 1.f;
  uint32_t nextQuery = 0;
  std::vector<std::string> labels;
};

}  // namespace lve::beacon
