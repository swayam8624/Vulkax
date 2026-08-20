#include "vulkax/research/energy_cycle.hpp"

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

    constexpr double horizon = 0.008;
    constexpr double dt = 1.0e-4;
    const auto result = research::runTransferEnergyCycleDiagnostic(
        makeWorld(), {0, 1, 2, 3}, makeBody(), makeGrid(), settings, horizon, dt, 0.01);

    assert(result.schemes.size() == 3);
    assert(result.dt == dt);
    assert(result.physicalHorizon == horizon);
    for (const auto& scheme : result.schemes) {
        assert(scheme.experiment.frames.size() == 80);
        assert(std::isfinite(scheme.finalMechanicalEnergyFraction));
        assert(std::isfinite(scheme.minimumMechanicalEnergyFraction));
        assert(std::isfinite(scheme.maximumMechanicalEnergyFraction));
        assert(std::isfinite(scheme.peakKineticEnergyFraction));
        assert(scheme.minimumMechanicalEnergyFraction > 0.0);
        assert(scheme.maximumMechanicalEnergyFraction > 0.0);
        assert(scheme.experiment.minimumDeformationDeterminant > 0.0);
        assert(scheme.experiment.maximumUnaffectedRegionDrift == 0.0);
        assert(scheme.experiment.maximumMassConservationError < 1.0e-9);
        assert(scheme.meaningfulKineticPeakCount <= scheme.kineticPeaks.size());
        if (scheme.meaningfulKineticPeakCount >= 2) {
            assert(scheme.completedMeaningfulCycles == scheme.meaningfulKineticPeakCount - 1);
            assert(scheme.meanMeaningfulCyclePeriod > 0.0);
            assert(scheme.peakToPeakMechanicalEnergyRetention > 0.0);
            assert(scheme.meanMechanicalEnergyRetentionPerCycle > 0.0);
            assert(scheme.peakToPeakKineticAmplitudeRetention > 0.0);
            assert(scheme.meanKineticAmplitudeRetentionPerCycle > 0.0);
        } else {
            assert(scheme.completedMeaningfulCycles == 0);
            assert(scheme.meanMeaningfulCyclePeriod == 0.0);
        }
    }

    const auto& pic = result.schemes[0].experiment.finalParticles;
    const auto& flip = result.schemes[1].experiment.finalParticles;
    const auto& apic = result.schemes[2].experiment.finalParticles;
    assert(particlePositionDifference(pic, flip) > 1.0e-12);
    assert(particlePositionDifference(pic, apic) > 1.0e-12);
    assert(particlePositionDifference(flip, apic) > 1.0e-12);
    return 0;
}
