#pragma once

#include "vulkax/research/captured_deformable.hpp"
#include "vulkax/world/reality_loop.hpp"

#include <optional>
#include <string>
#include <vector>

namespace vulkax::research {

struct CapturedObservationImportOptions {
    std::optional<world::EntityId> entityId;
    world::EvidenceClass evidence{world::EvidenceClass::Measured};
    std::optional<double> isotropicPositionStandardDeviation;
    std::string source{"captured_deformable_dataset"};
};

[[nodiscard]] std::string capturedObservationId(
    const capture::CapturedMarkerObservation& observation);

void appendCapturedObservations(
    world::WorldIR& world,
    const capture::CapturedDeformableDataset& dataset,
    const CapturedObservationImportOptions& options = {});

// Bridges the existing captured nonlinear APIC/MPM benchmark into the generic
// WorldIR ForwardModel contract. Material scalars are read from the candidate
// WorldIR on every invocation, while appearance is taken from candidate.appearance,
// so both inferred parameters and local world rewrites participate in prediction.
class CapturedMpmForwardModel {
  public:
    CapturedMpmForwardModel(
        std::vector<std::size_t> activeGaussianIndices,
        capture::CapturedDeformableDataset dataset,
        solvers::MpmGridSettings grid,
        NonlinearDeformableWorldSettings settings,
        world::ParameterAddress youngModulus,
        world::ParameterAddress poissonRatio,
        std::optional<world::ParameterAddress> density = std::nullopt);

    [[nodiscard]] std::vector<world::ObservationPrediction> operator()(
        const world::WorldIR& candidate) const;

  private:
    std::vector<std::size_t> activeGaussianIndices_;
    capture::CapturedDeformableDataset dataset_;
    solvers::MpmGridSettings grid_;
    NonlinearDeformableWorldSettings settings_;
    world::ParameterAddress youngModulus_;
    world::ParameterAddress poissonRatio_;
    std::optional<world::ParameterAddress> density_;
};

} // namespace vulkax::research
