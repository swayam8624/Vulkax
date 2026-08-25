#include "vulkax/capture/deformable_bundle.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace vulkax::capture {
namespace {

struct MarkerTrajectoryState {
    bool hasInitialization{};
    bool hasDynamic{};
    std::optional<ObservationSplit> dynamicSplit;
};

[[nodiscard]] bool isInitializationTime(double time) noexcept {
    return std::abs(time) <= 1.0e-12;
}

} // namespace

void validateCapturedObservationTrajectoryContract(
    const CapturedDeformableDataset& dataset) {
    if (dataset.observations.empty())
        throw std::invalid_argument("captured trajectory contract requires observations");

    std::unordered_map<std::string, MarkerTrajectoryState> trajectories;
    for (const auto& observation : dataset.observations) {
        auto& state = trajectories[observation.markerId];
        if (isInitializationTime(observation.time)) {
            state.hasInitialization = true;
            continue;
        }

        state.hasDynamic = true;
        if (!state.dynamicSplit) {
            state.dynamicSplit = observation.split;
        } else if (*state.dynamicSplit != observation.split) {
            throw std::invalid_argument(
                "captured bundle marker '" + observation.markerId +
                "' changes fit/validation assignment across nonzero-time observations");
        }
    }

    for (const auto& [markerId, state] : trajectories) {
        if (!state.hasInitialization)
            throw std::invalid_argument(
                "captured bundle marker '" + markerId +
                "' has dynamic observations but no t=0 initialization observation");
        if (!state.hasDynamic)
            throw std::invalid_argument(
                "captured bundle marker '" + markerId +
                "' has a t=0 observation but no nonzero-time trajectory sample");
    }
}

} // namespace vulkax::capture
