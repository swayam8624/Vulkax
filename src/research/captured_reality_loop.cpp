#include "vulkax/research/captured_reality_loop.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vulkax::research {
namespace {

std::string observationId(std::string_view markerId,
                          std::uint64_t particleId,
                          double time,
                          capture::ObservationSplit split) {
    std::ostringstream stream;
    stream << "captured/" << markerId << "/particle-" << particleId << "/time-"
           << std::hexfloat << time << "/" << capture::toString(split);
    return stream.str();
}

double requireParameter(const world::WorldIR& candidate,
                        const world::ParameterAddress& address,
                        const char* label) {
    const auto value = candidate.parameterValue(address);
    if (!value.has_value() || !std::isfinite(*value)) {
        throw std::invalid_argument(std::string("captured MPM forward model is missing ") + label);
    }
    return *value;
}

} // namespace

std::string capturedObservationId(const capture::CapturedMarkerObservation& observation) {
    return observationId(observation.markerId, observation.particleId, observation.time, observation.split);
}

void appendCapturedObservations(world::WorldIR& world,
                                const capture::CapturedDeformableDataset& dataset,
                                const CapturedObservationImportOptions& options) {
    if (dataset.observations.empty()) {
        throw std::invalid_argument("cannot import an empty captured observation set");
    }
    if (options.entityId.has_value() && world.findEntity(*options.entityId) == nullptr) {
        throw std::invalid_argument("captured observation import references a missing WorldIR entity");
    }
    if (options.isotropicPositionStandardDeviation.has_value() &&
        (!std::isfinite(*options.isotropicPositionStandardDeviation) ||
         *options.isotropicPositionStandardDeviation <= 0.0)) {
        throw std::invalid_argument("captured observation uncertainty must be finite and positive");
    }

    for (const auto& observation : dataset.observations) {
        const std::string id = capturedObservationId(observation);
        if (world.findObservation(id) != nullptr) {
            throw std::invalid_argument("captured observation import would create a duplicate stable observation id: " + id);
        }
        world::ObservationRecord record;
        record.id = id;
        record.observableId = "particle_position";
        record.entityId = options.entityId;
        record.timeSeconds = observation.time;
        record.values = {observation.position.x, observation.position.y, observation.position.z};
        if (options.isotropicPositionStandardDeviation.has_value()) {
            record.standardDeviation.assign(3U, *options.isotropicPositionStandardDeviation);
        }
        record.evidence = options.evidence;
        record.source = options.source;
        record.space = world::ObservationSpace::PhysicalSI;
        if (observation.split == capture::ObservationSplit::Validation) {
            record.role = world::ObservationRole::Validation;
        } else if (std::abs(observation.time) <= 1.0e-12) {
            record.role = world::ObservationRole::Initialization;
        } else {
            record.role = world::ObservationRole::Fit;
        }
        world.observations.push_back(std::move(record));
    }
}

CapturedMpmForwardModel::CapturedMpmForwardModel(
    std::vector<std::size_t> activeGaussianIndices,
    capture::CapturedDeformableDataset dataset,
    solvers::MpmGridSettings grid,
    NonlinearDeformableWorldSettings settings,
    world::ParameterAddress youngModulus,
    world::ParameterAddress poissonRatio,
    std::optional<world::ParameterAddress> density)
    : activeGaussianIndices_(std::move(activeGaussianIndices)),
      dataset_(std::move(dataset)),
      grid_(grid),
      settings_(settings),
      youngModulus_(std::move(youngModulus)),
      poissonRatio_(std::move(poissonRatio)),
      density_(std::move(density)) {
    if (activeGaussianIndices_.empty()) {
        throw std::invalid_argument("captured MPM forward model requires active Gaussian indices");
    }
    if (dataset_.observations.empty()) {
        throw std::invalid_argument("captured MPM forward model requires a captured dataset");
    }
}

std::vector<world::ObservationPrediction> CapturedMpmForwardModel::operator()(
    const world::WorldIR& candidate) const {
    NonlinearDeformableWorldSettings settings = settings_;
    settings.material.youngModulus = requireParameter(candidate, youngModulus_, "Young's modulus");
    settings.material.poissonRatio = requireParameter(candidate, poissonRatio_, "Poisson ratio");
    if (density_.has_value()) {
        settings.material.density = requireParameter(candidate, *density_, "density");
    }

    const auto replay = runCapturedFreeRelaxationBenchmark(
        candidate.appearance, activeGaussianIndices_, dataset_, grid_, settings);
    std::vector<world::ObservationPrediction> predictions;
    predictions.reserve(replay.samples.size());
    for (const auto& sample : replay.samples) {
        predictions.push_back({
            observationId(sample.markerId, sample.particleId, sample.time, sample.split),
            {sample.predicted.x, sample.predicted.y, sample.predicted.z},
            world::ObservationSpace::PhysicalSI,
        });
    }
    return predictions;
}

} // namespace vulkax::research
