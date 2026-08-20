#include "vulkax/research/transfer_diagnostics.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

std::vector<vulkax::solvers::MpmParticle> makeBody() {
    using namespace vulkax;
    std::vector<solvers::MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double spacing = 0.12;
    constexpr double volume = spacing * spacing * spacing;
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                solvers::MpmParticle p;
                p.id = id++;
                p.restPosition = {(x - 1.5) * spacing, (y - 1.5) * spacing, (z - 1.5) * spacing};
                p.position = p.restPosition;
                p.restVolume = volume;
                p.mass = 1000.0 * volume;
                particles.push_back(p);
            }
    return particles;
}

vulkax::gaussian::GaussianSplat splat(vulkax::math::Vec3 p) {
    vulkax::gaussian::GaussianSplat s;
    s.position = p;
    s.logScale = {std::log(0.06), std::log(0.05), std::log(0.04)};
    s.rotation = {1.0, 0.0, 0.0, 0.0};
    s.opacityLogit = 4.0;
    s.shDC = {0.2, 0.0, -0.1};
    return s;
}

vulkax::gaussian::GaussianCloud makeWorld() {
    vulkax::gaussian::GaussianCloud world;
    world.splats = {
        splat({-0.08, 0.04, 0.02}), splat({0.09, -0.06, 0.03}),
        splat({0.04, 0.10, -0.07}), splat({-0.05, -0.08, -0.06}),
        splat({5.0, 5.0, 5.0})};
    return world;
}

vulkax::solvers::MpmGridSettings makeGrid() {
    vulkax::solvers::MpmGridSettings grid;
    grid.origin = {-1.0, -1.0, -1.0};
    grid.nx = grid.ny = grid.nz = 26;
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

    const auto result = research::runMpmTransferDiagnostics(
        makeWorld(), {0, 1, 2, 3}, makeBody(), makeGrid(), settings,
        0.0008, {2.0e-4, 1.0e-4, 5.0e-5, 2.5e-5});

    assert(result.ablation.entries.size() == 3);
    assert(result.schemes.size() == 3);
    assert(result.finestPairDifferences.size() == 3);

    for (const auto& scheme : result.schemes) {
        assert(scheme.finestDt == 2.5e-5);
        assert(std::isfinite(scheme.finestRelativeEnergyDrift));
        assert(std::isfinite(scheme.peakKineticEnergyFraction));
        assert(std::isfinite(scheme.finalKineticEnergyFraction));
        assert(std::isfinite(scheme.finalElasticEnergyFraction));
        assert(scheme.finestMinimumDeformationDeterminant > 0.0);
        assert(scheme.finestGaussianDisplacement >= 0.0);
        if (scheme.coarseFloor.valid) {
            assert(std::isfinite(scheme.coarseFloor.observedOrder));
            assert(std::isfinite(scheme.coarseFloor.asymptoticRelativeEnergyDrift));
        }
        if (scheme.fineFloor.valid) {
            assert(std::isfinite(scheme.fineFloor.observedOrder));
            assert(std::isfinite(scheme.fineFloor.asymptoticRelativeEnergyDrift));
        }
    }

    for (const auto& entry : result.ablation.entries) {
        assert(entry.timestepSweep.levels.size() == 4);
        assert(entry.timestepSweep.levels.back().experiment.maximumUnaffectedRegionDrift == 0.0);
    }

    for (const auto& pair : result.finestPairDifferences) {
        assert(std::isfinite(pair.particlePositionRms));
        assert(std::isfinite(pair.particleVelocityRms));
        assert(std::isfinite(pair.gaussianPositionRms));
        assert(pair.particlePositionRms > 1.0e-12);
        assert(pair.gaussianPositionRms > 1.0e-12);
    }
    return 0;
}
