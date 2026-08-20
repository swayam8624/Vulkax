#include "vulkax/research/deformable_world.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

std::vector<vulkax::solvers::MpmParticle> makeBlock() {
    using namespace vulkax;
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

vulkax::gaussian::GaussianSplat makeSplat(
    vulkax::math::Vec3 position,
    double sx,
    double sy,
    double sz) {
    vulkax::gaussian::GaussianSplat splat;
    splat.position = position;
    splat.logScale = {std::log(sx), std::log(sy), std::log(sz)};
    splat.rotation = {1.0, 0.0, 0.0, 0.0};
    splat.opacityLogit = 4.0;
    splat.shDC = {0.3, 0.1, -0.1};
    return splat;
}

vulkax::gaussian::GaussianCloud makeWorld() {
    vulkax::gaussian::GaussianCloud world;
    world.splats.push_back(makeSplat({0.0, 0.0, 0.0}, 0.12, 0.055, 0.08));
    world.splats.push_back(makeSplat({0.055, -0.035, 0.025}, 0.07, 0.045, 0.035));
    // Locality control: deliberately outside the coupled MPM region.
    world.splats.push_back(makeSplat({0.72, 0.48, -0.22}, 0.09, 0.06, 0.04));
    return world;
}

vulkax::solvers::MpmGridSettings makeGrid() {
    vulkax::solvers::MpmGridSettings grid;
    grid.origin = {-1.2, -1.2, -1.2};
    grid.nx = 31;
    grid.ny = 31;
    grid.nz = 31;
    grid.cellSize = 0.08;
    grid.boundaryCells = 0;
    return grid;
}

} // namespace

int main() {
    using namespace vulkax;

    const auto initialWorld = makeWorld();
    research::AffineDeformableWorldSettings settings;
    settings.steps = 32;
    settings.dt = 0.01;
    settings.velocityGradient = {
         0.20,  0.05, 0.00,
        -0.03, -0.10, 0.04,
         0.00,  0.02, 0.06,
    };
    settings.translationVelocity = {0.08, -0.03, 0.02};
    settings.interactionProbeForce = {1.5, -2.0, 0.75};

    const auto result = research::runAffineDeformableWorldReference(
        initialWorld, {0, 1}, makeBlock(), makeGrid(), settings);

    assert(result.frames.size() == settings.steps);
    assert(result.finalParticles.size() == 8);
    assert(result.finalWorld.size() == initialWorld.size());

    assert(result.maximumMassConservationError < 1.0e-10);
    assert(result.maximumMomentumConservationError < 1.0e-10);
    assert(result.maximumForceBalanceError < 1.0e-10);
    assert(result.maximumMomentumBalanceError < 1.0e-10);
    assert(result.maximumDeformationDeterminantError < 1.0e-9);
    assert(result.maximumGaussianPositionError < 1.0e-9);
    assert(result.maximumGaussianCovarianceError < 1.0e-9);
    assert(result.maximumForceTransferError < 1.0e-10);
    assert(result.maximumTorqueTransferError < 1.0e-10);

    // The locality-control Gaussian must never be touched by the coupled region.
    assert(result.maximumUnaffectedRegionDrift == 0.0);
    assert(math::length(result.finalWorld.splats[2].position - initialWorld.splats[2].position) == 0.0);

    assert(std::abs(result.frames.front().time - settings.dt) < 1.0e-15);
    assert(std::abs(result.frames.back().time -
                    static_cast<double>(settings.steps) * settings.dt) < 1.0e-15);

    assert(math::length(result.finalWorld.splats[0].position - initialWorld.splats[0].position) > 1.0e-3);
    assert(math::length(result.finalWorld.splats[1].position - initialWorld.splats[1].position) > 1.0e-3);

    for (const auto& frame : result.frames) {
        assert(std::isfinite(frame.expectedDeformationDeterminant));
        assert(frame.expectedDeformationDeterminant > 0.0);
        assert(frame.minimumDeformationDeterminant > 0.0);
        assert(frame.maximumDeformationDeterminant > 0.0);
        assert(frame.unaffectedRegionDrift == 0.0);
    }

    return 0;
}
