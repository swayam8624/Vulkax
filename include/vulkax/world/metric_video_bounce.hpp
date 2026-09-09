#pragma once

#include "vulkax/world/bouncing_point_video.hpp"
#include "vulkax/world/image_plane_bounce.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vulkax::world {

// Independent metric evidence tying an image-space length to a physical SI length.
// The image length may come from a ruler, calibration target, known object dimension,
// or another measured correspondence. The physical length remains the observation;
// pixels-per-metre is an inferred parameter, not something Vulkax pretends to have
// directly measured.
struct MetricLengthAnchor {
    std::string observationId;
    double imageLengthPixels{};
    double physicalLengthMeters{};
    double imageStandardDeviationPixels{};
    double physicalStandardDeviationMeters{};
    EvidenceClass evidence{EvidenceClass::Measured};
    std::string source;
    ObservationRole role{ObservationRole::Fit};
};

inline void validateMetricLengthAnchor(const MetricLengthAnchor& anchor) {
    if (anchor.observationId.empty() || anchor.source.empty() ||
        !std::isfinite(anchor.imageLengthPixels) || !(anchor.imageLengthPixels > 0.0) ||
        !std::isfinite(anchor.physicalLengthMeters) || !(anchor.physicalLengthMeters > 0.0) ||
        !std::isfinite(anchor.imageStandardDeviationPixels) || anchor.imageStandardDeviationPixels < 0.0 ||
        !std::isfinite(anchor.physicalStandardDeviationMeters) || !(anchor.physicalStandardDeviationMeters > 0.0)) {
        throw std::invalid_argument("invalid metric length anchor");
    }
}

[[nodiscard]] inline double metricAnchorPixelsPerMeter(const MetricLengthAnchor& anchor) {
    validateMetricLengthAnchor(anchor);
    return anchor.imageLengthPixels / anchor.physicalLengthMeters;
}

// First-order uncertainty expressed in metres around the anchor-ratio scale. Pixel
// extraction uncertainty and physical-length uncertainty remain separately declared
// inputs even though WorldIR's scalar residual stores their combined local effect.
[[nodiscard]] inline double metricAnchorLengthSigmaMeters(const MetricLengthAnchor& anchor) {
    validateMetricLengthAnchor(anchor);
    const double scale = metricAnchorPixelsPerMeter(anchor);
    return std::hypot(anchor.physicalStandardDeviationMeters,
                      anchor.imageStandardDeviationPixels / scale);
}

inline void appendMetricLengthAnchorObservation(
    WorldIR& world,
    const MetricLengthAnchor& anchor,
    std::string observableId = "metric_length_anchor") {
    validateMetricLengthAnchor(anchor);
    if (observableId.empty()) throw std::invalid_argument("metric anchor observable id is empty");
    if (world.findObservation(anchor.observationId) != nullptr) {
        throw std::invalid_argument("duplicate metric length anchor observation id");
    }
    ObservationRecord observation;
    observation.id = anchor.observationId;
    observation.observableId = std::move(observableId);
    observation.timeSeconds = 0.0;
    observation.values = {anchor.physicalLengthMeters};
    observation.standardDeviation = {metricAnchorLengthSigmaMeters(anchor)};
    observation.evidence = anchor.evidence;
    observation.source = anchor.source;
    observation.role = anchor.role;
    observation.space = ObservationSpace::PhysicalSI;
    world.observations.push_back(std::move(observation));
}

struct MetricBounceSeed {
    double pixelsPerMeter{};
    double imageOriginXPixels{};
    double groundYPixels{};
    double releaseTimeSeconds{};
    BouncingPointDynamics dynamics;
};

// Converts the non-metric image-plane initializer into an SI-valued seed once an
// independent length anchor is supplied. This bridge assumes locally orthographic,
// fronto-parallel image motion with negligible depth change. It is an initialization
// model, not a claim that arbitrary perspective footage has become metrically solved.
[[nodiscard]] inline MetricBounceSeed promoteImagePlaneBounceSeedToMetric(
    const ImagePlaneBounceSeed& imageSeed,
    const MetricLengthAnchor& anchor) {
    const double scale = metricAnchorPixelsPerMeter(anchor);
    if (!std::isfinite(imageSeed.initialXPixels) || !std::isfinite(imageSeed.initialYPixels) ||
        !std::isfinite(imageSeed.velocityXPixelsPerSecond) ||
        !std::isfinite(imageSeed.velocityYPixelsPerSecond) ||
        !std::isfinite(imageSeed.accelerationYPixelsPerSecond2) ||
        !(imageSeed.accelerationYPixelsPerSecond2 > 0.0) ||
        !std::isfinite(imageSeed.groundYPixels) ||
        !std::isfinite(imageSeed.restitution) || imageSeed.restitution < 0.0 || imageSeed.restitution > 1.0 ||
        !std::isfinite(imageSeed.releaseTimeSeconds) || imageSeed.releaseTimeSeconds < 0.0) {
        throw std::invalid_argument("invalid image-plane bounce seed for metric promotion");
    }
    const double heightMeters = (imageSeed.groundYPixels - imageSeed.initialYPixels) / scale;
    if (!(heightMeters >= 0.0) || !std::isfinite(heightMeters)) {
        throw std::invalid_argument("metric promotion places initial point below ground");
    }

    MetricBounceSeed result;
    result.pixelsPerMeter = scale;
    result.imageOriginXPixels = imageSeed.initialXPixels;
    result.groundYPixels = imageSeed.groundYPixels;
    result.releaseTimeSeconds = imageSeed.releaseTimeSeconds;
    result.dynamics.initialPosition = {0.0, heightMeters, 0.0};
    result.dynamics.initialVelocity = {
        imageSeed.velocityXPixelsPerSecond / scale,
        -imageSeed.velocityYPixelsPerSecond / scale,
        0.0,
    };
    result.dynamics.gravityY = -imageSeed.accelerationYPixelsPerSecond2 / scale;
    result.dynamics.restitution = imageSeed.restitution;
    result.dynamics.groundY = 0.0;
    validateBouncingPointDynamics(result.dynamics);
    return result;
}

