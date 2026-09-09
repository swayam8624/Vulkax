#pragma once

#include "vulkax/capture/video_track.hpp"
#include "vulkax/numerics/dense.hpp"
#include "vulkax/world/bouncing_point_video.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vulkax::world {

struct ImagePlaneBounceSeed {
    double initialXPixels{};
    double initialYPixels{};
    double velocityXPixelsPerSecond{};
    double velocityYPixelsPerSecond{};
    double accelerationYPixelsPerSecond2{};
    double restitution{0.7};
    double groundYPixels{};
    double releaseTimeSeconds{};
    std::optional<std::size_t> releaseSample;
    std::optional<std::size_t> firstBounceSample;
};

namespace detail {

[[nodiscard]] inline std::vector<double> fitPolynomial(
    const std::vector<capture::VideoPointTrackSample>& samples,
    std::size_t beginInclusive,
    std::size_t endInclusive,
    bool fitY,
    std::size_t degree,
    double timeOrigin) {
    if (beginInclusive > endInclusive || endInclusive >= samples.size() ||
        endInclusive - beginInclusive + 1U < degree + 1U) {
        throw std::invalid_argument("insufficient video samples for polynomial seed fit");
    }
    numerics::DenseMatrix normal(degree + 1U, degree + 1U, 0.0);
    std::vector<double> rhs(degree + 1U, 0.0);
    for (std::size_t index = beginInclusive; index <= endInclusive; ++index) {
        const double t = samples[index].timeSeconds - timeOrigin;
        const double value = fitY ? samples[index].yPixels : samples[index].xPixels;
        std::vector<double> basis(degree + 1U, 1.0);
        for (std::size_t power = 1; power <= degree; ++power) {
            basis[power] = basis[power - 1U] * t;
        }
        for (std::size_t row = 0; row <= degree; ++row) {
            rhs[row] += basis[row] * value;
            for (std::size_t column = 0; column <= degree; ++column) {
                normal(row, column) += basis[row] * basis[column];
            }
        }
    }
    return numerics::solveGaussian(std::move(normal), std::move(rhs));
}

[[nodiscard]] inline double medianSampleInterval(
    const std::vector<capture::VideoPointTrackSample>& samples) {
    if (samples.size() < 2U) return 0.0;
    std::vector<double> intervals;
    intervals.reserve(samples.size() - 1U);
    for (std::size_t index = 1; index < samples.size(); ++index) {
        const double dt = samples[index].timeSeconds - samples[index - 1U].timeSeconds;
        if (dt > 0.0 && std::isfinite(dt)) intervals.push_back(dt);
    }
    if (intervals.empty()) return 0.0;
    const auto middle = intervals.begin() + static_cast<std::ptrdiff_t>(intervals.size() / 2U);
    std::nth_element(intervals.begin(), middle, intervals.end());
    return *middle;
}

[[nodiscard]] inline std::pair<double, double> verticalExtent(
    const std::vector<capture::VideoPointTrackSample>& samples) {
    double minimum = samples.front().yPixels;
    double maximum = minimum;
    for (const auto& sample : samples) {
        minimum = std::min(minimum, sample.yPixels);
        maximum = std::max(maximum, sample.yPixels);
    }
    return {minimum, maximum};
}

// A bounce in image coordinates is a local maximum of y because image y grows
// downward. Requiring the candidate to live near the lower envelope prevents
// tiny jitter in a stationary pre-roll from masquerading as an impact.
[[nodiscard]] inline std::optional<std::size_t> firstImageBounce(
    const std::vector<capture::VideoPointTrackSample>& samples) {
    if (samples.size() < 7U) return std::nullopt;
    const auto [minimumY, maximumY] = verticalExtent(samples);
    const double verticalRange = maximumY - minimumY;
    const double medianDt = medianSampleInterval(samples);
    if (!(verticalRange > 1.0) || !(medianDt > 0.0)) return std::nullopt;

    constexpr std::size_t window = 2U;
    const double lowerEnvelope = minimumY + 0.65 * verticalRange;
    const double minimumProminence = std::max(0.5, 0.02 * verticalRange);
    const double minimumSpeed = std::max(2.0, 0.025 * verticalRange / medianDt);

    for (std::size_t index = window; index + window < samples.size(); ++index) {
        double beforeY = 0.0;
        double beforeTime = 0.0;
        double afterY = 0.0;
        double afterTime = 0.0;
        for (std::size_t offset = 1; offset <= window; ++offset) {
            beforeY += samples[index - offset].yPixels;
            beforeTime += samples[index - offset].timeSeconds;
            afterY += samples[index + offset].yPixels;
            afterTime += samples[index + offset].timeSeconds;
        }
        beforeY /= static_cast<double>(window);
        beforeTime /= static_cast<double>(window);
        afterY /= static_cast<double>(window);
        afterTime /= static_cast<double>(window);
        const auto& candidate = samples[index];
        const double incomingDt = candidate.timeSeconds - beforeTime;
        const double outgoingDt = afterTime - candidate.timeSeconds;
        if (!(incomingDt > 0.0) || !(outgoingDt > 0.0)) continue;
        const double incoming = (candidate.yPixels - beforeY) / incomingDt;
        const double outgoing = (afterY - candidate.yPixels) / outgoingDt;
        const double prominence = candidate.yPixels - std::max(beforeY, afterY);
        if (candidate.yPixels >= lowerEnvelope && prominence >= minimumProminence &&
            incoming >= minimumSpeed && outgoing <= -minimumSpeed) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline std::size_t imageMotionReleaseSample(
    const std::vector<capture::VideoPointTrackSample>& samples,
    std::size_t endExclusive) {
    if (endExclusive < 2U) return 0U;
    const auto [minimumY, maximumY] = verticalExtent(samples);
    const double verticalRange = std::max(maximumY - minimumY, 1.0);
    const double medianDt = std::max(medianSampleInterval(samples), 1.0e-9);
    const double speedThreshold = std::max(2.0, 0.02 * verticalRange / medianDt);

    for (std::size_t index = 1; index < endExclusive; ++index) {
        const double dt = samples[index].timeSeconds - samples[index - 1U].timeSeconds;
        if (!(dt > 0.0)) continue;
        const double dx = samples[index].xPixels - samples[index - 1U].xPixels;
        const double dy = samples[index].yPixels - samples[index - 1U].yPixels;
        const double speed = std::hypot(dx, dy) / dt;
        if (speed < speedThreshold) continue;

        std::size_t supporting = 0U;
        for (std::size_t look = index; look < std::min(endExclusive, index + 3U); ++look) {
            if (look == 0U) continue;
            const double localDt = samples[look].timeSeconds - samples[look - 1U].timeSeconds;
            if (!(localDt > 0.0)) continue;
            const double localDx = samples[look].xPixels - samples[look - 1U].xPixels;
            const double localDy = samples[look].yPixels - samples[look - 1U].yPixels;
            if (std::hypot(localDx, localDy) / localDt >= 0.5 * speedThreshold) ++supporting;
        }
        if (supporting >= 2U) return index - 1U;
    }
    return 0U;
}

} // namespace detail

// Produces a kinematic initializer directly in image coordinates. This is a
// model-proxy initializer, not a metric gravity estimate: pixels/s^2 become a
// physical acceleration only after camera geometry and scene scale are known.
[[nodiscard]] inline ImagePlaneBounceSeed estimateImagePlaneBounceSeed(
    const capture::VideoPointTrack& track) {
    if (track.samples.size() < 3U) {
        throw std::invalid_argument("image-plane bounce seed needs at least three video samples");
    }
    const auto bounce = detail::firstImageBounce(track.samples);
    const std::size_t eventEnd = bounce.value_or(
        std::min<std::size_t>(track.samples.size() - 1U, 10U));
    std::size_t release = detail::imageMotionReleaseSample(track.samples, eventEnd + 1U);
    if (eventEnd - release + 1U < 3U) {
        release = eventEnd >= 2U ? eventEnd - 2U : 0U;
    }
    // Very long pre-impact tracks can make the normal equations needlessly ill
    // conditioned. Keep the most recent free-flight evidence while retaining a
    // physically meaningful observation-time origin.
    const std::size_t fitBegin = eventEnd - release + 1U > 32U ? eventEnd - 31U : release;
    const double releaseTime = track.samples[release].timeSeconds;

    const auto xFit = detail::fitPolynomial(
        track.samples, fitBegin, eventEnd, false, 1U, releaseTime);
    const auto yFit = detail::fitPolynomial(
        track.samples, fitBegin, eventEnd, true, 2U, releaseTime);

    ImagePlaneBounceSeed seed;
    seed.initialXPixels = xFit[0];
    seed.velocityXPixelsPerSecond = xFit[1];
    seed.initialYPixels = yFit[0];
    seed.velocityYPixelsPerSecond = yFit[1];
    seed.accelerationYPixelsPerSecond2 = 2.0 * yFit[2];
    seed.releaseTimeSeconds = releaseTime;
    seed.releaseSample = release;
    seed.firstBounceSample = bounce;

    const auto [minimumY, maximumY] = detail::verticalExtent(track.samples);
    (void)minimumY;
    seed.groundYPixels = bounce.has_value() ? track.samples[*bounce].yPixels : maximumY;

    if (bounce.has_value() && *bounce >= 3U && *bounce + 3U < track.samples.size()) {
        const std::size_t beforeBegin = std::max(release, *bounce - 6U);
        const std::size_t beforeEnd = *bounce;
        const std::size_t afterBegin = *bounce;
        const std::size_t afterEnd = std::min(track.samples.size() - 1U, *bounce + 6U);
        if (beforeEnd - beforeBegin + 1U >= 3U && afterEnd - afterBegin + 1U >= 3U) {
            const double impactTime = track.samples[*bounce].timeSeconds;
            const auto beforeFit = detail::fitPolynomial(
                track.samples, beforeBegin, beforeEnd, true, 2U, impactTime);
            const auto afterFit = detail::fitPolynomial(
                track.samples, afterBegin, afterEnd, true, 2U, impactTime);
            const double incomingImpact = beforeFit[1];
            const double outgoingImpact = afterFit[1];
            if (incomingImpact > 1.0e-6 && outgoingImpact < -1.0e-6) {
                seed.restitution = std::clamp(-outgoingImpact / incomingImpact, 0.05, 0.99);
            }
        }
    }
    if (!std::isfinite(seed.accelerationYPixelsPerSecond2) ||
        seed.accelerationYPixelsPerSecond2 <= 0.0) {
        const double duration = std::max(
            track.samples[eventEnd].timeSeconds - track.samples[release].timeSeconds, 1.0e-3);
        seed.accelerationYPixelsPerSecond2 =
            std::max(1.0, 2.0 * std::max(seed.groundYPixels - seed.initialYPixels, 1.0) /
                              (duration * duration));
    }
    return seed;
}

struct ImagePlaneBounceParameterBinding {
    ParameterAddress initialX;
    ParameterAddress initialY;
    ParameterAddress velocityX;
    ParameterAddress velocityY;
    ParameterAddress accelerationY;
    ParameterAddress restitution;
    ParameterAddress groundY;
    std::optional<ParameterAddress> releaseTime;
};

[[nodiscard]] inline BouncingPointDynamics imagePlaneBounceFromWorld(
    const WorldIR& world,
    const ImagePlaneBounceParameterBinding& binding) {
    const auto require = [&](const ParameterAddress& address, const char* label) {
        const auto value = world.parameterValue(address);
        if (!value.has_value() || !std::isfinite(*value)) {
            throw std::invalid_argument(std::string("WorldIR is missing image-plane bounce parameter ") + label);
        }
        return *value;
    };
    const double ground = require(binding.groundY, "ground_y");
    const double initialY = require(binding.initialY, "initial_y");
    const double acceleration = require(binding.accelerationY, "acceleration_y");
    BouncingPointDynamics dynamics;
    dynamics.initialPosition = {
        require(binding.initialX, "initial_x"), ground - initialY, 0.0};
    dynamics.initialVelocity = {
        require(binding.velocityX, "velocity_x"), -require(binding.velocityY, "velocity_y"), 0.0};
    dynamics.gravityY = -acceleration;
    dynamics.restitution = require(binding.restitution, "restitution");
    dynamics.groundY = 0.0;
    validateBouncingPointDynamics(dynamics);
    return dynamics;
}

[[nodiscard]] inline double imagePlaneReleaseTime(
    const WorldIR& world,
    const ImagePlaneBounceParameterBinding& binding) {
    if (!binding.releaseTime.has_value()) return 0.0;
    const auto value = world.parameterValue(*binding.releaseTime);
    if (!value.has_value() || !std::isfinite(*value) || *value < 0.0) {
        throw std::invalid_argument("WorldIR is missing or has invalid image-plane release_time");
    }
    return *value;
}

// Pre-calibration video model. It predicts only image-plane kinematics, so the
// acceleration parameter remains pixels/s^2. Its purpose is robust initialization,
// event detection and held-out checking before metric camera/scene calibration.
class ImagePlaneBounceForwardModel {
  public:
    explicit ImagePlaneBounceForwardModel(
        ImagePlaneBounceParameterBinding binding,
        std::string observableId = "image_point")
        : binding_(std::move(binding)), observableId_(std::move(observableId)) {
        if (observableId_.empty()) throw std::invalid_argument("image-plane bounce observable id is empty");
    }

    [[nodiscard]] std::vector<ObservationPrediction> operator()(const WorldIR& world) const {
        const auto dynamics = imagePlaneBounceFromWorld(world, binding_);
        const double groundPixels = *world.parameterValue(binding_.groundY);
        const double releaseTime = imagePlaneReleaseTime(world, binding_);
        std::vector<ObservationPrediction> predictions;
        for (const auto& observation : world.observations) {
            if (observation.observableId != observableId_) continue;
            if (observation.space != ObservationSpace::ImagePixels) {
                throw std::invalid_argument("image-plane bounce model requires ImagePixels observations");
            }
            const auto state = simulateBouncingPointObservationTime(
                dynamics, observation.timeSeconds, releaseTime);
            predictions.push_back({
                observation.id,
                {state.position.x, groundPixels - state.position.y},
                ObservationSpace::ImagePixels,
            });
        }
        if (predictions.empty()) {
            throw std::invalid_argument("image-plane bounce model found no matching observations");
        }
        return predictions;
    }

  private:
    ImagePlaneBounceParameterBinding binding_;
    std::string observableId_;
};

} // namespace vulkax::world
