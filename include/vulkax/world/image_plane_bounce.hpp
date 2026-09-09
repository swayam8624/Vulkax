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
    std::optional<std::size_t> firstBounceSample;
};

namespace detail {

[[nodiscard]] inline std::vector<double> fitPolynomial(
    const std::vector<capture::VideoPointTrackSample>& samples,
    std::size_t endInclusive,
    bool fitY,
    std::size_t degree) {
    if (endInclusive >= samples.size() || endInclusive + 1U < degree + 1U) {
        throw std::invalid_argument("insufficient video samples for polynomial seed fit");
    }
    numerics::DenseMatrix normal(degree + 1U, degree + 1U, 0.0);
    std::vector<double> rhs(degree + 1U, 0.0);
    for (std::size_t index = 0; index <= endInclusive; ++index) {
        const double t = samples[index].timeSeconds;
        const double value = fitY ? samples[index].yPixels : samples[index].xPixels;
        std::vector<double> basis(degree + 1U, 1.0);
        for (std::size_t power = 1; power <= degree; ++power) basis[power] = basis[power - 1U] * t;
        for (std::size_t row = 0; row <= degree; ++row) {
            rhs[row] += basis[row] * value;
            for (std::size_t column = 0; column <= degree; ++column) {
                normal(row, column) += basis[row] * basis[column];
            }
        }
    }
    return numerics::solveGaussian(std::move(normal), std::move(rhs));
}

[[nodiscard]] inline std::optional<std::size_t> firstImageBounce(
    const std::vector<capture::VideoPointTrackSample>& samples) {
    if (samples.size() < 5U) return std::nullopt;
    for (std::size_t index = 2; index + 2U < samples.size(); ++index) {
        const double dtBefore = samples[index].timeSeconds - samples[index - 1U].timeSeconds;
        const double dtAfter = samples[index + 1U].timeSeconds - samples[index].timeSeconds;
        if (dtBefore <= 0.0 || dtAfter <= 0.0) continue;
        const double incoming = (samples[index].yPixels - samples[index - 1U].yPixels) / dtBefore;
        const double outgoing = (samples[index + 1U].yPixels - samples[index].yPixels) / dtAfter;
        if (incoming > 0.0 && outgoing < 0.0 &&
            samples[index].yPixels >= samples[index - 1U].yPixels &&
            samples[index].yPixels >= samples[index + 1U].yPixels) {
            return index;
        }
    }
    return std::nullopt;
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
    std::size_t fitEnd = bounce.value_or(std::min<std::size_t>(track.samples.size() - 1U, 7U));
    fitEnd = std::max<std::size_t>(fitEnd, 2U);

    const auto xFit = detail::fitPolynomial(track.samples, fitEnd, false, 1U);
    const auto yFit = detail::fitPolynomial(track.samples, fitEnd, true, 2U);
    ImagePlaneBounceSeed seed;
    seed.initialXPixels = xFit[0];
    seed.velocityXPixelsPerSecond = xFit[1];
    seed.initialYPixels = yFit[0];
    seed.velocityYPixelsPerSecond = yFit[1];
    seed.accelerationYPixelsPerSecond2 = 2.0 * yFit[2];
    seed.firstBounceSample = bounce;

    double maximumY = track.samples.front().yPixels;
    for (const auto& sample : track.samples) maximumY = std::max(maximumY, sample.yPixels);
    seed.groundYPixels = bounce.has_value() ? track.samples[*bounce].yPixels : maximumY;

    if (bounce.has_value() && *bounce > 0U && *bounce + 1U < track.samples.size()) {
        const auto& before = track.samples[*bounce - 1U];
        const auto& impact = track.samples[*bounce];
        const auto& after = track.samples[*bounce + 1U];
        const double dtIn = impact.timeSeconds - before.timeSeconds;
        const double dtOut = after.timeSeconds - impact.timeSeconds;
        if (dtIn > 0.0 && dtOut > 0.0) {
            const double incomingAverage = (impact.yPixels - before.yPixels) / dtIn;
            const double outgoingAverage = (after.yPixels - impact.yPixels) / dtOut;
            const double incomingImpact = incomingAverage + 0.5 * seed.accelerationYPixelsPerSecond2 * dtIn;
            const double outgoingImpact = outgoingAverage - 0.5 * seed.accelerationYPixelsPerSecond2 * dtOut;
            if (incomingImpact > 1.0e-9 && outgoingImpact < 0.0) {
                seed.restitution = std::clamp(-outgoingImpact / incomingImpact, 0.05, 0.99);
            }
        }
    }
    if (!std::isfinite(seed.accelerationYPixelsPerSecond2) || seed.accelerationYPixelsPerSecond2 <= 0.0) {
        seed.accelerationYPixelsPerSecond2 = 100.0;
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
        std::vector<ObservationPrediction> predictions;
        for (const auto& observation : world.observations) {
            if (observation.observableId != observableId_) continue;
            if (observation.space != ObservationSpace::ImagePixels) {
                throw std::invalid_argument("image-plane bounce model requires ImagePixels observations");
            }
            const auto state = simulateBouncingPoint(dynamics, observation.timeSeconds);
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
