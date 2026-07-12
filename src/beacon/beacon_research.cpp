#include "beacon/beacon_research.hpp"

#include <algorithm>
#include <cmath>

namespace lve::beacon {

BudgetController::BudgetController(
    float clusterBuildBudgetMs,
    float lightingBudgetMs,
    float imageErrorBudget)
    : targetClusterBuildMs{clusterBuildBudgetMs},
      targetLightingMs{lightingBudgetMs},
      currentImageErrorBudget{imageErrorBudget} {}

float LightBoundEstimator::diffuseUpperBound(
    const GpuPointLight& light,
    const glm::vec3& clusterMin,
    const glm::vec3& clusterMax,
    float maxReflectance) const {
  glm::vec3 p{light.positionRadius};
  glm::vec3 closest{
      std::clamp(p.x, clusterMin.x, clusterMax.x),
      std::clamp(p.y, clusterMin.y, clusterMax.y),
      std::clamp(p.z, clusterMin.z, clusterMax.z)};
  glm::vec3 delta = p - closest;
  float distanceSquared = std::max(glm::dot(delta, delta), 0.0001f);
  float intensity = light.colorIntensity.w;
  return intensity * std::max(maxReflectance, 0.f) / distanceSquared;
}

ClusterEncoding ClusterEncodingSelector::choose(uint32_t retainedLights, uint32_t totalLights) const {
  if (totalLights == 0 || retainedLights == 0) return ClusterEncoding::ExplicitList;
  float fraction = static_cast<float>(retainedLights) / static_cast<float>(totalLights);
  if (fraction >= denseFractionThreshold && totalLights >= 64) {
    return ClusterEncoding::DenseBitset;
  }
  return ClusterEncoding::ExplicitList;
}

uint64_t ClusterEncodingSelector::estimateBytes(
    uint32_t retainedLights,
    uint32_t totalLights,
    ClusterEncoding encoding) const {
  switch (encoding) {
    case ClusterEncoding::DenseBitset:
      return ((static_cast<uint64_t>(totalLights) + 31u) / 32u) * sizeof(uint32_t);
    case ClusterEncoding::CompactRanges:
      return static_cast<uint64_t>(retainedLights) * sizeof(uint32_t);
    case ClusterEncoding::Overflow:
      return static_cast<uint64_t>(retainedLights) * sizeof(uint32_t);
    case ClusterEncoding::ExplicitList:
    default:
      return static_cast<uint64_t>(retainedLights) * sizeof(uint32_t);
  }
}

void BudgetController::update(const RenderStats& stats) {
  if (clusterBuildEwmaMs == 0.0) {
    clusterBuildEwmaMs = stats.gpu.clusterBuildMs;
    lightingEwmaMs = stats.gpu.lightingPassMs;
  } else {
    clusterBuildEwmaMs = alpha * stats.gpu.clusterBuildMs + (1.0 - alpha) * clusterBuildEwmaMs;
    lightingEwmaMs = alpha * stats.gpu.lightingPassMs + (1.0 - alpha) * lightingEwmaMs;
  }

  if (clusterBuildEwmaMs > targetClusterBuildMs) {
    currentSplitPenalty = std::min(currentSplitPenalty * 1.05f, 8.0f);
    currentMaxSubdivisionDepth = std::max<uint32_t>(1, currentMaxSubdivisionDepth - 1);
  } else if (lightingEwmaMs > targetLightingMs) {
    currentPruningScale = std::min(currentPruningScale * 1.05f, 4.0f);
  } else {
    currentSplitPenalty = std::max(currentSplitPenalty * 0.995f, 1.0f);
    currentPruningScale = std::max(currentPruningScale * 0.995f, 1.0f);
  }
}

float estimateClusterCost(const ClusterCostInput& input, const ClusterCostWeights& weights) {
  return input.shadedPixels * static_cast<float>(input.candidateLights) +
         weights.memoryWeight * static_cast<float>(input.listBytes) +
         weights.buildWeight * input.buildCost + weights.splitCost;
}

}  // namespace lve::beacon
