#pragma once

#include "vulkax/research/energy_cycle.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vulkax::research {

struct AffineFlipBlendEntry {
    std::string label;
    solvers::MpmTransferScheme scheme{solvers::MpmTransferScheme::APIC};
    double flipBlend{};
    TransferEnergyCycleSchemeResult cycle;
    double particlePositionRmsToApic{};
    double particleVelocityRmsToApic{};
    double gaussianPositionRmsToApic{};
};

struct AffineFlipBlendSweepResult {
    double physicalHorizon{};
    double dt{};
    double meaningfulPeakThresholdFraction{0.01};
    std::vector<AffineFlipBlendEntry> entries;
};

[[nodiscard]] AffineFlipBlendSweepResult runAffineFlipBlendSweep(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    double physicalHorizon,
    double dt,
    std::vector<double> flipBlends = {0.0, 0.25, 0.5, 0.75, 1.0},
    double meaningfulPeakThresholdFraction = 0.01);

void writeAffineFlipBlendSweepCsv(
    const AffineFlipBlendSweepResult& result,
    const std::filesystem::path& path);

void writeAffineFlipBlendPeaksCsv(
    const AffineFlipBlendSweepResult& result,
    const std::filesystem::path& path);

} // namespace vulkax::research
