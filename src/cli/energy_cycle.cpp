#include "vulkax/cli/energy_cycle.hpp"

#include "vulkax/research/energy_cycle.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
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

} // namespace

int transferEnergyCycleCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "deformable-transfer-cycle") return -1;
    if (argc < 3)
        throw std::invalid_argument("usage: vulkax deformable-transfer-cycle <output-dir>");

    constexpr double physicalHorizon = 0.192;
    constexpr double dt = 5.0e-5;
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
                  << "    first_peak_time: " << scheme.firstMeaningfulPeakTime << '\n'
                  << "    last_peak_time: " << scheme.lastMeaningfulPeakTime << '\n'
                  << "    peak_to_peak_total_energy_retention: "
                  << scheme.peakToPeakMechanicalEnergyRetention << '\n'
                  << "    peak_to_peak_kinetic_retention: "
                  << scheme.peakToPeakKineticAmplitudeRetention << '\n'
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