struct MetricImagePlaneBounceParameterBinding {
    ParameterAddress pixelsPerMeter;
    ParameterAddress imageOriginXPixels;
    ParameterAddress groundYPixels;
    BouncingPointParameterBinding dynamics;
};

// SI physics observed through a locally orthographic image-plane model. Pixel-track
// residuals constrain dynamics while one or more independent MetricLengthAnchor
// observations break the monocular scale gauge. Perspective/full camera inference
// remains the job of the pinhole-camera path.
class MetricImagePlaneBounceForwardModel {
  public:
    MetricImagePlaneBounceForwardModel(
        MetricImagePlaneBounceParameterBinding binding,
        std::vector<MetricLengthAnchor> anchors = {},
        std::string imageObservableId = "image_point",
        std::string anchorObservableId = "metric_length_anchor")
        : binding_(std::move(binding)),
          anchors_(std::move(anchors)),
          imageObservableId_(std::move(imageObservableId)),
          anchorObservableId_(std::move(anchorObservableId)) {
        if (imageObservableId_.empty() || anchorObservableId_.empty()) {
            throw std::invalid_argument("metric image-plane observable id is empty");
        }
        for (const auto& anchor : anchors_) validateMetricLengthAnchor(anchor);
    }

    [[nodiscard]] std::vector<ObservationPrediction> operator()(const WorldIR& world) const {
        const auto require = [&](const ParameterAddress& address, const char* label) {
            const auto value = world.parameterValue(address);
            if (!value.has_value() || !std::isfinite(*value)) {
                throw std::invalid_argument(std::string("WorldIR is missing metric video parameter ") + label);
            }
            return *value;
        };
        const double scale = require(binding_.pixelsPerMeter, "pixels_per_metre");
        const double originX = require(binding_.imageOriginXPixels, "image_origin_x");
        const double groundPixels = require(binding_.groundYPixels, "ground_y_pixels");
        if (!(scale > 0.0)) throw std::invalid_argument("metric video scale must be positive");

        const auto dynamics = bouncingPointFromWorld(world, binding_.dynamics);
        const double releaseTime = bouncingPointReleaseTime(world, binding_.dynamics);
        std::vector<ObservationPrediction> predictions;
        for (const auto& observation : world.observations) {
            if (observation.observableId != imageObservableId_) continue;
            if (observation.space != ObservationSpace::ImagePixels) {
                throw std::invalid_argument("metric image-plane bounce model requires ImagePixels trajectory evidence");
            }
            const auto state = simulateBouncingPointObservationTime(
                dynamics, observation.timeSeconds, releaseTime);
            predictions.push_back({
                observation.id,
                {
                    originX + scale * state.position.x,
                    groundPixels - scale * (state.position.y - dynamics.groundY),
                },
                ObservationSpace::ImagePixels,
            });
        }

        for (const auto& anchor : anchors_) {
            const auto* observation = world.findObservation(anchor.observationId);
            if (observation == nullptr) continue;
            if (observation->observableId != anchorObservableId_ ||
                observation->space != ObservationSpace::PhysicalSI) {
                throw std::invalid_argument("metric length anchor WorldIR observation has incompatible type");
            }
            predictions.push_back({
                anchor.observationId,
                {anchor.imageLengthPixels / scale},
                ObservationSpace::PhysicalSI,
            });
        }

        if (predictions.empty()) {
            throw std::invalid_argument("metric image-plane bounce model found no matching observations");
        }
        return predictions;
    }

  private:
    MetricImagePlaneBounceParameterBinding binding_;
    std::vector<MetricLengthAnchor> anchors_;
    std::string imageObservableId_;
    std::string anchorObservableId_;
};

} // namespace vulkax::world
