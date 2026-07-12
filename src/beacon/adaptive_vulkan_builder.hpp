#pragma once

#include "beacon/beacon_research.hpp"
#include "beacon/benchmark_config.hpp"
#include "lve_frame_info.hpp"

#include <vector>

namespace lve::beacon {

struct AdaptiveVulkanBuildResult {
  ClusterConfigData config{};
  std::vector<ClusterHeaderData> headers;
  std::vector<AdaptiveClusterNodeData> nodes;
  std::vector<uint32_t> lightData;
  RenderStats stats{};
};

struct AdaptiveTemporalState {
  std::vector<uint8_t> leafCounts;
  std::vector<uint8_t> lifetimes;
};

AdaptiveVulkanBuildResult buildAdaptiveVulkanClusters(
    const std::vector<SsboPointLight>& lights,
    const glm::vec3& worldMin,
    const glm::vec3& worldMax,
    RenderTechnique technique,
    const BudgetController& controller,
    float imageErrorBudget,
    AdaptiveTemporalState& temporalState);

}  // namespace lve::beacon
