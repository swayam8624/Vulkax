#pragma once

#include "vulkax/core/math.hpp"
#include "vulkax/world/reality_loop.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vulkax::world {

struct PinholeCameraModel {
    std::uint32_t widthPixels{};
    std::uint32_t heightPixels{};
    double fxPixels{};
    double fyPixels{};
    double cxPixels{};
    double cyPixels{};
    math::Vec3 position{};
    math::Vec3 target{};
    math::Vec3 up{0.0, 1.0, 0.0};
    double nearPlane{1.0e-6};
};

struct PixelProjection {
    double xPixels{};
    double yPixels{};
    double depth{};
};

inline void validatePinholeCamera(const PinholeCameraModel& camera) {
    const auto finiteVec = [](math::Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (camera.widthPixels == 0 || camera.heightPixels == 0 ||
        !std::isfinite(camera.fxPixels) || !std::isfinite(camera.fyPixels) ||
        !std::isfinite(camera.cxPixels) || !std::isfinite(camera.cyPixels) ||
        camera.fxPixels <= 0.0 || camera.fyPixels <= 0.0 ||
        !std::isfinite(camera.nearPlane) || camera.nearPlane <= 0.0 ||
        !finiteVec(camera.position) || !finiteVec(camera.target) || !finiteVec(camera.up)) {
        throw std::invalid_argument("invalid pinhole camera parameters");
    }
    const math::Vec3 forward = math::normalized(camera.target - camera.position);
    if (math::length(forward) <= 1.0e-12) {
        throw std::invalid_argument("pinhole camera position and target coincide");
    }
    const math::Vec3 right = math::normalized(math::cross(forward, camera.up));
    if (math::length(right) <= 1.0e-12) {
        throw std::invalid_argument("pinhole camera up is parallel to view direction");
    }
}

// Projects a world-space point into pixel-center coordinates. Camera x points
// along cross(forward, up); camera y follows the supplied up vector, while image
// y increases downward. The returned depth is forward-camera distance.
[[nodiscard]] inline std::optional<PixelProjection> projectWorldPoint(
    const PinholeCameraModel& camera,
    math::Vec3 worldPoint) {
    validatePinholeCamera(camera);
    if (!std::isfinite(worldPoint.x) || !std::isfinite(worldPoint.y) || !std::isfinite(worldPoint.z)) {
        throw std::invalid_argument("cannot project a non-finite world point");
    }
    const math::Vec3 forward = math::normalized(camera.target - camera.position);
    const math::Vec3 right = math::normalized(math::cross(forward, camera.up));
    const math::Vec3 cameraUp = math::cross(right, forward);
    const math::Vec3 delta = worldPoint - camera.position;
    const double depth = math::dot(delta, forward);
    if (depth <= camera.nearPlane) return std::nullopt;
    const double cameraX = math::dot(delta, right);
    const double cameraY = math::dot(delta, cameraUp);
    return PixelProjection{
        camera.fxPixels * cameraX / depth + camera.cxPixels,
        camera.cyPixels - camera.fyPixels * cameraY / depth,
        depth,
    };
}

struct PinholeCameraParameterBinding {
    ParameterAddress fx;
    ParameterAddress fy;
    ParameterAddress cx;
    ParameterAddress cy;
    ParameterAddress positionX;
    ParameterAddress positionY;
    ParameterAddress positionZ;
    ParameterAddress targetX;
    ParameterAddress targetY;
    ParameterAddress targetZ;
};

[[nodiscard]] inline PinholeCameraModel pinholeCameraFromWorld(
    const WorldIR& world,
    std::uint32_t widthPixels,
    std::uint32_t heightPixels,
    const PinholeCameraParameterBinding& binding,
    math::Vec3 up = {0.0, 1.0, 0.0},
    double nearPlane = 1.0e-6) {
    const auto require = [&](const ParameterAddress& address, const char* label) {
        const auto value = world.parameterValue(address);
        if (!value.has_value() || !std::isfinite(*value)) {
            throw std::invalid_argument(std::string("WorldIR is missing pinhole camera parameter ") + label);
        }
        return *value;
    };
    PinholeCameraModel camera;
    camera.widthPixels = widthPixels;
    camera.heightPixels = heightPixels;
    camera.fxPixels = require(binding.fx, "fx");
    camera.fyPixels = require(binding.fy, "fy");
    camera.cxPixels = require(binding.cx, "cx");
    camera.cyPixels = require(binding.cy, "cy");
    camera.position = {
        require(binding.positionX, "position_x"),
        require(binding.positionY, "position_y"),
        require(binding.positionZ, "position_z"),
    };
    camera.target = {
        require(binding.targetX, "target_x"),
        require(binding.targetY, "target_y"),
        require(binding.targetZ, "target_z"),
    };
    camera.up = up;
    camera.nearPlane = nearPlane;
    validatePinholeCamera(camera);
    return camera;
}

struct WorldPointProjectionSample {
    std::string observationId;
    math::Vec3 worldPosition{};
};

// Calibration/reference forward model for known 3D point correspondences. It is
// deliberately separate from the showcase renderer: every invocation reads the
// candidate camera state from WorldIR, projects the supplied world points, and
// emits explicitly ImagePixels predictions for the generic reality loop.
class PinholeProjectionForwardModel {
  public:
    PinholeProjectionForwardModel(
        std::uint32_t widthPixels,
        std::uint32_t heightPixels,
        PinholeCameraParameterBinding binding,
        std::vector<WorldPointProjectionSample> samples,
        math::Vec3 up = {0.0, 1.0, 0.0},
        double nearPlane = 1.0e-6)
        : widthPixels_(widthPixels),
          heightPixels_(heightPixels),
          binding_(std::move(binding)),
          samples_(std::move(samples)),
          up_(up),
          nearPlane_(nearPlane) {
        if (widthPixels_ == 0 || heightPixels_ == 0 || samples_.empty()) {
            throw std::invalid_argument("pinhole projection forward model is incomplete");
        }
        for (const auto& sample : samples_) {
            if (sample.observationId.empty()) {
                throw std::invalid_argument("pinhole projection sample has empty observation id");
            }
        }
    }

    [[nodiscard]] std::vector<ObservationPrediction> operator()(const WorldIR& world) const {
        const auto camera = pinholeCameraFromWorld(
            world, widthPixels_, heightPixels_, binding_, up_, nearPlane_);
        std::vector<ObservationPrediction> predictions;
        predictions.reserve(samples_.size());
        for (const auto& sample : samples_) {
            const auto projected = projectWorldPoint(camera, sample.worldPosition);
            if (!projected.has_value()) {
                throw std::runtime_error(
                    "pinhole forward model point lies on/behind the camera near plane: " +
                    sample.observationId);
            }
            predictions.push_back({
                sample.observationId,
                {projected->xPixels, projected->yPixels},
                ObservationSpace::ImagePixels,
            });
        }
        return predictions;
    }

  private:
    std::uint32_t widthPixels_{};
    std::uint32_t heightPixels_{};
    PinholeCameraParameterBinding binding_;
    std::vector<WorldPointProjectionSample> samples_;
    math::Vec3 up_{};
    double nearPlane_{};
};

} // namespace vulkax::world
