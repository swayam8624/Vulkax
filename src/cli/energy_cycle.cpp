#include "vulkax/cli/energy_cycle.hpp"

#include "vulkax/research/affine_flip_sweep.hpp"
#include "vulkax/research/energy_cycle.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vulkax::cli {
namespace {

std::vector<solvers::MpmParticle> makeBody() {
    std::vector<solvers::MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double spacing = 0.12;
    constexpr double restVolume = spacing * spacing * spacing;
    constexpr double density = 1000.0;
    for (int iz = 0; iz < 4; ++iz)
        for (int iy = 0; iy < 4; ++iy)
            for (int ix = 0; ix < 4; ++ix) {
                solvers::MpmParticle particle;
                particle.id = id++;
                particle.restPosition = {
                    (static_cast<double>(ix) - 1.5) * spacing,
                    (static_cast<double>(iy) - 1.5) * spacing,
                    (static_cast<double>(iz) - 1.5) * spacing,
                };
                particle.position = particle.restPosition;
                particle.restVolume = restVolume;
                particle.mass = density * restVolume;
                particles.push_back(particle);
            }
    return particles;
}

gaussian::GaussianSplat makeSplat(math::Vec3 position, double scale, double tint) {
    gaussian::GaussianSplat splat;
    splat.position = position;
    splat.logScale = {std::log(scale), std::log(scale * 0.82), std::log(scale * 0.66)};
    splat.rotation = {1.0, 0.0, 0.0, 0.0};
    splat.opacityLogit = 3.5;
    splat.shDC = {0.25 + 0.25 * tint, 0.02 + 0.12 * tint, -0.10 + 0.18 * tint};
    return splat;
}

struct DenseWorld {
    gaussian::GaussianCloud cloud;
    std::vector<std::size_t> activeIndices;
};

DenseWorld makeDenseWorld() {
    DenseWorld result;
    result.cloud.splats.reserve(126);
    result.activeIndices.reserve(125);
    constexpr int resolution = 5;
    constexpr double spacing = 0.075;
    constexpr double scale = 0.045;
    for (int iz = 0; iz < resolution; ++iz)
        for (int iy = 0; iy < resolution; ++iy)
            for (int ix = 0; ix < resolution; ++ix) {
                const math::Vec3 position{
                    (static_cast<double>(ix) - 2.0) * spacing,
                    (static_cast<double>(iy) - 2.0) * spacing,
                    (static_cast<double>(iz) - 2.0) * spacing,
                };
                const double tint = static_cast<double>(ix + iy + iz) / 12.0;
                result.activeIndices.push_back(result.cloud.splats.size());
                result.cloud.splats.push_back(makeSplat(position, scale, tint));
            }
    result.cloud.splats.push_back(makeSplat({5.0, 5.0, 5.0}, 0.08, 0.5));
    return result;
}

solvers::MpmGridSettings makeGrid() {
    solvers::MpmGridSettings grid;
    grid.origin = {-1.0, -1.0, -1.0};
    grid.nx = 26;
    grid.ny = 26;
    grid.nz = 26;
    grid.cellSize = 0.08;
    grid.boundaryCells = 0;
    return grid;
}

research::NonlinearDeformableWorldSettings physicsSettings() {
    research::NonlinearDeformableWorldSettings settings;
    settings.material = {1000.0, 1.5e4, 0.30};
    settings.couplingNeighborCount = 20;
    return settings;
}

double parsePositiveDouble(std::string_view text, std::string_view label) {
    std::size_t consumed = 0;
    const std::string owned(text);
    double value = 0.0;
    try {
        value = std::stod(owned, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " must be a positive finite number");
    }
    if (consumed != owned.size() || !std::isfinite(value) || value <= 0.0)
        throw std::invalid_argument(std::string(label) + " must be a positive finite number");
    return value;
}

int hybridSweepCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "deformable-transfer-hybrid") return -1;
    if (argc < 3)
        throw std::invalid_argument(
            "usage: vulkax deformable-transfer-hybrid <output-dir> [physical-horizon] [dt]");

    const double physicalHorizon =
        argc >= 4 ? parsePositiveDouble(argv[3], "physical horizon") : 0.48;
    const double dt = argc >= 5 ? parsePositiveDouble(argv[4], "timestep") : 5.0e-5;
    constexpr double meaningfulPeakThreshold = 0.01;

    auto dense = makeDenseWorld();
    const auto result = research::runAffineFlipBlendSweep(
        std::move(dense.cloud), dense.activeIndices, makeBody(), makeGrid(), physicsSettings(),
        physicalHorizon, dt, {0.0, 0.25, 0.5, 0.75, 1.0}, meaningfulPeakThreshold);

    const std::filesystem::path outputDirectory(argv[2]);
    std::filesystem::create_directories(outputDirectory);
    research::writeAffineFlipBlendSweepCsv(result, outputDirectory / "summary.csv");
    research::writeAffineFlipBlendPeaksCsv(result, outputDirectory / "peaks.csv");

    std::cout << std::setprecision(10)
              << "Affine APIC-FLIP transfer sweep\n"
              << "  physical_horizon: " << physicalHorizon << '\n'
              << "  dt: " << dt << '\n'
              << "  meaningful_peak_threshold_fraction: " << meaningfulPeakThreshold << '\n';
    for (const auto& entry : result.entries) {
        const auto& cycle = entry.cycle;
        const auto& experiment = cycle.experiment;
        std::cout << "  candidate: " << entry.label << '\n'
                  << "    scheme: " << solvers::toString(entry.scheme) << '\n'
                  << "    flip_blend: " << entry.flipBlend << '\n'
                  << "    meaningful_cycles: " << cycle.completedMeaningfulCycles << '\n'
                  << "    mean_cycle_period: " << cycle.meanMeaningfulCyclePeriod << '\n'
                  << "    mean_total_energy_retention_per_cycle: "
                  << cycle.meanMechanicalEnergyRetentionPerCycle << '\n'
                  << "    mean_kinetic_retention_per_cycle: "
                  << cycle.meanKineticAmplitudeRetentionPerCycle << '\n'
                  << "    max_gaussian_displacement: " << experiment.maximumGaussianDisplacement << '\n'
                  << "    max_mls_rms_residual: " << experiment.maximumMlsRmsResidual << '\n'
                  << "    max_mls_residual: " << experiment.maximumMlsResidual << '\n'
                  << "    min_J: " << experiment.minimumDeformationDeterminant << '\n'
                  << "    max_J: " << experiment.maximumDeformationDeterminant << '\n'
                  << "    particle_position_rms_to_apic: " << entry.particlePositionRmsToApic << '\n'
                  << "    particle_velocity_rms_to_apic: " << entry.particleVelocityRmsToApic << '\n'
                  << "    gaussian_position_rms_to_apic: " << entry.gaussianPositionRmsToApic << '\n';
    }
    std::cout << "  outputs: " << outputDirectory.string() << '\n';
    return 0;
}

} // namespace

