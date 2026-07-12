#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lve::beacon {

enum class RenderTechnique {
  BaselineForward,
  SsboForward,
  InstancedForward,
  CpuClusteredFixed,
  FixedClusterCostModel,
  GpuClusteredFixed,
  AdaptiveClusteredExact,
  AdaptiveClusteredBounded,
  BeaconFull
};

enum class ScenePreset {
  Tutorial,
  RepeatedGeometry,
  UniqueObjects,
  DepthHeavy,
  OcclusionHeavy
};

enum class LightDistribution {
  Tutorial,
  Uniform,
  SingleHotspot,
  MultiHotspot,
  DepthStacked,
  MovingSwarm,
  LargeRadiusAdversarial,
  CameraAttached
};

struct BenchmarkConfig {
  ScenePreset scene = ScenePreset::Tutorial;
  LightDistribution lightDistribution = LightDistribution::Tutorial;
  uint32_t objectCount = 3;
  uint32_t lightCount = 6;
  uint32_t frameCount = 0;
  uint32_t warmupFrames = 120;
  uint32_t randomSeed = 1337;
  uint32_t width = 800;
  uint32_t height = 600;
  RenderTechnique technique = RenderTechnique::BaselineForward;
  float clusterBuildBudgetMs = 0.7f;
  float lightingBudgetMs = 3.0f;
  float imageErrorBudget = 0.005f;
  bool benchmark = false;
  bool verbose = true;
  bool showLightBillboards = true;
  bool captureReference = false;
  std::filesystem::path outputDirectory = "beacon_results";
};

BenchmarkConfig parseCommandLine(int argc, char** argv);
std::string toString(RenderTechnique technique);
std::string toString(ScenePreset scene);
std::string toString(LightDistribution distribution);
std::vector<RenderTechnique> implementedTechniques();

}  // namespace lve::beacon
