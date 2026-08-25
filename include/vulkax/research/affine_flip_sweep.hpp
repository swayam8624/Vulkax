#pragma once

#include "vulkax/research/energy_cycle.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
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
    double jExcursion{};
    bool paretoEligible{};
    bool onParetoFrontier{};
    bool dominatesApic{};
};

struct AffineFlipBlendSweepResult {
    double physicalHorizon{};
    double dt{};
    double meaningfulPeakThresholdFraction{0.01};
    std::vector<AffineFlipBlendEntry> entries;
    std::optional<std::size_t> recommendedIndex;
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

// Analyze the sweep as a four-objective Pareto problem. Higher per-cycle
// mechanical-energy and kinetic-amplitude retention are better; lower MLS RMS
// residual and lower J excursion are better. A recommendation is produced only
// when a candidate Pareto-dominates the pure APIC reference on all four
// objectives, with at least one strict improvement. State RMS-to-APIC remains a
// reported consequence metric and is deliberately not a dominance objective.
void analyzeAffineFlipBlendSweep(AffineFlipBlendSweepResult& result);

void writeAffineFlipBlendSweepCsv(
    const AffineFlipBlendSweepResult& result,
    const std::filesystem::path& path);

void writeAffineFlipBlendPeaksCsv(
    const AffineFlipBlendSweepResult& result,
    const std::filesystem::path& path);

void writeAffineFlipBlendParetoCsv(
    const AffineFlipBlendSweepResult& result,
    const std::filesystem::path& path);

} // namespace vulkax::research
