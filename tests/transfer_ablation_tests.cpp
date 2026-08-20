#include "vulkax/research/transfer_ablation.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

std::vector<vulkax::solvers::MpmParticle> makeBody() {
    using namespace vulkax;
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

vulkax::gaussian::GaussianSplat splat(vulkax::math::Vec3 position) {
    vulkax::gaussian::GaussianSplat result;
    result.position = position;
    result.logScale = {std::log(0.06), std::log(0.05), std::log(0.04)};
    result.rotation = {1.0, 0.0, 0.0, 0.0};
    result.opacityLogit = 4.0;
    result.shDC = {0.2, 0.0, -0.1};
    return result;
}

vulkax::gaussian::GaussianCloud makeWorld() {
    vulkax::gaussian::GaussianCloud world;
    world.splats.push_back(splat({-0.08, 0.04, 0.02}));
    world.splats.push_back(splat({0.09, -0.06, 0.03}));
    world.splats.push_back(splat({0.04, 0.10, -0.07}));
    world.splats.push_back(splat({-0.05, -0.08, -0.06}));
    world.splats.push_back(splat({5.0, 5.0, 5.0}));
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

double particlePositionDifference(
    const std::vector<vulkax::solvers::MpmParticle>& lhs,
    const std::vector<vulkax::solvers::MpmParticle>& rhs) {
    assert(lhs.size() == rhs.size());
    double squared = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto delta = lhs[index].position - rhs[index].position;
        squared += vulkax::math::dot(delta, delta);
    }
    return std::sqrt(squared / static_cast<double>(lhs.size()));
}

} // namespace

int main() {
    using namespace vulkax;

    research::NonlinearDeformableWorldSettings settings;
    settings.material = {1000.0, 1.5e4, 0.30};
    settings.couplingNeighborCount = 20;

    const auto result = research::runMpmTransferAblation(
        makeWorld(), {0, 1, 2, 3}, makeBody(), makeGrid(), settings,
        0.004, {2.0e-4, 1.0e-4, 5.0e-5});

    assert(result.entries.size() == 3);
    assert(result.entries[0].scheme == solvers::MpmTransferScheme::PIC);
    assert(result.entries[1].scheme == solvers::MpmTransferScheme::FLIP);
    assert(result.entries[2].scheme == solvers::MpmTransferScheme::APIC);

    for (const auto& entry : result.entries) {
        assert(entry.timestepSweep.levels.size() == 3);
        for (const auto& level : entry.timestepSweep.levels) {
            const auto& experiment = level.experiment;
            assert(std::isfinite(experiment.finalMechanicalEnergy));
            assert(std::isfinite(experiment.maximumRelativeMechanicalEnergyDrift));
            assert(experiment.minimumDeformationDeterminant > 0.0);
            assert(experiment.maximumUnaffectedRegionDrift == 0.0);
            assert(experiment.maximumMassConservationError < 1.0e-9);
            assert(experiment.maximumForceBalanceError < 1.0e-8);
        }
        assert(entry.timestepSweep.levels.back().particlePositionRmsToFinest == 0.0);
        assert(entry.timestepSweep.levels.back().gaussianPositionRmsToFinest == 0.0);
    }

    const auto& pic = result.entries[0].timestepSweep.levels.front().experiment.finalParticles;
    const auto& flip = result.entries[1].timestepSweep.levels.front().experiment.finalParticles;
    const auto& apic = result.entries[2].timestepSweep.levels.front().experiment.finalParticles;
    assert(particlePositionDifference(pic, apic) > 1.0e-10);
    assert(particlePositionDifference(flip, apic) > 1.0e-10);
    assert(particlePositionDifference(pic, flip) > 1.0e-10);

    assert(solvers::toString(solvers::MpmTransferScheme::PIC) == "PIC");
    assert(solvers::toString(solvers::MpmTransferScheme::FLIP) == "FLIP");
    assert(solvers::toString(solvers::MpmTransferScheme::APIC) == "APIC");
    return 0;
}
