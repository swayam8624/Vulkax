#include "vulkax/cli/deformable_reference.hpp"

#include "vulkax/research/deformable_world.hpp"
#include "vulkax/research/nonlinear_deformable_world.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace vulkax::cli {
namespace {

std::vector<solvers::MpmParticle> makeReferenceBlock() {
    std::vector<solvers::MpmParticle> particles;
    std::uint64_t id = 1;
    for (int sx : {-1, 1})
        for (int sy : {-1, 1})
            for (int sz : {-1, 1}) {
                solvers::MpmParticle particle;
                particle.id = id++;
                particle.restPosition = {
                    0.16 * static_cast<double>(sx),
                    0.16 * static_cast<double>(sy),
                    0.16 * static_cast<double>(sz),
                };
                particle.position = particle.restPosition;
                particle.mass = 1.0;
                particle.restVolume = 1.0e-3;
                particles.push_back(particle);
            }
    return particles;
}

std::vector<solvers::MpmParticle> makeNonlinearBody() {
    std::vector<solvers::MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double spacing = 0.12;
    constexpr double volume = spacing * spacing * spacing;
    constexpr double density = 1000.0;
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                solvers::MpmParticle particle;
                particle.id = id++;
                particle.restPosition = {
                    (static_cast<double>(x) - 1.5) * spacing,
                    (static_cast<double>(y) - 1.5) * spacing,
                    (static_cast<double>(z) - 1.5) * spacing,
                };
                particle.position = particle.restPosition;
                particle.mass = density * volume;
                particle.restVolume = volume;
                particles.push_back(particle);
            }
    return particles;
}

gaussian::GaussianSplat makeSplat(math::Vec3 position, double sx, double sy, double sz) {
    gaussian::GaussianSplat splat;
    splat.position = position;
    splat.logScale = {std::log(sx), std::log(sy), std::log(sz)};
    splat.rotation = {1.0, 0.0, 0.0, 0.0};
    splat.opacityLogit = 4.0;
    splat.shDC = {0.3, 0.1, -0.1};
    return splat;
}

gaussian::GaussianCloud makeReferenceWorld() {
    gaussian::GaussianCloud world;
    world.splats.push_back(makeSplat({0.0, 0.0, 0.0}, 0.12, 0.055, 0.08));
    world.splats.push_back(makeSplat({0.055, -0.035, 0.025}, 0.07, 0.045, 0.035));
    world.splats.push_back(makeSplat({0.72, 0.48, -0.22}, 0.09, 0.06, 0.04));
    return world;
}

gaussian::GaussianCloud makeNonlinearWorld() {
    gaussian::GaussianCloud world;
    world.splats.push_back(makeSplat({-0.08, 0.04, 0.02}, 0.07, 0.050, 0.038));
    world.splats.push_back(makeSplat({0.09, -0.06, 0.03}, 0.065, 0.047, 0.036));
    world.splats.push_back(makeSplat({0.04, 0.10, -0.07}, 0.055, 0.040, 0.031));
    world.splats.push_back(makeSplat({-0.05, -0.08, -0.06}, 0.060, 0.043, 0.033));
    world.splats.push_back(makeSplat({0.76, 0.55, -0.30}, 0.08, 0.06, 0.04));
    return world;
}

solvers::MpmGridSettings makeReferenceGrid() {
    solvers::MpmGridSettings grid;
    grid.origin = {-1.2, -1.2, -1.2};
    grid.nx = 31;
    grid.ny = 31;
    grid.nz = 31;
    grid.cellSize = 0.08;
    grid.boundaryCells = 0;
    return grid;
}

solvers::MpmGridSettings makeNonlinearGrid() {
    solvers::MpmGridSettings grid;
    grid.origin = {-1.0, -1.0, -1.0};
    grid.nx = 26;
    grid.ny = 26;
    grid.nz = 26;
    grid.cellSize = 0.08;
    grid.boundaryCells = 0;
    return grid;
}

