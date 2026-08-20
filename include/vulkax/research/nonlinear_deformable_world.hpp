#pragma once

#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/solvers/mpm.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

namespace vulkax::research {

struct NonlinearDeformableWorldSettings {
    std::size_t steps{180};
    double dt{2.5e-4};
    solvers::MpmMaterial material{1000.0, 2.0e4, 0.30};
    solvers::Matrix3 initialDeformation{
        1.04, 0.04, 0.00,
        0.01, 0.97, 0.02,
        0.00, 0.01, 1.02,
    };
    std::size_t couplingNeighborCount{20};
};

struct NonlinearDeformableWorldFrameEvidence {
    std::size_t step{};
    double time{};
    double massConservationError{};
    double momentumConservationError{};
    double forceBalanceError{};
    double momentumBalanceError{};
    double minimumDeformationDeterminant{1.0};
    double maximumDeformationDeterminant{1.0};
    double kineticEnergy{};
    double elasticEnergy{};
    double mechanicalEnergy{};
    double relativeMechanicalEnergyDrift{};
    double centerOfMassDrift{};
    double maximumMlsRmsResidual{};
    double maximumMlsResidual{};
    double maximumGaussianDisplacement{};
    double unaffectedRegionDrift{};
};

struct NonlinearDeformableWorldResult {
    gaussian::GaussianCloud finalWorld;
    std::vector<solvers::MpmParticle> finalParticles;
    std::vector<NonlinearDeformableWorldFrameEvidence> frames;
    double initialMechanicalEnergy{};
    double finalMechanicalEnergy{};
    double maximumMassConservationError{};
    double maximumMomentumConservationError{};
    double maximumForceBalanceError{};
    double maximumMomentumBalanceError{};
    double minimumDeformationDeterminant{1.0};
    double maximumDeformationDeterminant{1.0};
    double maximumRelativeMechanicalEnergyDrift{};
    double maximumCenterOfMassDrift{};
    double maximumMlsRmsResidual{};
    double maximumMlsResidual{};
    double maximumGaussianDisplacement{};
    double maximumUnaffectedRegionDrift{};
};

// Called after a completed nonlinear MPM step and Gaussian rewrite. The world
// reference is valid only for the duration of the callback, allowing streaming
// render/export without retaining every captured-world state in memory.
using NonlinearDeformableWorldObserver = std::function<void(
    const NonlinearDeformableWorldFrameEvidence& frame,
    const gaussian::GaussianCloud& world)>;

// Free nonlinear elastic relaxation. Particles are initialized in a finite
// deformation and then evolved only by compressible Neo-Hookean internal
// forces. There is no gravity, no external force and no boundary contact.
// This makes momentum, center-of-mass and locality errors meaningful while
// energy drift measures the explicit APIC/MPM time integration itself.
[[nodiscard]] NonlinearDeformableWorldResult runNonlinearDeformableWorld(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    const NonlinearDeformableWorldSettings& settings = {},
    const NonlinearDeformableWorldObserver& observer = {});

void writeNonlinearDeformableWorldEvidenceCsv(
    const NonlinearDeformableWorldResult& result,
    const std::filesystem::path& path);

} // namespace vulkax::research
