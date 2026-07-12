#pragma once

#include "beacon/render_stats.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace lve::beacon {

enum class ClusterEncoding : uint8_t {
  ExplicitList = 0,
  DenseBitset = 1,
  CompactRanges = 2,
  Overflow = 255
};

struct GpuPointLight {
  alignas(16) glm::vec4 positionRadius{};
  alignas(16) glm::vec4 colorIntensity{};
};

struct GpuObject {
  alignas(16) glm::mat4 model{1.f};
  alignas(16) glm::mat4 normal{1.f};
  uint32_t materialIndex = 0;
  uint32_t meshIndex = 0;
  uint32_t flags = 0;
  uint32_t padding = 0;
};

struct ClusterHeader {
  uint32_t dataOffset = 0;
  uint32_t storedCount = 0;
  uint32_t totalCandidateCount = 0;
  uint32_t flags = 0;
};

struct AdaptiveClusterNode {
  alignas(16) glm::vec4 boundsMin{};
  alignas(16) glm::vec4 boundsMax{};
  uint32_t firstChild = 0;
  uint32_t clusterHeaderIndex = 0;
  uint16_t depth = 0;
  uint16_t state = 0;
};

struct ClusterCostInput {
  float shadedPixels = 0.f;
  uint32_t candidateLights = 0;
  uint32_t listBytes = 0;
  float buildCost = 0.f;
};

struct ClusterCostWeights {
  float memoryWeight = 0.05f;
  float buildWeight = 1.0f;
  float splitCost = 0.1f;
};

class LightBoundEstimator {
 public:
  float diffuseUpperBound(
      const GpuPointLight& light,
      const glm::vec3& clusterMin,
      const glm::vec3& clusterMax,
      float maxReflectance = 1.f) const;
};

class ClusterEncodingSelector {
 public:
  ClusterEncoding choose(uint32_t retainedLights, uint32_t totalLights) const;
  uint64_t estimateBytes(uint32_t retainedLights, uint32_t totalLights, ClusterEncoding encoding) const;

 private:
  float denseFractionThreshold = 0.35f;
};

class BudgetController {
 public:
  BudgetController() = default;
  BudgetController(float clusterBuildBudgetMs, float lightingBudgetMs, float imageErrorBudget);

  void update(const RenderStats& stats);
  float splitPenalty() const { return currentSplitPenalty; }
  float pruningScale() const { return currentPruningScale; }
  uint32_t maxSubdivisionDepth() const { return currentMaxSubdivisionDepth; }
  float errorBudget() const { return currentImageErrorBudget * currentPruningScale; }

 private:
  float targetClusterBuildMs = 0.7f;
  float targetLightingMs = 3.0f;
  float currentImageErrorBudget = 0.005f;
  double clusterBuildEwmaMs = 0.0;
  double lightingEwmaMs = 0.0;
  float currentSplitPenalty = 1.0f;
  float currentPruningScale = 1.0f;
  uint32_t currentMaxSubdivisionDepth = 4;
  double alpha = 0.1;
};

float estimateClusterCost(const ClusterCostInput& input, const ClusterCostWeights& weights);

}  // namespace lve::beacon
