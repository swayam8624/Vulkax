#pragma once

#include "vulkax/research/nonlinear_deformable_world.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace vulkax::research {

struct EnergyCyclePeak {
    std::size_t ordinal{};
    std::size_t step{};
    double time{};
    double kineticEnergyFraction{};
    double mechanicalEnergyFraction{};
    double gaussianDisplacement{};
    double mlsRmsResidual{};
};

struct TransferEnergyCycleSchemeResult {
    solvers::MpmTransferScheme scheme{solvers::MpmTransferScheme::APIC};
    double dt{};
    double physicalHorizon{};
    NonlinearDeformableWorldResult experiment;
    std::vector<EnergyCyclePeak> kineticPeaks;
    std::size_t meaningfulKineticPeakCount{};
    double minimumMechanicalEnergyFraction{1.0};
    double maximumMechanicalEnergyFraction{1.0};
    double finalMechanicalEnergyFraction{1.0};
    double peakKineticEnergyFraction{};
    double firstMeaningfulPeakTime{};
    double lastMeaningfulPeakTime{};
    double firstMeaningfulPeakMechanicalEnergyFraction{};
    double lastMeaningfulPeakMechanicalEnergyFraction{};
    double peakToPeakMechanicalEnergyRetention{};
    double firstMeaningfulPeakKineticEnergyFraction{};
    double lastMeaningfulPeakKineticEnergyFraction{};
    double peakToPeakKineticAmplitudeRetention{};
};

struct TransferEnergyCycleResult {
    double physicalHorizon{};
    double dt{};
    double meaningfulPeakThresholdFraction{0.01};
    std::vector<TransferEnergyCycleSchemeResult> schemes;
};

[[nodiscard]] TransferEnergyCycleResult runTransferEnergyCycleDiagnostic(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    double physicalHorizon,
    double dt,
    double meaningfulPeakThresholdFraction = 0.01,
    std::vector<solvers::MpmTransferScheme> schemes = {
        solvers::MpmTransferScheme::PIC,
        solvers::MpmTransferScheme::FLIP,
        solvers::MpmTransferScheme::APIC,
    });

void writeTransferEnergyCycleSummaryCsv(
    const TransferEnergyCycleResult& result,
    const std::filesystem::path& path);

void writeTransferEnergyCycleTraces(
    const TransferEnergyCycleResult& result,
    const std::filesystem::path& directory);

} // namespace vulkax::research
