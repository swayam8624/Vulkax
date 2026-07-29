#pragma once

#include <cstdint>
#include <optional>

namespace vulkax::research {

struct QualityBudget {
  double targetFrameMilliseconds = 16.67;
  double targetVisualError = 0.01;
  double minimumResolutionScale = 0.35;
  double maximumResolutionScale = 1.0;
  uint32_t minimumSamples = 1;
  uint32_t maximumSamples = 16;
};

struct QualityState {
  double resolutionScale = 1.0;
  uint32_t samplesPerPixel = 1;
  uint32_t simulationSubsteps = 1;
};

struct QualityMeasurements {
  double frameMilliseconds = 0.0;
  double numericalError = 0.0;
  // An absent measurement must not be represented by zero: zero is a strong
  // claim of exact visual agreement and would otherwise authorize a quality
  // reduction under timing pressure.
  std::optional<double> visualError;
};

// Transparent EWMA controller for preview and offline-quality decisions. It
// deliberately reports measured errors separately from its internal budgets.
class QualityController {
 public:
  explicit QualityController(QualityBudget budget = {});

  void reset(QualityState initial = {});
  void update(const QualityMeasurements& measurements);
  [[nodiscard]] const QualityState& state() const { return state_; }
  [[nodiscard]] double frameTimeEwma() const { return frameTimeEwma_; }
  [[nodiscard]] std::optional<double> visualErrorEwma() const { return visualErrorEwma_; }
  [[nodiscard]] uint32_t changeCount() const { return changeCount_; }

 private:
  QualityBudget budget_;
  QualityState state_;
  double frameTimeEwma_ = 0.0;
  std::optional<double> visualErrorEwma_;
  bool initialized_ = false;
  uint32_t changeCount_ = 0;
};

}  // namespace vulkax::research
