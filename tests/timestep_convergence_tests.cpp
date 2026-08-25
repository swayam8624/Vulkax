#include "vulkax/research/timestep_convergence.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<vulkax::solvers::MpmParticle> makeBody() {
    std::vector<vulkax::solvers::MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double spacing = 0.12;
    constexpr double restVolume = spacing * spacing * spacing;
    constexpr double density = 1000.0;
    for (int iz = 0; iz < 4; ++iz)
        for (int iy = 0; iy < 4; ++iy)
            for (int ix = 0; ix < 4; ++ix) {
                vulkax::solvers::MpmParticle particle;
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

vulkax::gaussian::GaussianSplat splat(vulkax::math::Vec3 position) {
    vulkax::gaussian::GaussianSplat result;
    result.position = position;
    result.logScale = {std::log(0.06), std::log(0.045), std::log(0.035)};
    result.rotation = {1.0, 0.0, 0.0, 0.0};
    result.opacityLogit = 4.0;
    return result;
}

vulkax::gaussian::GaussianCloud makeWorld() {
    vulkax::gaussian::GaussianCloud world;
    world.splats.push_back(splat({-0.08, 0.04, 0.02}));
    world.splats.push_back(splat({0.09, -0.06, 0.03}));
    world.splats.push_back(splat({0.04, 0.10, -0.07}));
    world.splats.push_back(splat({-0.05, -0.08, -0.06}));
    world.splats.push_back(splat({0.76, 0.55, -0.30}));
    return world;
}

vulkax::solvers::MpmGridSettings makeGrid() {
    vulkax::solvers::MpmGridSettings grid;
    grid.origin = {-1.0, -1.0, -1.0};
    grid.nx = 26;
    grid.ny = 26;
    grid.nz = 26;
    grid.cellSize = 0.08;
    grid.boundaryCells = 0;
    return grid;
}

} // namespace

int main() {
    using namespace vulkax;
    research::NonlinearDeformableWorldSettings settings;
    settings.material = {1000.0, 1.5e4, 0.30};
    settings.couplingNeighborCount = 20;

    const auto sweep = research::runNonlinearTimestepSweep(
        makeWorld(), {0, 1, 2, 3}, makeBody(), makeGrid(), settings,
        0.004, {4.0e-4, 2.0e-4, 1.0e-4});

    assert(sweep.levels.size() == 3);
    assert(sweep.levels[0].steps == 10);
    assert(sweep.levels[1].steps == 20);
    assert(sweep.levels[2].steps == 40);
    for (const auto& level : sweep.levels) {
        assert(std::abs(level.finalTime - 0.004) < 1.0e-12);
        assert(std::isfinite(level.experiment.finalMechanicalEnergy));
        assert(level.experiment.minimumDeformationDeterminant > 0.0);
        assert(level.particlePositionRmsToFinest >= 0.0);
        assert(level.particleVelocityRmsToFinest >= 0.0);
        assert(level.gaussianPositionRmsToFinest >= 0.0);
    }
    assert(sweep.levels.back().particlePositionRmsToFinest == 0.0);
    assert(sweep.levels.back().particleVelocityRmsToFinest == 0.0);
    assert(sweep.levels.back().gaussianPositionRmsToFinest == 0.0);
    assert(std::isfinite(sweep.observedParticlePositionOrder) ||
           std::isnan(sweep.observedParticlePositionOrder));
    assert(std::isfinite(sweep.observedParticleVelocityOrder) ||
           std::isnan(sweep.observedParticleVelocityOrder));
    assert(std::isfinite(sweep.observedGaussianPositionOrder) ||
           std::isnan(sweep.observedGaussianPositionOrder));

    const auto output = std::filesystem::temp_directory_path() / "vulkax_timestep_convergence.csv";
    research::writeNonlinearTimestepSweepCsv(sweep, output);
    assert(std::filesystem::exists(output));
    assert(std::filesystem::file_size(output) > 256U);
    std::ifstream stream(output);
    std::string header;
    std::getline(stream, header);
    assert(header.find("particle_position_rms_to_finest") != std::string::npos);
    assert(header.find("observed_particle_position_order") != std::string::npos);
    stream.close();
    std::filesystem::remove(output);
    return 0;
}
