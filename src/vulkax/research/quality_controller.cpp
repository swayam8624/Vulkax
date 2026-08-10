#include "vulkax/research/quality_controller.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::research {
namespace {

constexpr double kAlpha = 0.15;

template <typename T>
void updateOptionalEwma(std::optional<double>& ewma, const std::optional<T>& measurement) {
  if (!measurement) return;
  const double value = static_cast<double>(*measurement);
  ewma = ewma ? kAlpha * value + (1.0 - kAlpha) * *ewma : value;
}

}  // namespace

QualityController::QualityController(QualityBudget budget, QualityPolicy policy)
    : budget_(budget), policy_(policy) {
  if (budget_.targetFrameMilliseconds <= 0.0 || budget_.targetVisualError <= 0.0 ||
      budget_.minimumResolutionScale <= 0.0 ||
      budget_.minimumResolutionScale > budget_.maximumResolutionScale ||
      budget_.minimumSamples == 0 || budget_.minimumSamples > budget_.maximumSamples ||
      (budget_.targetNumericalError && *budget_.targetNumericalError <= 0.0) ||
      (budget_.targetGpuMemoryBytes && *budget_.targetGpuMemoryBytes == 0)) {
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
  numericalErrorEwma_.reset();
  visualErrorEwma_.reset();
  gpuMemoryBytesEwma_.reset();
  initialized_ = false;
  stableFrames_ = 0;
  changeCount_ = 0;
}

void QualityController::setPolicy(QualityPolicy policy) {
  if (policy_ == policy) return;
  policy_ = policy;
  stableFrames_ = 0;
}

bool QualityController::visualMeasurementRequired() const {
  return policy_ == QualityPolicy::ScreenSpaceOnly || policy_ == QualityPolicy::EquationAware;
}

bool QualityController::numericalMeasurementRequired() const {
  return policy_ == QualityPolicy::NumericalOnly ||
         (policy_ == QualityPolicy::EquationAware && budget_.targetNumericalError.has_value());
}

bool QualityController::hasRequiredMeasurements() const {
  if (policy_ == QualityPolicy::Fixed) return true;
  if (visualMeasurementRequired() && !visualErrorEwma_) return false;
  if (numericalMeasurementRequired() && !numericalErrorEwma_) return false;
  return true;
}

bool QualityController::underPressure() const {
  const bool timing = frameTimeEwma_ > budget_.targetFrameMilliseconds * 1.08;
  const bool visual = visualMeasurementRequired() && visualErrorEwma_ &&
                      *visualErrorEwma_ > budget_.targetVisualError * 1.04;
  const double numericalTarget = budget_.targetNumericalError.value_or(budget_.targetVisualError);
  const bool numerical = numericalMeasurementRequired() && numericalErrorEwma_ &&
                         *numericalErrorEwma_ > numericalTarget * 1.04;
  const bool memory = budget_.targetGpuMemoryBytes && gpuMemoryBytesEwma_ &&
                      *gpuMemoryBytesEwma_ > static_cast<double>(*budget_.targetGpuMemoryBytes);
  return timing || visual || numerical || memory;
}

bool QualityController::comfortablyWithinTargets() const {
  const bool timing = frameTimeEwma_ < budget_.targetFrameMilliseconds * 0.78;
  const bool visual = !visualMeasurementRequired() ||
                      (visualErrorEwma_ && *visualErrorEwma_ < budget_.targetVisualError * 0.70);
  const double numericalTarget = budget_.targetNumericalError.value_or(budget_.targetVisualError);
  const bool numerical = !numericalMeasurementRequired() ||
                         (numericalErrorEwma_ && *numericalErrorEwma_ < numericalTarget * 0.70);
  const bool memory = !budget_.targetGpuMemoryBytes || !gpuMemoryBytesEwma_ ||
                      *gpuMemoryBytesEwma_ < static_cast<double>(*budget_.targetGpuMemoryBytes) * 0.85;
  return timing && visual && numerical && memory;
}

void QualityController::lowerQuality() {
  state_.resolutionScale = std::max(
      budget_.minimumResolutionScale, state_.resolutionScale * 0.92);
  if (state_.samplesPerPixel > budget_.minimumSamples) --state_.samplesPerPixel;
  if (state_.simulationSubsteps > 1) --state_.simulationSubsteps;
}

void QualityController::raiseQuality() {
  state_.resolutionScale = std::min(
      budget_.maximumResolutionScale, state_.resolutionScale * 1.04);
  if (state_.samplesPerPixel < budget_.maximumSamples) ++state_.samplesPerPixel;
  if (numericalMeasurementRequired() && numericalErrorEwma_) ++state_.simulationSubsteps;
}

void QualityController::update(const QualityMeasurements& measurements) {
  const auto invalidOptional = [](const auto& value) {
    return value && (!std::isfinite(static_cast<double>(*value)) || static_cast<double>(*value) < 0.0);
  };
  if (!std::isfinite(measurements.frameMilliseconds) || measurements.frameMilliseconds < 0.0 ||
      invalidOptional(measurements.numericalError) || invalidOptional(measurements.visualError)) {
    throw std::invalid_argument("quality measurements must be finite and non-negative");
  }

  if (!initialized_) {
    frameTimeEwma_ = measurements.frameMilliseconds;
    initialized_ = true;
  } else {
    frameTimeEwma_ = kAlpha * measurements.frameMilliseconds +
                     (1.0 - kAlpha) * frameTimeEwma_;
  }
  updateOptionalEwma(numericalErrorEwma_, measurements.numericalError);
  updateOptionalEwma(visualErrorEwma_, measurements.visualError);
  updateOptionalEwma(gpuMemoryBytesEwma_, measurements.gpuMemoryBytes);

  if (policy_ == QualityPolicy::Fixed || !hasRequiredMeasurements()) {
    stableFrames_ = 0;
    return;
  }

  const QualityState previous = state_;
  if (underPressure()) {
    // Time/memory pressure can only trade quality downward when the selected
    // policy has real quality measurements proving that tradeoff is observable.
    const bool errorPressure =
        (visualMeasurementRequired() && visualErrorEwma_ &&
         *visualErrorEwma_ > budget_.targetVisualError * 1.04) ||
        (numericalMeasurementRequired() && numericalErrorEwma_ &&
         *numericalErrorEwma_ > budget_.targetNumericalError.value_or(budget_.targetVisualError) * 1.04);
    if (!errorPressure) lowerQuality();
    else raiseQuality();
    stableFrames_ = 0;
  } else if (comfortablyWithinTargets()) {
    ++stableFrames_;
    if (stableFrames_ >= 4) {
      raiseQuality();
      stableFrames_ = 0;
    }
  } else {
    stableFrames_ = 0;
  }

  if (previous.resolutionScale != state_.resolutionScale ||
      previous.samplesPerPixel != state_.samplesPerPixel ||
      previous.simulationSubsteps != state_.simulationSubsteps) {
    ++changeCount_;
  }
}

}  // namespace vulkax::research
