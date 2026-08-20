#include "vulkax/cli/deformable_reference.hpp"

#include "vulkax/research/deformable_world.hpp"

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

} // namespace

int deformableReferenceCommand(int argc, char** argv) {
    if (argc < 3 || std::string_view(argv[1]) != "deformable-reference") return -1;

    research::AffineDeformableWorldSettings settings;
    settings.steps = 48;
    settings.dt = 0.01;
    const auto result = research::runAffineDeformableWorldReference(
        makeReferenceWorld(), {0, 1}, makeReferenceBlock(), makeReferenceGrid(), settings);
    research::writeDeformableWorldEvidenceCsv(result, argv[2]);

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
              << "  csv: " << argv[2] << '\n';
    return 0;
}

} // namespace vulkax::cli
