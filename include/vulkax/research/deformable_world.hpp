#pragma once

#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/solvers/mpm.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace vulkax::research {

struct AffineDeformableWorldSettings {
    std::size_t steps{24};
    double dt{0.01};
    solvers::Matrix3 velocityGradient{
        0.20, 0.05, 0.00,
       -0.03,-0.10, 0.04,
        0.00, 0.02, 0.06,
    };
    math::Vec3 translationVelocity{0.08, -0.03, 0.02};
    math::Vec3 interactionProbeForce{1.5, -2.0, 0.75};
};

struct DeformableWorldFrameEvidence {
    std::size_t step{};
    double time{};
    double massConservationError{};
    double momentumConservationError{};
    double forceBalanceError{};
    double momentumBalanceError{};
    double minimumDeformationDeterminant{1.0};
    double maximumDeformationDeterminant{1.0};
    double expectedDeformationDeterminant{1.0};
    double deformationDeterminantError{};
    double maximumGaussianPositionError{};
    double maximumGaussianCovarianceError{};
    double forceTransferError{};
    double torqueTransferError{};
    double unaffectedPositionDrift{};
    double unaffectedCovarianceDrift{};
    double unaffectedRegionDrift{};
};

struct DeformableWorldExperimentResult {
    gaussian::GaussianCloud finalWorld;
    std::vector<solvers::MpmParticle> finalParticles;
    std::vector<DeformableWorldFrameEvidence> frames;
    double maximumMassConservationError{};
    double maximumMomentumConservationError{};
    double maximumForceBalanceError{};
    double maximumMomentumBalanceError{};
    double maximumDeformationDeterminantError{};
    double maximumGaussianPositionError{};
    double maximumGaussianCovarianceError{};
    double maximumForceTransferError{};
    double maximumTorqueTransferError{};
    double maximumUnaffectedRegionDrift{};
};

// Controlled multi-step APIC/MPM reference experiment. The constitutive stress is
// disabled deliberately so the prescribed affine velocity field has an exact
// discrete ground truth. Only the selected Gaussian indices are coupled to the
// MPM body; all other splats remain untouched and form the locality control.
[[nodiscard]] DeformableWorldExperimentResult runAffineDeformableWorldReference(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    const AffineDeformableWorldSettings& settings = {});

// Writes one row per experiment frame using stable column names intended for
// plotting, regression dashboards, and paper-artifact analysis.
void writeDeformableWorldEvidenceCsv(
    const DeformableWorldExperimentResult& result,
    const std::filesystem::path& path);

} // namespace vulkax::research
