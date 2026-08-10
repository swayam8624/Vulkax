#pragma once

#include <cstdint>
#include <optional>

namespace vulkax::research {

enum class QualityPolicy : uint32_t {
  Fixed = 0,
  ScreenSpaceOnly = 1,
  NumericalOnly = 2,
  EquationAware = 3,
};

struct QualityBudget {
  double targetFrameMilliseconds = 16.67;
  double targetVisualError = 0.01;
  double minimumResolutionScale = 0.35;
  double maximumResolutionScale = 1.0;
  uint32_t minimumSamples = 1;
  uint32_t maximumSamples = 16;
  std::optional<double> targetNumericalError;
  std::optional<uint64_t> targetGpuMemoryBytes;
};

struct QualityState {
  double resolutionScale = 1.0;
  uint32_t samplesPerPixel = 1;
  uint32_t simulationSubsteps = 1;
};

struct QualityMeasurements {
  double frameMilliseconds = 0.0;
  // Missing measurements are first-class. Zero means measured zero error and
  // is never reused as the sentinel for "we did not measure this domain".
  std::optional<double> numericalError;
  std::optional<double> visualError;
  std::optional<uint64_t> gpuMemoryBytes;
};

// Transparent EWMA controller for preview and offline-quality decisions. The
// selected policy determines which evidence is required before quality may be
// changed, enabling apples-to-apples fixed/screen/numerical/equation-aware
// experiments from the same runtime.
class QualityController {
 public:
  explicit QualityController(
      QualityBudget budget = {},
      QualityPolicy policy = QualityPolicy::EquationAware);

  void reset(QualityState initial = {});
  void update(const QualityMeasurements& measurements);
  void setPolicy(QualityPolicy policy);

  [[nodiscard]] const QualityState& state() const { return state_; }
  [[nodiscard]] QualityPolicy policy() const { return policy_; }
  [[nodiscard]] double frameTimeEwma() const { return frameTimeEwma_; }
  [[nodiscard]] std::optional<double> numericalErrorEwma() const { return numericalErrorEwma_; }
  [[nodiscard]] std::optional<double> visualErrorEwma() const { return visualErrorEwma_; }
  [[nodiscard]] std::optional<double> gpuMemoryBytesEwma() const { return gpuMemoryBytesEwma_; }
  [[nodiscard]] uint32_t changeCount() const { return changeCount_; }
  [[nodiscard]] bool hasRequiredMeasurements() const;

 private:
  [[nodiscard]] bool visualMeasurementRequired() const;
  [[nodiscard]] bool numericalMeasurementRequired() const;
  [[nodiscard]] bool underPressure() const;
  [[nodiscard]] bool comfortablyWithinTargets() const;
  void lowerQuality();
  void raiseQuality();

  QualityBudget budget_;
  QualityPolicy policy_ = QualityPolicy::EquationAware;
  QualityState state_;
  double frameTimeEwma_ = 0.0;
  std::optional<double> numericalErrorEwma_;
  std::optional<double> visualErrorEwma_;
  std::optional<double> gpuMemoryBytesEwma_;
  bool initialized_ = false;
  uint32_t stableFrames_ = 0;
  uint32_t changeCount_ = 0;
};

}  // namespace vulkax::research
