#pragma once

#include "vulkax/research/nonlinear_deformable_world.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace vulkax::research {

struct NonlinearTimestepLevel {
    double dt{};
    std::size_t steps{};
    double finalTime{};
    NonlinearDeformableWorldResult experiment;
    double particlePositionRmsToFinest{};
    double particleVelocityRmsToFinest{};
    double gaussianPositionRmsToFinest{};
};

struct NonlinearTimestepSweepResult {
    std::vector<NonlinearTimestepLevel> levels; // coarse -> fine
    double observedParticlePositionOrder{};
    double observedParticleVelocityOrder{};
    double observedGaussianPositionOrder{};
};

// Runs the same nonlinear experiment to the same physical horizon with several
// timesteps. Timesteps are sorted coarse-to-fine. The finest final state is used
// as a practical reference for RMS state errors. When the three finest levels
// share a common refinement ratio, observed orders are estimated from pairwise
// coarse/medium and medium/fine state differences.
[[nodiscard]] NonlinearTimestepSweepResult runNonlinearTimestepSweep(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    double physicalHorizon,
    std::vector<double> timesteps);

void writeNonlinearTimestepSweepCsv(
    const NonlinearTimestepSweepResult& result,
    const std::filesystem::path& path);

} // namespace vulkax::research
