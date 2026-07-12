#include "beacon/adaptive_vulkan_builder.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lve::beacon {
namespace {
constexpr uint32_t kTilesX = 16;
constexpr uint32_t kTilesY = 9;
constexpr uint32_t kMaxLeaves = 8;
constexpr uint32_t kExplicitCapacity = 128;
constexpr uint32_t kActive = 1u << 1;
constexpr uint32_t kBitset = 1u << 2;
constexpr uint32_t kPruned = 1u << 3;

float distanceSquaredToAabb(const glm::vec3& p, const glm::vec3& lo, const glm::vec3& hi) {
  glm::vec3 q = glm::clamp(p, lo, hi);
  glm::vec3 d = p - q;
  return glm::dot(d, d);
}

float boundFor(const SsboPointLight& light, const glm::vec3& lo, const glm::vec3& hi) {
  float d2 = std::max(distanceSquaredToAabb(glm::vec3{light.positionRadius}, lo, hi), 0.0001f);
  float peak = std::max({light.colorIntensity.r, light.colorIntensity.g, light.colorIntensity.b});
  return peak * std::max(light.colorIntensity.w, 0.f) / d2;
}

bool intersects(const SsboPointLight& light, const glm::vec3& lo, const glm::vec3& hi) {
  float r = std::max(light.positionRadius.w, 0.0001f);
  return distanceSquaredToAabb(glm::vec3{light.positionRadius}, lo, hi) <= r * r;
}
}  // namespace

