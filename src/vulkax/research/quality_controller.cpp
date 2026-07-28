#include "vulkax/research/quality_controller.hpp"

#include <algorithm>
#include <stdexcept>

namespace vulkax::research {

QualityController::QualityController(QualityBudget budget) : budget_(budget) {
  if (budget_.targetFrameMilliseconds <= 0.0 || budget_.targetVisualError <= 0.0 ||
      budget_.minimumResolutionScale <= 0.0 || budget_.minimumResolutionScale > budget_.maximumResolutionScale ||
      budget_.minimumSamples == 0 || budget_.minimumSamples > budget_.maximumSamples) {
    throw std::invalid_argument("invalid Vulkax quality budget");
  }
  reset();
}

void QualityController::reset(QualityState initial) {
  state_.resolutionScale = std::clamp(
      initial.resolutionScale, budget_.minimumResolutionScale, budget_.maximumResolutionScale);
  state_.samplesPerPixel = std::clamp(
      initial.samplesPerPixel, budget_.minimumSamples, budget_.maximumSamples);
  state_.simulationSubsteps = std::max(1u, initial.simulationSubsteps);
  frameTimeEwma_ = 0.0;
  visualErrorEwma_ = 0.0;
  initialized_ = false;
  changeCount_ = 0;
}

void QualityController::update(const QualityMeasurements& measurements) {
  if (measurements.frameMilliseconds < 0.0 || measurements.visualError < 0.0 ||
      measurements.numericalError < 0.0) {
    throw std::invalid_argument("quality measurements must be non-negative");
  }
  constexpr double alpha = 0.15;
  if (!initialized_) {
    frameTimeEwma_ = measurements.frameMilliseconds;
    visualErrorEwma_ = measurements.visualError;
    initialized_ = true;
  } else {
    frameTimeEwma_ = alpha * measurements.frameMilliseconds + (1.0 - alpha) * frameTimeEwma_;
    visualErrorEwma_ = alpha * measurements.visualError + (1.0 - alpha) * visualErrorEwma_;
  }

  // Separate thresholds prevent per-frame oscillation at the two budgets.
  const bool timingPressure = frameTimeEwma_ > budget_.targetFrameMilliseconds * 1.08;
  const bool qualityPressure = visualErrorEwma_ > budget_.targetVisualError * 1.04 ||
                               measurements.numericalError > budget_.targetVisualError;
  const bool timingHeadroom = frameTimeEwma_ < budget_.targetFrameMilliseconds * 0.78;
  const bool qualityHeadroom = visualErrorEwma_ < budget_.targetVisualError * 0.70;
  const QualityState previous = state_;

  if (timingPressure && !qualityPressure) {
    state_.resolutionScale = std::max(budget_.minimumResolutionScale, state_.resolutionScale * 0.92);
    if (state_.samplesPerPixel > budget_.minimumSamples) --state_.samplesPerPixel;
  } else if (qualityPressure && !timingPressure) {
    state_.resolutionScale = std::min(budget_.maximumResolutionScale, state_.resolutionScale * 1.04);
    if (state_.samplesPerPixel < budget_.maximumSamples) ++state_.samplesPerPixel;
  } else if (timingHeadroom && qualityHeadroom) {
    state_.resolutionScale = std::min(budget_.maximumResolutionScale, state_.resolutionScale * 1.02);
  }
  if (previous.resolutionScale != state_.resolutionScale ||
      previous.samplesPerPixel != state_.samplesPerPixel ||
      previous.simulationSubsteps != state_.simulationSubsteps) {
    ++changeCount_;
  }
}

}  // namespace vulkax::research
