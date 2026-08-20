#include "vulkax/cli/transfer_ablation.hpp"

#include "vulkax/research/transfer_ablation.hpp"
#include "vulkax/research/transfer_diagnostics.hpp"

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
    research::NonlinearDeformableWorldSettings physics;
    physics.steps = 240;
    physics.dt = 2.0e-4;
    physics.material = {1000.0, 1.5e4, 0.30};
    physics.couplingNeighborCount = 20;
    return physics;
}

} // namespace

int transferAblationCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "deformable-transfer-ablation") return -1;
    if (argc < 3)
        throw std::invalid_argument("usage: vulkax deformable-transfer-ablation <output.csv>");

    auto dense = makeDenseWorld();
    constexpr double physicalHorizon = 0.048;
    const auto result = research::runMpmTransferAblation(
        std::move(dense.cloud), dense.activeIndices, makeBody(), makeGrid(), physicsSettings(),
        physicalHorizon, {2.0e-4, 1.0e-4, 5.0e-5});
    research::writeMpmTransferAblationCsv(result, argv[2]);

    std::cout << std::setprecision(10)
              << "Nonlinear MPM transfer/timestep ablation\n"
              << "  physical_horizon: " << physicalHorizon << '\n';
    for (const auto& entry : result.entries) {
        std::cout << "  scheme: " << solvers::toString(entry.scheme) << '\n';
        for (const auto& level : entry.timestepSweep.levels) {
            const auto& experiment = level.experiment;
            std::cout << "    dt=" << level.dt
                      << " | steps=" << level.steps
                      << " | max_energy_drift=" << experiment.maximumRelativeMechanicalEnergyDrift
                      << " | final_energy=" << experiment.finalMechanicalEnergy
                      << " | min_J=" << experiment.minimumDeformationDeterminant
                      << " | max_J=" << experiment.maximumDeformationDeterminant
                      << " | particle_pos_rms_to_scheme_finest=" << level.particlePositionRmsToFinest
                      << " | gaussian_pos_rms_to_scheme_finest=" << level.gaussianPositionRmsToFinest
                      << '\n';
        }
        std::cout << "    observed_particle_position_order: "
                  << entry.timestepSweep.observedParticlePositionOrder << '\n'
                  << "    observed_particle_velocity_order: "
                  << entry.timestepSweep.observedParticleVelocityOrder << '\n'
                  << "    observed_gaussian_position_order: "
                  << entry.timestepSweep.observedGaussianPositionOrder << '\n';
    }
    std::cout << "  csv: " << argv[2] << '\n';
    return 0;
}

int transferDiagnosticsCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "deformable-transfer-diagnostics") return -1;
    if (argc < 3)
        throw std::invalid_argument("usage: vulkax deformable-transfer-diagnostics <output-dir>");

    const std::filesystem::path outputDirectory(argv[2]);
    std::filesystem::create_directories(outputDirectory);
    auto dense = makeDenseWorld();
    constexpr double physicalHorizon = 0.048;
    const auto result = research::runMpmTransferDiagnostics(
        std::move(dense.cloud), dense.activeIndices, makeBody(), makeGrid(), physicsSettings(),
        physicalHorizon, {2.0e-4, 1.0e-4, 5.0e-5, 2.5e-5});
    research::writeMpmTransferAblationCsv(result.ablation, outputDirectory / "ablation.csv");
    research::writeMpmTransferDiagnosticsSummaryCsv(result, outputDirectory / "scheme_summary.csv");
    research::writeMpmTransferDiagnosticsPairCsv(result, outputDirectory / "scheme_pairs.csv");

    std::cout << std::setprecision(10)
              << "MPM transfer diagnostics\n"
              << "  physical_horizon: " << physicalHorizon << '\n';
    for (const auto& scheme : result.schemes) {
        std::cout << "  scheme: " << solvers::toString(scheme.scheme) << '\n'
                  << "    finest_dt: " << scheme.finestDt << '\n'
                  << "    finest_energy_drift: " << scheme.finestRelativeEnergyDrift << '\n'
                  << "    peak_kinetic_fraction: " << scheme.peakKineticEnergyFraction << '\n'
                  << "    final_kinetic_fraction: " << scheme.finalKineticEnergyFraction << '\n'
                  << "    final_elastic_fraction: " << scheme.finalElasticEnergyFraction << '\n'
                  << "    max_gaussian_displacement: " << scheme.finestGaussianDisplacement << '\n'
                  << "    coarse_energy_floor: "
                  << (scheme.coarseFloor.valid ? scheme.coarseFloor.asymptoticRelativeEnergyDrift : 0.0) << '\n'
                  << "    fine_energy_floor: "
                  << (scheme.fineFloor.valid ? scheme.fineFloor.asymptoticRelativeEnergyDrift : 0.0) << '\n'
                  << "    floor_estimate_difference: " << scheme.floorEstimateDifference << '\n';
    }
    std::cout << "  finest scheme-pair RMS differences:\n";
    for (const auto& pair : result.finestPairDifferences)
        std::cout << "    " << solvers::toString(pair.first) << " vs " << solvers::toString(pair.second)
                  << " | particle_pos=" << pair.particlePositionRms
                  << " | particle_vel=" << pair.particleVelocityRms
                  << " | gaussian_pos=" << pair.gaussianPositionRms << '\n';
    std::cout << "  outputs: " << outputDirectory.string() << '\n';
    return 0;
}

} // namespace vulkax::cli
