#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace lve::beacon {

struct CpuTimings {
  double frameMs = 0.0;
  double updateMs = 0.0;
  double commandRecordingMs = 0.0;
  double visibilityMs = 0.0;
  double clusterPreparationMs = 0.0;
  double bufferUpdateMs = 0.0;
  double acquirePresentMs = 0.0;
};

struct GpuTimings {
  double depthMs = 0.0;
  double clusterBuildMs = 0.0;
  double lightAssignmentMs = 0.0;
  double objectPassMs = 0.0;
  double lightingPassMs = 0.0;
  double debugPassMs = 0.0;
  double totalFrameMs = 0.0;
};

struct RenderStats {
  CpuTimings cpu{};
  GpuTimings gpu{};
  uint32_t drawCalls = 0;
  uint32_t visibleObjects = 0;
  uint32_t activeLights = 0;
  uint32_t activeClusters = 0;
  float averageLightsPerCluster = 0.f;
  uint32_t maximumLightsPerCluster = 0;
  uint64_t lightListBytes = 0;
  uint32_t explicitClusterCount = 0;
  uint32_t bitsetClusterCount = 0;
  uint32_t overflowCount = 0;
  uint32_t splitCount = 0;
  uint32_t mergeCount = 0;
  float clusterChurn = 0.f;
  uint64_t evaluatedLightSamples = 0;
  uint64_t prunedLightSamples = 0;
  float predictedErrorBound = 0.f;
  float modeledScalarBoundDifference = 0.f;
  float modeledMaxBoundDifference = 0.f;
  float derivedScalarScore = 0.f;
};

class CpuTimer {
 public:
  void begin();
  double elapsedMs() const;

 private:
  std::chrono::high_resolution_clock::time_point start{};
};

}  // namespace lve::beacon
