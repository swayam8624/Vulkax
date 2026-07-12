#include "beacon/cluster_experiment.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lve::beacon {
namespace {

float distanceSquaredToAabb(const glm::vec3& point, const glm::vec3& min, const glm::vec3& max) {
  glm::vec3 closest{
      std::clamp(point.x, min.x, max.x),
      std::clamp(point.y, min.y, max.y),
      std::clamp(point.z, min.z, max.z)};
  glm::vec3 delta = point - closest;
  return glm::dot(delta, delta);
}

bool intersectsLight(const GpuPointLight& light, const glm::vec3& min, const glm::vec3& max) {
  glm::vec3 p{light.positionRadius};
  float radius = std::max(light.positionRadius.w, 0.001f);
  return distanceSquaredToAabb(p, min, max) <= radius * radius;
}

float estimatePixels(const BenchmarkConfig& config, uint32_t tilesX, uint32_t tilesY, uint32_t zSlices, uint32_t depth) {
  float tilePixels = (static_cast<float>(config.width) / tilesX) * (static_cast<float>(config.height) / tilesY);
  return tilePixels / std::max(1u, zSlices) * static_cast<float>(1u << depth);
}

std::vector<float> computeBounds(
    const BenchmarkScene& scene,
    const glm::vec3& min,
    const glm::vec3& max,
    const LightBoundEstimator& estimator) {
  std::vector<float> bounds;
  bounds.reserve(scene.lights.size());
  for (const auto& light : scene.lights) {
    if (intersectsLight(light, min, max)) {
      bounds.push_back(estimator.diffuseUpperBound(light, min, max));
    }
  }
  return bounds;
}

ClusterExperimentRecord buildRecord(
    const BenchmarkConfig& config,
    const BenchmarkScene& scene,
    const BudgetController& controller,
    const glm::vec3& min,
    const glm::vec3& max,
    uint32_t depth,
    float shadedPixels,
    bool allowPruning) {
  LightBoundEstimator estimator{};
  ClusterEncodingSelector selector{};
  auto bounds = computeBounds(scene, min, max, estimator);
  std::sort(bounds.begin(), bounds.end());
  float exactReference = std::accumulate(bounds.begin(), bounds.end(), 0.f);

  float removedBound = 0.f;
  uint32_t pruned = 0;
  if (allowPruning) {
    float clusterBudget = controller.errorBudget() * std::max(1u, static_cast<uint32_t>(bounds.size()));
    for (float bound : bounds) {
      if (removedBound + bound > clusterBudget) break;
      removedBound += bound;
      pruned += 1;
    }
  }

  uint32_t retained = static_cast<uint32_t>(bounds.size()) - pruned;
  ClusterEncoding encoding = selector.choose(retained, static_cast<uint32_t>(scene.lights.size()));
  uint64_t bytes = selector.estimateBytes(retained, static_cast<uint32_t>(scene.lights.size()), encoding);
  ClusterCostInput costInput{shadedPixels, retained, static_cast<uint32_t>(bytes), 0.01f * bounds.size()};

  ClusterExperimentRecord record{};
  record.boundsMin = min;
  record.boundsMax = max;
  record.candidateLights = static_cast<uint32_t>(bounds.size());
  record.retainedLights = retained;
  record.prunedLights = pruned;
  record.depth = depth;
  record.listBytes = bytes;
  record.shadedPixels = shadedPixels;
  record.predictedCost = estimateClusterCost(costInput, {});
  record.predictedErrorBound = removedBound;
  record.exactReference = exactReference;
  record.approximateReference = std::max(0.f, exactReference - removedBound);
  float error = record.exactReference - record.approximateReference;
  record.modeledSquaredBoundDifference = error * error;
  record.encoding = encoding;
  return record;
}

void addAdaptiveZ(
    const BenchmarkConfig& config,
    const BenchmarkScene& scene,
    const BudgetController& controller,
    const glm::vec3& min,
    const glm::vec3& max,
    uint32_t depth,
    float shadedPixels,
    bool allowPruning,
    std::vector<ClusterExperimentRecord>& out) {
  ClusterExperimentRecord parent =
      buildRecord(config, scene, controller, min, max, depth, shadedPixels, allowPruning);
  if (depth >= controller.maxSubdivisionDepth() || parent.candidateLights <= 2) {
    out.push_back(parent);
    return;
  }

  float splitZ = 0.5f * (min.z + max.z);
  glm::vec3 leftMax{max.x, max.y, splitZ};
  glm::vec3 rightMin{min.x, min.y, splitZ};
  auto childA = buildRecord(config, scene, controller, min, leftMax, depth + 1, shadedPixels * 0.5f, allowPruning);
  auto childB = buildRecord(config, scene, controller, rightMin, max, depth + 1, shadedPixels * 0.5f, allowPruning);
  float childCost = childA.predictedCost + childB.predictedCost + controller.splitPenalty();

  if (childCost < parent.predictedCost * 0.87f) {
    addAdaptiveZ(config, scene, controller, min, leftMax, depth + 1, shadedPixels * 0.5f, allowPruning, out);
    addAdaptiveZ(config, scene, controller, rightMin, max, depth + 1, shadedPixels * 0.5f, allowPruning, out);
  } else {
    out.push_back(parent);
  }
}

bool isAdaptive(RenderTechnique technique) {
  return technique == RenderTechnique::AdaptiveClusteredExact ||
         technique == RenderTechnique::AdaptiveClusteredBounded ||
         technique == RenderTechnique::BeaconFull;
}

bool isBounded(RenderTechnique technique) {
  return technique == RenderTechnique::AdaptiveClusteredBounded || technique == RenderTechnique::BeaconFull;
}

}  // namespace

