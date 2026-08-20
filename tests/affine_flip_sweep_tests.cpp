#include "vulkax/research/affine_flip_sweep.hpp"

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

vulkax::gaussian::GaussianSplat makeSplat(vulkax::math::Vec3 position) {
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
    world.splats.push_back(makeSplat({-0.08, 0.04, 0.02}));
    world.splats.push_back(makeSplat({0.09, -0.06, 0.03}));
    world.splats.push_back(makeSplat({0.04, 0.10, -0.07}));
    world.splats.push_back(makeSplat({-0.05, -0.08, -0.06}));
    world.splats.push_back(makeSplat({5.0, 5.0, 5.0}));
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

    constexpr double horizon = 0.016;
    constexpr double dt = 1.0e-4;
    const auto result = research::runAffineFlipBlendSweep(
        makeWorld(), {0, 1, 2, 3}, makeBody(), makeGrid(), settings,
        horizon, dt, {0.0, 0.5, 1.0}, 0.01);

    assert(result.entries.size() == 5);
    assert(result.entries.front().scheme == solvers::MpmTransferScheme::APIC);
    assert(result.entries[1].scheme == solvers::MpmTransferScheme::APIC_FLIP);
    assert(result.entries[1].flipBlend == 0.0);
    assert(result.entries[2].flipBlend == 0.5);
    assert(result.entries[3].flipBlend == 1.0);
    assert(result.entries.back().scheme == solvers::MpmTransferScheme::FLIP);

    // The beta=0 affine-FLIP endpoint must reproduce APIC exactly to numerical precision.
    assert(result.entries[1].particlePositionRmsToApic < 1.0e-12);
    assert(result.entries[1].particleVelocityRmsToApic < 1.0e-12);
    assert(result.entries[1].gaussianPositionRmsToApic < 1.0e-12);

    for (const auto& entry : result.entries) {
        const auto& experiment = entry.cycle.experiment;
        assert(experiment.frames.size() == 160);
        assert(std::isfinite(entry.cycle.finalMechanicalEnergyFraction));
        assert(std::isfinite(experiment.maximumMlsRmsResidual));
        assert(std::isfinite(experiment.maximumGaussianDisplacement));
        assert(experiment.minimumDeformationDeterminant > 0.0);
        assert(experiment.maximumUnaffectedRegionDrift == 0.0);
        assert(experiment.maximumMassConservationError < 1.0e-9);
        assert(experiment.maximumForceBalanceError < 1.0e-8);
    }

    // Intermediate affine-FLIP and pure FLIP must not silently collapse to APIC.
    assert(result.entries[2].particlePositionRmsToApic > 1.0e-12);
    assert(result.entries[2].gaussianPositionRmsToApic > 1.0e-12);
    assert(result.entries.back().particlePositionRmsToApic > 1.0e-12);

    assert(solvers::toString(solvers::MpmTransferScheme::APIC_FLIP) == "APIC-FLIP");
    return 0;
}