int transferEnergyCycleCommand(int argc, char** argv) {
    const int hybridResult = hybridSweepCommand(argc, argv);
    if (hybridResult >= 0) return hybridResult;

    if (argc < 2 || std::string_view(argv[1]) != "deformable-transfer-cycle") return -1;
    if (argc < 3)
        throw std::invalid_argument(
            "usage: vulkax deformable-transfer-cycle <output-dir> [physical-horizon] [dt]");

    const double physicalHorizon =
        argc >= 4 ? parsePositiveDouble(argv[3], "physical horizon") : 0.192;
    const double dt = argc >= 5 ? parsePositiveDouble(argv[4], "timestep") : 5.0e-5;
    constexpr double meaningfulPeakThreshold = 0.01;

    auto dense = makeDenseWorld();
    const auto result = research::runTransferEnergyCycleDiagnostic(
        std::move(dense.cloud), dense.activeIndices, makeBody(), makeGrid(), physicsSettings(),
        physicalHorizon, dt, meaningfulPeakThreshold);

    const std::filesystem::path outputDirectory(argv[2]);
    std::filesystem::create_directories(outputDirectory);
    research::writeTransferEnergyCycleSummaryCsv(result, outputDirectory / "summary.csv");
    research::writeTransferEnergyCycleTraces(result, outputDirectory);

    std::cout << std::setprecision(10)
              << "Long-horizon MPM transfer energy-cycle diagnostic\n"
              << "  physical_horizon: " << physicalHorizon << '\n'
              << "  dt: " << dt << '\n'
              << "  meaningful_peak_threshold_fraction: " << meaningfulPeakThreshold << '\n';
    for (const auto& scheme : result.schemes) {
        std::cout << "  scheme: " << solvers::toString(scheme.scheme) << '\n'
                  << "    steps: " << scheme.experiment.frames.size() << '\n'
                  << "    final_energy_fraction: " << scheme.finalMechanicalEnergyFraction << '\n'
                  << "    min_energy_fraction: " << scheme.minimumMechanicalEnergyFraction << '\n'
                  << "    peak_kinetic_fraction: " << scheme.peakKineticEnergyFraction << '\n'
                  << "    all_kinetic_peaks: " << scheme.kineticPeaks.size() << '\n'
                  << "    meaningful_kinetic_peaks: " << scheme.meaningfulKineticPeakCount << '\n'
                  << "    completed_meaningful_cycles: " << scheme.completedMeaningfulCycles << '\n'
                  << "    first_peak_time: " << scheme.firstMeaningfulPeakTime << '\n'
                  << "    last_peak_time: " << scheme.lastMeaningfulPeakTime << '\n'
                  << "    mean_cycle_period: " << scheme.meanMeaningfulCyclePeriod << '\n'
                  << "    peak_to_peak_total_energy_retention: "
                  << scheme.peakToPeakMechanicalEnergyRetention << '\n'
                  << "    mean_total_energy_retention_per_cycle: "
                  << scheme.meanMechanicalEnergyRetentionPerCycle << '\n'
                  << "    peak_to_peak_kinetic_retention: "
                  << scheme.peakToPeakKineticAmplitudeRetention << '\n'
                  << "    mean_kinetic_retention_per_cycle: "
                  << scheme.meanKineticAmplitudeRetentionPerCycle << '\n'
                  << "    max_gaussian_displacement: "
                  << scheme.experiment.maximumGaussianDisplacement << '\n'
                  << "    max_mls_rms_residual: " << scheme.experiment.maximumMlsRmsResidual << '\n'
                  << "    min_J: " << scheme.experiment.minimumDeformationDeterminant << '\n'
                  << "    max_J: " << scheme.experiment.maximumDeformationDeterminant << '\n'
                  << "    max_unaffected_region_drift: "
                  << scheme.experiment.maximumUnaffectedRegionDrift << '\n';
    }
    std::cout << "  outputs: " << outputDirectory.string() << '\n';
    return 0;
}

} // namespace vulkax::cli
