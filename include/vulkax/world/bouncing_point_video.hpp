#pragma once

#include "vulkax/world/pinhole_camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vulkax::world {

struct BouncingPointState {
    math::Vec3 position{};
    math::Vec3 velocity{};
    std::size_t impacts{};
    bool settled{};
};

struct BouncingPointDynamics {
    math::Vec3 initialPosition{};
    math::Vec3 initialVelocity{};
    double gravityY{-9.80665};
    double restitution{0.8};
    double groundY{};
    double settleVelocity{1.0e-5};
    std::size_t maximumImpacts{256};
};

inline void validateBouncingPointDynamics(const BouncingPointDynamics& dynamics) {
    const auto finiteVec = [](math::Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!finiteVec(dynamics.initialPosition) || !finiteVec(dynamics.initialVelocity) ||
        !std::isfinite(dynamics.gravityY) || dynamics.gravityY >= 0.0 ||
        !std::isfinite(dynamics.restitution) || dynamics.restitution < 0.0 ||
        dynamics.restitution > 1.0 || !std::isfinite(dynamics.groundY) ||
        !std::isfinite(dynamics.settleVelocity) || dynamics.settleVelocity < 0.0 ||
        dynamics.maximumImpacts == 0 || dynamics.initialPosition.y < dynamics.groundY - 1.0e-10) {
        throw std::invalid_argument("invalid bouncing-point dynamics");
    }
}

namespace detail {

[[nodiscard]] inline double nextGroundImpactTime(
    double height,
    double velocityY,
    double gravityY) {
    if (height < -1.0e-10) return -1.0;
    if (height <= 1.0e-10 && velocityY <= 0.0) return 0.0;
    const double discriminant = velocityY * velocityY - 2.0 * gravityY * std::max(height, 0.0);
    if (discriminant < 0.0) return -1.0;
    const double root = (-velocityY - std::sqrt(discriminant)) / gravityY;
    return root >= 0.0 ? root : -1.0;
}

inline void advanceFreeFlight(BouncingPointState& state, double gravityY, double dt) {
    state.position.x += state.velocity.x * dt;
    state.position.y += state.velocity.y * dt + 0.5 * gravityY * dt * dt;
    state.position.z += state.velocity.z * dt;
    state.velocity.y += gravityY * dt;
}

} // namespace detail

// Event-driven point-mass free flight with a horizontal unilateral ground and
// Newton restitution. Impact times are solved analytically rather than snapped
// to a simulation timestep, so video residuals do not inherit an arbitrary
// contact-time quantization error from this reference model.
[[nodiscard]] inline BouncingPointState simulateBouncingPoint(
    const BouncingPointDynamics& dynamics,
    double timeSeconds) {
    validateBouncingPointDynamics(dynamics);
    if (!std::isfinite(timeSeconds) || timeSeconds < 0.0) {
        throw std::invalid_argument("bouncing-point sample time must be finite and non-negative");
    }

    BouncingPointState state{dynamics.initialPosition, dynamics.initialVelocity, 0U, false};
    double remaining = timeSeconds;
    constexpr double timeTolerance = 1.0e-12;

    while (remaining > timeTolerance) {
        const double height = state.position.y - dynamics.groundY;
        if (height <= 1.0e-10 && state.velocity.y <= 0.0) {
            state.position.y = dynamics.groundY;
            if (std::abs(state.velocity.y) <= dynamics.settleVelocity || dynamics.restitution == 0.0) {
                state.velocity.y = 0.0;
                state.position.x += state.velocity.x * remaining;
                state.position.z += state.velocity.z * remaining;
                state.settled = true;
                remaining = 0.0;
                break;
            }
            state.velocity.y = -dynamics.restitution * state.velocity.y;
            ++state.impacts;
            if (state.impacts > dynamics.maximumImpacts) {
                throw std::runtime_error("bouncing-point impact budget exceeded");
            }
            continue;
        }

        const double impactTime = detail::nextGroundImpactTime(
            height, state.velocity.y, dynamics.gravityY);
        if (impactTime < 0.0 || impactTime > remaining) {
            detail::advanceFreeFlight(state, dynamics.gravityY, remaining);
            remaining = 0.0;
            break;
        }

        detail::advanceFreeFlight(state, dynamics.gravityY, impactTime);
        state.position.y = dynamics.groundY;
        state.velocity.y = -dynamics.restitution * state.velocity.y;
        ++state.impacts;
        remaining -= impactTime;
        if (state.impacts > dynamics.maximumImpacts) {
            throw std::runtime_error("bouncing-point impact budget exceeded");
        }
        if (std::abs(state.velocity.y) <= dynamics.settleVelocity) {
            state.velocity.y = 0.0;
            state.position.x += state.velocity.x * remaining;
            state.position.z += state.velocity.z * remaining;
            state.settled = true;
            remaining = 0.0;
        }
    }
    if (state.position.y < dynamics.groundY &&
        dynamics.groundY - state.position.y <= 1.0e-9) {
        state.position.y = dynamics.groundY;
    }
    return state;
}

struct BouncingPointParameterBinding {
    ParameterAddress initialX;
    ParameterAddress initialY;
    ParameterAddress initialZ;
    ParameterAddress velocityX;
    ParameterAddress velocityY;
    ParameterAddress velocityZ;
    ParameterAddress gravityY;
    ParameterAddress restitution;
    ParameterAddress groundY;
    // Optional observation-time origin. Before this timestamp the point remains
    // at its initial pose; after it, physical time is t - releaseTime. This is
    // needed for real footage that contains a stationary pre-roll before release.
    std::optional<ParameterAddress> releaseTime;
};