ClusterExperimentResult runClusterExperiment(
    const BenchmarkConfig& config,
    const BenchmarkScene& scene,
    const BudgetController& controller) {
  ClusterExperimentResult result{};
  uint32_t tilesX = 16;
  uint32_t tilesY = 9;
  uint32_t zSlices = isAdaptive(config.technique) ? 6 : 24;
  float dx = (scene.worldMax.x - scene.worldMin.x) / static_cast<float>(tilesX);
  float dy = (scene.worldMax.y - scene.worldMin.y) / static_cast<float>(tilesY);
  float dz = (scene.worldMax.z - scene.worldMin.z) / static_cast<float>(zSlices);

  for (uint32_t y = 0; y < tilesY; ++y) {
    for (uint32_t x = 0; x < tilesX; ++x) {
      for (uint32_t z = 0; z < zSlices; ++z) {
        glm::vec3 min{scene.worldMin.x + dx * x, scene.worldMin.y + dy * y, scene.worldMin.z + dz * z};
        glm::vec3 max{min.x + dx, min.y + dy, min.z + dz};
        float pixels = estimatePixels(config, tilesX, tilesY, zSlices, 0);
        if (isAdaptive(config.technique)) {
          addAdaptiveZ(config, scene, controller, min, max, 0, pixels, isBounded(config.technique), result.clusters);
        } else {
          result.clusters.push_back(buildRecord(config, scene, controller, min, max, 0, pixels, false));
        }
      }
    }
  }

  auto& stats = result.stats;
  stats.visibleObjects = static_cast<uint32_t>(scene.objects.size());
  stats.activeLights = static_cast<uint32_t>(scene.lights.size());
  stats.activeClusters = static_cast<uint32_t>(result.clusters.size());
  uint64_t totalLights = 0;
  double weightedSquaredError = 0.0;
  double totalReference = 0.0;
  double totalApproximate = 0.0;
  double totalPixels = 0.0;
  for (const auto& cluster : result.clusters) {
    totalLights += cluster.retainedLights;
    stats.maximumLightsPerCluster = std::max(stats.maximumLightsPerCluster, cluster.retainedLights);
    stats.lightListBytes += cluster.listBytes;
    stats.evaluatedLightSamples += static_cast<uint64_t>(cluster.retainedLights) * static_cast<uint64_t>(cluster.shadedPixels);
    stats.prunedLightSamples += static_cast<uint64_t>(cluster.prunedLights) * static_cast<uint64_t>(cluster.shadedPixels);
    stats.predictedErrorBound += cluster.predictedErrorBound;
    weightedSquaredError +=
        static_cast<double>(cluster.modeledSquaredBoundDifference) * cluster.shadedPixels;
    totalReference += static_cast<double>(cluster.exactReference) * cluster.shadedPixels;
    totalApproximate += static_cast<double>(cluster.approximateReference) * cluster.shadedPixels;
    totalPixels += cluster.shadedPixels;
    stats.modeledMaxBoundDifference = std::max(
        stats.modeledMaxBoundDifference,
        std::abs(cluster.exactReference - cluster.approximateReference));
  }
  stats.averageLightsPerCluster =
      result.clusters.empty() ? 0.f : static_cast<float>(totalLights) / static_cast<float>(result.clusters.size());
  stats.splitCount = isAdaptive(config.technique) ? stats.activeClusters > tilesX * tilesY * zSlices
                                                        ? stats.activeClusters - tilesX * tilesY * zSlices
                                                        : 0
                                                  : 0;
  stats.clusterChurn = stats.activeClusters == 0 ? 0.f : static_cast<float>(stats.splitCount) / stats.activeClusters;
  if (totalPixels > 0.0) {
    double mse = weightedSquaredError / totalPixels;
    stats.modeledScalarBoundDifference = static_cast<float>(std::sqrt(mse));
    double peak = std::max(1.0, totalReference / totalPixels);
    stats.derivedScalarScore =
        mse <= 0.0 ? 99.f : static_cast<float>(20.0 * std::log10(peak / std::sqrt(mse)));
  }
  stats.gpu.clusterBuildMs = 0.00002 * static_cast<double>(result.clusters.size()) * std::log2(scene.lights.size() + 2.0);
  stats.gpu.lightingPassMs = static_cast<double>(stats.evaluatedLightSamples) / 900000000.0;
  stats.gpu.totalFrameMs = stats.gpu.clusterBuildMs + stats.gpu.lightingPassMs;
  return result;
}

const char* toString(ClusterEncoding encoding) {
  switch (encoding) {
    case ClusterEncoding::ExplicitList: return "explicit";
    case ClusterEncoding::DenseBitset: return "bitset";
    case ClusterEncoding::CompactRanges: return "ranges";
    case ClusterEncoding::Overflow: return "overflow";
  }
  return "unknown";
}

}  // namespace lve::beacon
