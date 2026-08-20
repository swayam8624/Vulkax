#pragma once

#include "vulkax/research/timestep_convergence.hpp"

#include <filesystem>
#include <vector>

namespace vulkax::research {

struct MpmTransferAblationEntry {
    solvers::MpmTransferScheme scheme{solvers::MpmTransferScheme::APIC};
    NonlinearTimestepSweepResult timestepSweep;
};

struct MpmTransferAblationResult {
    double physicalHorizon{};
    std::vector<MpmTransferAblationEntry> entries;
};

[[nodiscard]] MpmTransferAblationResult runMpmTransferAblation(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    double physicalHorizon,
    std::vector<double> timesteps,
    std::vector<solvers::MpmTransferScheme> schemes = {
        solvers::MpmTransferScheme::PIC,
        solvers::MpmTransferScheme::FLIP,
        solvers::MpmTransferScheme::APIC,
    });

void writeMpmTransferAblationCsv(
    const MpmTransferAblationResult& result,
    const std::filesystem::path& path);

} // namespace vulkax::research