AdaptiveVulkanBuildResult buildAdaptiveVulkanClusters(
    const std::vector<SsboPointLight>& lights,
    const glm::vec3& worldMin,
    const glm::vec3& worldMax,
    RenderTechnique technique,
    const BudgetController& controller,
    float imageErrorBudget,
    AdaptiveTemporalState& temporalState) {
  AdaptiveVulkanBuildResult out{};
  const uint32_t maxDepth = std::min(controller.maxSubdivisionDepth(), 3u);
  const uint32_t maxLeaves = 1u << maxDepth;
  const uint32_t bitWords = std::max(1u, (static_cast<uint32_t>(lights.size()) + 31u) / 32u);
  const uint32_t stride = std::max(kExplicitCapacity, bitWords);
  const uint32_t slotCount = kTilesX * kTilesY * kMaxLeaves;
  out.headers.resize(slotCount);
  out.nodes.resize(slotCount);
  out.lightData.assign(static_cast<size_t>(slotCount) * stride, 0u);

  out.config.worldMin = glm::vec4{worldMin, 0.f};
  out.config.worldMax = glm::vec4{worldMax, 0.f};
  out.config.gridSize = {kTilesX, kTilesY, kMaxLeaves, maxLeaves};
  out.config.clusterCount = slotCount;
  out.config.maxLightsPerCluster = stride;
  out.config.lightIndexCapacity = static_cast<uint32_t>(out.lightData.size());
  out.config.flags = 1u;
  const uint32_t tileCount = kTilesX * kTilesY;
  if (temporalState.leafCounts.size() != tileCount) {
    temporalState.leafCounts.assign(tileCount, 1u);
    temporalState.lifetimes.assign(tileCount, 0u);
  }
  uint32_t structuralChanges = 0;

  const bool bounded = technique == RenderTechnique::AdaptiveClusteredBounded ||
                       technique == RenderTechnique::BeaconFull;
  glm::vec3 extent = worldMax - worldMin;
  uint64_t retainedTotal = 0;
  for (uint32_t y = 0; y < kTilesY; ++y) {
    for (uint32_t x = 0; x < kTilesX; ++x) {
      glm::vec3 tileMin{worldMin.x + extent.x * x / kTilesX,
                        worldMin.y + extent.y * y / kTilesY, worldMin.z};
      glm::vec3 tileMax{worldMin.x + extent.x * (x + 1) / kTilesX,
                        worldMin.y + extent.y * (y + 1) / kTilesY, worldMax.z};
      uint32_t parentCandidates = 0;
      for (const auto& light : lights) parentCandidates += intersects(light, tileMin, tileMax);
      uint32_t desiredLeaves = 1;
      while (desiredLeaves < maxLeaves && parentCandidates > desiredLeaves * 24u) desiredLeaves *= 2;
      uint32_t tileId = y * kTilesX + x;
      uint32_t leaves = desiredLeaves;
      if (technique == RenderTechnique::BeaconFull) {
        uint32_t previous = temporalState.leafCounts[tileId];
        leaves = previous;
        bool canChange = temporalState.lifetimes[tileId] >= 8u && structuralChanges < 16u;
        if (canChange && desiredLeaves > previous && parentCandidates > previous * 28u) {
          leaves = std::min(desiredLeaves, previous * 2u);
        } else if (canChange && desiredLeaves < previous && parentCandidates < (previous / 2u) * 18u) {
          leaves = std::max(desiredLeaves, previous / 2u);
        }
        if (leaves != previous) {
          structuralChanges++;
          temporalState.lifetimes[tileId] = 0;
          if (leaves > previous) out.stats.splitCount += leaves - previous;
          else out.stats.mergeCount += previous - leaves;
        } else {
          temporalState.lifetimes[tileId] = std::min<uint8_t>(255u, temporalState.lifetimes[tileId] + 1u);
        }
        temporalState.leafCounts[tileId] = static_cast<uint8_t>(leaves);
      }

      for (uint32_t leaf = 0; leaf < leaves; ++leaf) {
        uint32_t slot = (y * kTilesX + x) * kMaxLeaves + leaf;
        glm::vec3 lo = tileMin;
        glm::vec3 hi = tileMax;
        lo.z = worldMin.z + extent.z * leaf / leaves;
        hi.z = worldMin.z + extent.z * (leaf + 1) / leaves;
        out.nodes[slot] = {glm::vec4{lo, 0.f}, glm::vec4{hi, 0.f}};

        std::vector<std::pair<float, uint32_t>> candidates;
        for (uint32_t i = 0; i < lights.size(); ++i) {
          if (intersects(lights[i], lo, hi)) candidates.emplace_back(boundFor(lights[i], lo, hi), i);
        }
        std::sort(candidates.begin(), candidates.end());
        float removed = 0.f;
        size_t firstRetained = 0;
        float budget = imageErrorBudget * controller.pruningScale();
        if (bounded) {
          while (firstRetained < candidates.size() &&
                 removed + candidates[firstRetained].first <= budget) {
            removed += candidates[firstRetained].first;
            ++firstRetained;
          }
        }
        uint32_t retained = static_cast<uint32_t>(candidates.size() - firstRetained);
        bool bitset = retained > kExplicitCapacity ||
                      (!lights.empty() && static_cast<float>(retained) / lights.size() >= 0.35f);
        auto& header = out.headers[slot];
        header.dataOffset = slot * stride;
        header.storedCount = retained;
        header.totalCandidateCount = static_cast<uint32_t>(candidates.size());
        header.flags = kActive | (bitset ? kBitset : 0u) | (firstRetained ? kPruned : 0u);
        if (bitset) {
          out.stats.bitsetClusterCount++;
          for (size_t i = firstRetained; i < candidates.size(); ++i) {
            uint32_t id = candidates[i].second;
            out.lightData[header.dataOffset + id / 32u] |= 1u << (id % 32u);
          }
        } else {
          out.stats.explicitClusterCount++;
          for (uint32_t i = 0; i < retained; ++i) {
            out.lightData[header.dataOffset + i] = candidates[firstRetained + i].second;
          }
        }
        out.stats.activeClusters++;
        out.stats.maximumLightsPerCluster = std::max(out.stats.maximumLightsPerCluster, retained);
        out.stats.predictedErrorBound += removed;
        out.stats.prunedLightSamples += firstRetained;
        out.stats.evaluatedLightSamples += retained;
        retainedTotal += retained;
        if (technique != RenderTechnique::BeaconFull && leaves > 1) out.stats.splitCount++;
      }
    }
  }
  out.stats.activeLights = static_cast<uint32_t>(lights.size());
  out.stats.lightListBytes = out.lightData.size() * sizeof(uint32_t);
  out.stats.averageLightsPerCluster = out.stats.activeClusters
      ? static_cast<float>(retainedTotal) / out.stats.activeClusters : 0.f;
  out.stats.clusterChurn = out.stats.activeClusters
      ? static_cast<float>(out.stats.splitCount + out.stats.mergeCount) / out.stats.activeClusters : 0.f;
  return out;
}

}  // namespace lve::beacon