[[nodiscard]] inline BouncingPointDynamics bouncingPointFromWorld(
    const WorldIR& world,
    const BouncingPointParameterBinding& binding,
    double settleVelocity = 1.0e-5,
    std::size_t maximumImpacts = 256) {
    const auto require = [&](const ParameterAddress& address, const char* label) {
        const auto value = world.parameterValue(address);
        if (!value.has_value() || !std::isfinite(*value)) {
            throw std::invalid_argument(std::string("WorldIR is missing bouncing-point parameter ") + label);
        }
        return *value;
    };
    BouncingPointDynamics dynamics;
    dynamics.initialPosition = {
        require(binding.initialX, "initial_x"),
        require(binding.initialY, "initial_y"),
        require(binding.initialZ, "initial_z"),
    };
    dynamics.initialVelocity = {
        require(binding.velocityX, "velocity_x"),
        require(binding.velocityY, "velocity_y"),
        require(binding.velocityZ, "velocity_z"),
    };
    dynamics.gravityY = require(binding.gravityY, "gravity_y");
    dynamics.restitution = require(binding.restitution, "restitution");
    dynamics.groundY = require(binding.groundY, "ground_y");
    dynamics.settleVelocity = settleVelocity;
    dynamics.maximumImpacts = maximumImpacts;
    validateBouncingPointDynamics(dynamics);
    return dynamics;
}

[[nodiscard]] inline double bouncingPointReleaseTime(
    const WorldIR& world,
    const BouncingPointParameterBinding& binding) {
    if (!binding.releaseTime.has_value()) return 0.0;
    const auto value = world.parameterValue(*binding.releaseTime);
    if (!value.has_value() || !std::isfinite(*value) || *value < 0.0) {
        throw std::invalid_argument("WorldIR is missing or has invalid bouncing-point release_time");
    }
    return *value;
}

[[nodiscard]] inline BouncingPointState simulateBouncingPointObservationTime(
    const BouncingPointDynamics& dynamics,
    double observationTimeSeconds,
    double releaseTimeSeconds) {
    if (!std::isfinite(observationTimeSeconds) || observationTimeSeconds < 0.0 ||
        !std::isfinite(releaseTimeSeconds) || releaseTimeSeconds < 0.0) {
        throw std::invalid_argument("invalid bouncing-point observation/release time");
    }
    if (observationTimeSeconds <= releaseTimeSeconds) {
        return {dynamics.initialPosition, math::Vec3{}, 0U, false};
    }
    return simulateBouncingPoint(dynamics, observationTimeSeconds - releaseTimeSeconds);
}

// First physical video forward model: a candidate WorldIR supplies both the
// pinhole camera and point-mass dynamics. The model evolves the 3D state at each
// imported video observation time, then projects it into exactly ImagePixels.
class BouncingPointVideoForwardModel {
  public:
    BouncingPointVideoForwardModel(
        std::uint32_t widthPixels,
        std::uint32_t heightPixels,
        PinholeCameraParameterBinding cameraBinding,
        BouncingPointParameterBinding dynamicsBinding,
        std::string observableId = "image_point",
        math::Vec3 cameraUp = {0.0, 1.0, 0.0})
        : widthPixels_(widthPixels),
          heightPixels_(heightPixels),
          cameraBinding_(std::move(cameraBinding)),
          dynamicsBinding_(std::move(dynamicsBinding)),
          observableId_(std::move(observableId)),
          cameraUp_(cameraUp) {
        if (widthPixels_ == 0 || heightPixels_ == 0 || observableId_.empty()) {
            throw std::invalid_argument("bouncing-point video forward model is incomplete");
        }
    }

    [[nodiscard]] std::vector<ObservationPrediction> operator()(const WorldIR& world) const {
        const auto camera = pinholeCameraFromWorld(
            world, widthPixels_, heightPixels_, cameraBinding_, cameraUp_);
        const auto dynamics = bouncingPointFromWorld(world, dynamicsBinding_);
        const double releaseTime = bouncingPointReleaseTime(world, dynamicsBinding_);
        std::vector<ObservationPrediction> predictions;
        for (const auto& observation : world.observations) {
            if (observation.observableId != observableId_) continue;
            if (observation.space != ObservationSpace::ImagePixels) {
                throw std::invalid_argument(
                    "bouncing-point video model requires ImagePixels observations");
            }
            const auto state = simulateBouncingPointObservationTime(
                dynamics, observation.timeSeconds, releaseTime);
            const auto pixel = projectWorldPoint(camera, state.position);
            if (!pixel.has_value()) {
                throw std::runtime_error(
                    "bouncing-point video state projects behind the camera: " + observation.id);
            }
            predictions.push_back({
                observation.id,
                {pixel->xPixels, pixel->yPixels},
                ObservationSpace::ImagePixels,
            });
        }
        if (predictions.empty()) {
            throw std::invalid_argument("bouncing-point video model found no matching observations");
        }
        return predictions;
    }

  private:
    std::uint32_t widthPixels_{};
    std::uint32_t heightPixels_{};
    PinholeCameraParameterBinding cameraBinding_;
    BouncingPointParameterBinding dynamicsBinding_;
    std::string observableId_;
    math::Vec3 cameraUp_{};
};

} // namespace vulkax::world