int runAffineReference(const char* outputPath) {
    research::AffineDeformableWorldSettings settings;
    settings.steps = 48;
    settings.dt = 0.01;
    const auto result = research::runAffineDeformableWorldReference(
        makeReferenceWorld(), {0, 1}, makeReferenceBlock(), makeReferenceGrid(), settings);
    research::writeDeformableWorldEvidenceCsv(result, outputPath);

    std::cout << std::setprecision(8)
              << "Deformable-world reference experiment\n"
              << "  frames: " << result.frames.size() << '\n'
              << "  max_mass_error: " << result.maximumMassConservationError << '\n'
              << "  max_momentum_error: " << result.maximumMomentumConservationError << '\n'
              << "  max_force_balance_error: " << result.maximumForceBalanceError << '\n'
              << "  max_momentum_balance_error: " << result.maximumMomentumBalanceError << '\n'
              << "  max_J_error: " << result.maximumDeformationDeterminantError << '\n'
              << "  max_gaussian_position_error: " << result.maximumGaussianPositionError << '\n'
              << "  max_gaussian_covariance_error: " << result.maximumGaussianCovarianceError << '\n'
              << "  max_force_transfer_error: " << result.maximumForceTransferError << '\n'
              << "  max_torque_transfer_error: " << result.maximumTorqueTransferError << '\n'
              << "  max_unaffected_region_drift: " << result.maximumUnaffectedRegionDrift << '\n'
              << "  csv: " << outputPath << '\n';
    return 0;
}

int runNonlinearReference(const char* outputPath) {
    research::NonlinearDeformableWorldSettings settings;
    settings.steps = 240;
    settings.dt = 2.0e-4;
    settings.material = {1000.0, 1.5e4, 0.30};
    settings.couplingNeighborCount = 20;
    const auto result = research::runNonlinearDeformableWorld(
        makeNonlinearWorld(), {0, 1, 2, 3}, makeNonlinearBody(), makeNonlinearGrid(), settings);
    research::writeNonlinearDeformableWorldEvidenceCsv(result, outputPath);

    std::cout << std::setprecision(8)
              << "Nonlinear deformable-world experiment\n"
              << "  frames: " << result.frames.size() << '\n'
              << "  initial_energy: " << result.initialMechanicalEnergy << '\n'
              << "  final_energy: " << result.finalMechanicalEnergy << '\n'
              << "  max_relative_energy_drift: " << result.maximumRelativeMechanicalEnergyDrift << '\n'
              << "  min_J: " << result.minimumDeformationDeterminant << '\n'
              << "  max_J: " << result.maximumDeformationDeterminant << '\n'
              << "  max_mass_error: " << result.maximumMassConservationError << '\n'
              << "  max_momentum_error: " << result.maximumMomentumConservationError << '\n'
              << "  max_force_balance_error: " << result.maximumForceBalanceError << '\n'
              << "  max_momentum_balance_error: " << result.maximumMomentumBalanceError << '\n'
              << "  max_center_of_mass_drift: " << result.maximumCenterOfMassDrift << '\n'
              << "  max_mls_rms_residual: " << result.maximumMlsRmsResidual << '\n'
              << "  max_mls_residual: " << result.maximumMlsResidual << '\n'
              << "  max_gaussian_displacement: " << result.maximumGaussianDisplacement << '\n'
              << "  max_unaffected_region_drift: " << result.maximumUnaffectedRegionDrift << '\n'
              << "  csv: " << outputPath << '\n';
    return 0;
}

} // namespace

int deformableReferenceCommand(int argc, char** argv) {
    if (argc < 3) return -1;
    const std::string_view command(argv[1]);
    if (command == "deformable-reference") return runAffineReference(argv[2]);
    if (command == "deformable-nonlinear") return runNonlinearReference(argv[2]);
    return -1;
}

} // namespace vulkax::cli
