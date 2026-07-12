#pragma once

#include "beacon/benchmark_config.hpp"
#include "beacon/beacon_research.hpp"
#include "beacon/scene_generator.hpp"

#include <vector>

namespace lve::beacon {

struct ClusterExperimentRecord {
  glm::vec3 boundsMin{};
  glm::vec3 boundsMax{};
  uint32_t candidateLights = 0;
  uint32_t retainedLights = 0;
  uint32_t prunedLights = 0;
  uint32_t depth = 0;
  uint64_t listBytes = 0;
  float shadedPixels = 0.f;
  float predictedCost = 0.f;
  float predictedErrorBound = 0.f;
  float exactReference = 0.f;
  float approximateReference = 0.f;
  float modeledSquaredBoundDifference = 0.f;
  ClusterEncoding encoding = ClusterEncoding::ExplicitList;
};

struct ClusterExperimentResult {
  std::vector<ClusterExperimentRecord> clusters;
  RenderStats stats{};
};

ClusterExperimentResult runClusterExperiment(
    const BenchmarkConfig& config,
    const BenchmarkScene& scene,
    const BudgetController& controller);

const char* toString(ClusterEncoding encoding);

}  // namespace lve::beacon
