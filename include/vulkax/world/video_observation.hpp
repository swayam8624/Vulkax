#pragma once

#include "vulkax/capture/video_track.hpp"
#include "vulkax/world/world_ir.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vulkax::world {

struct VideoObservationImportOptions {
    std::optional<EntityId> entityId;
    EvidenceClass evidence{EvidenceClass::Derived};
    double baseStandardDeviationPixels{1.5};
    std::string observableId{"image_point"};
};

[[nodiscard]] inline std::string videoObservationId(
    const capture::VideoPointTrackSample& sample,
    std::string_view observableId = "image_point") {
    return "video/" + std::string(observableId) + "/frame-" + std::to_string(sample.frameIndex);
}

// Converts a tracked 2D image trajectory into typed WorldIR observations.
// Confidence scales uncertainty as sigma = baseSigma/sqrt(confidence), making
// low-confidence vision rows weaker without treating confidence as probability.
inline void appendVideoPointTrackObservations(
    WorldIR& world,
    const capture::VideoPointTrack& track,
    const VideoObservationImportOptions& options = {}) {
    if (track.samples.empty()) throw std::invalid_argument("cannot import an empty video point track");
    if (track.source.empty() || track.widthPixels == 0 || track.heightPixels == 0 ||
        !std::isfinite(track.nominalFps) || track.nominalFps <= 0.0) {
        throw std::invalid_argument("video point-track metadata is incomplete");
    }
    if (options.observableId.empty()) {
        throw std::invalid_argument("video observable id must be non-empty");
    }
    if (!std::isfinite(options.baseStandardDeviationPixels) ||
        options.baseStandardDeviationPixels <= 0.0) {
        throw std::invalid_argument("video observation base uncertainty must be finite and positive");
    }
    if (options.entityId.has_value() && world.findEntity(*options.entityId) == nullptr) {
        throw std::invalid_argument("video observation import references a missing WorldIR entity");
    }

    for (const auto& sample : track.samples) {
        const std::string id = videoObservationId(sample, options.observableId);
        if (world.findObservation(id) != nullptr) {
            throw std::invalid_argument(
                "video observation import would create a duplicate stable observation id: " + id);
        }
        ObservationRecord observation;
        observation.id = id;
        observation.observableId = options.observableId;
        observation.entityId = options.entityId;
        observation.timeSeconds = sample.timeSeconds;
        observation.values = {sample.xPixels, sample.yPixels};
        const double sigma = options.baseStandardDeviationPixels / std::sqrt(sample.confidence);
        observation.standardDeviation = {sigma, sigma};
        observation.evidence = options.evidence;
        observation.source = track.source;
        observation.role = sample.split == capture::VideoTrackSplit::Fit
                               ? ObservationRole::Fit
                               : ObservationRole::Validation;
        observation.space = ObservationSpace::ImagePixels;
        world.observations.push_back(std::move(observation));
    }
}

} // namespace vulkax::world
