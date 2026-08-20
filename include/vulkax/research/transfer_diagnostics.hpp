#pragma once

#include "vulkax/research/transfer_ablation.hpp"

#include <filesystem>
#include <vector>

namespace vulkax::research {

struct DissipationFloorEstimate {
    bool valid{};
    double observedOrder{};
    double asymptoticRelativeEnergyDrift{};
};

struct TransferSchemeDiagnostics {
    solvers::MpmTransferScheme scheme{solvers::MpmTransferScheme::APIC};
    double finestDt{};
    double finestRelativeEnergyDrift{};
    double peakKineticEnergyFraction{};
    double finalKineticEnergyFraction{};
    double finalElasticEnergyFraction{};
    double finestGaussianDisplacement{};
    double finestMinimumDeformationDeterminant{1.0};
    double finestMaximumDeformationDeterminant{1.0};
    DissipationFloorEstimate coarseFloor;
    DissipationFloorEstimate fineFloor;
    double floorEstimateDifference{};
};

struct TransferSchemePairDifference {
    solvers::MpmTransferScheme first{solvers::MpmTransferScheme::PIC};
    solvers::MpmTransferScheme second{solvers::MpmTransferScheme::APIC};
    double particlePositionRms{};
    double particleVelocityRms{};
    double gaussianPositionRms{};
};

struct MpmTransferDiagnosticsResult {
    MpmTransferAblationResult ablation;
    std::vector<TransferSchemeDiagnostics> schemes;
    std::vector<TransferSchemePairDifference> finestPairDifferences;
};

[[nodiscard]] MpmTransferDiagnosticsResult runMpmTransferDiagnostics(
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

void writeMpmTransferDiagnosticsSummaryCsv(
    const MpmTransferDiagnosticsResult& result,
    const std::filesystem::path& path);

void writeMpmTransferDiagnosticsPairCsv(
    const MpmTransferDiagnosticsResult& result,
    const std::filesystem::path& path);

} // namespace vulkax::research
