#include "vulkax/coupling/mpm_gaussian.hpp"
#include "vulkax/solvers/mpm.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

vulkax::math::Vec3 matVec(const vulkax::solvers::Matrix3& matrix,
                          vulkax::math::Vec3 vector) {
    return {
        matrix[0] * vector.x + matrix[1] * vector.y + matrix[2] * vector.z,
        matrix[3] * vector.x + matrix[4] * vector.y + matrix[5] * vector.z,
        matrix[6] * vector.x + matrix[7] * vector.y + matrix[8] * vector.z,
    };
}

std::vector<vulkax::solvers::MpmParticle> makeBlock() {
    using namespace vulkax;
    std::vector<solvers::MpmParticle> particles;
    std::uint64_t id = 1;
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            for (int sz : {-1, 1}) {
                solvers::MpmParticle particle;
                particle.id = id++;
                particle.restPosition = {
                    0.15 * static_cast<double>(sx),
                    0.15 * static_cast<double>(sy),
                    0.15 * static_cast<double>(sz),
                };
                particle.position = particle.restPosition;
                particle.mass = 1.0;
                particle.restVolume = 1.0e-3;
                particles.push_back(particle);
            }
        }
    }
    return particles;
}

vulkax::gaussian::GaussianCloud makeGaussian(vulkax::math::Vec3 position = {}) {
    using namespace vulkax;
    gaussian::GaussianCloud cloud;
    gaussian::GaussianSplat splat;
    splat.position = position;
    splat.logScale = {std::log(0.12), std::log(0.05), std::log(0.08)};
    splat.rotation = {1.0, 0.0, 0.0, 0.0};
    splat.opacityLogit = 4.0;
    splat.shDC = {0.4, 0.1, -0.2};
    cloud.splats.push_back(splat);
    return cloud;
}

vulkax::solvers::MpmGridSettings makeGrid() {
    vulkax::solvers::MpmGridSettings settings;
    settings.origin = {-1.0, -1.0, -1.0};
    settings.nx = 21;
    settings.ny = 21;
    settings.nz = 21;
    settings.cellSize = 0.1;
    settings.boundaryCells = 0;
    return settings;
}

} // namespace

int main() {
    using namespace vulkax;

    const auto gridSettings = makeGrid();
    solvers::MpmMaterial zeroStress;
    zeroStress.youngModulus = 0.0;
    zeroStress.poissonRatio = 0.3;

    {
        auto particles = makeBlock();
        for (auto& particle : particles) particle.velocity = {0.3, -0.2, 0.1};
        std::vector<solvers::MpmGridNode> grid;
        const auto transfer = solvers::particleToGridMpm(particles, gridSettings, zeroStress, grid);
        assert(transfer.massConservationError < 1.0e-12);
        assert(transfer.momentumConservationError < 1.0e-12);
        assert(transfer.forceBalanceError < 1.0e-12);
    }

    {
        auto particles = makeBlock();
        auto cloud = makeGaussian();
        const auto binding = coupling::bindGaussianCloudToMpm(cloud, particles, particles.size());

        const solvers::Matrix3 affineVelocity{
            0.50, 0.0, 0.0,
            0.0, -0.25, 0.0,
            0.0, 0.0, 0.10,
        };
        const math::Vec3 translationVelocity{0.10, -0.02, 0.03};
        const double dt = 0.02;
        const auto initialParticles = particles;
        const auto initialScale = cloud.splats.front().linearScale();
        for (auto& particle : particles) {
            particle.affineVelocity = affineVelocity;
            particle.velocity = translationVelocity + matVec(affineVelocity, particle.position);
        }

        const auto evidence = solvers::stepMpm(particles, gridSettings, zeroStress, dt, {0.0, 0.0, 0.0});
        assert(evidence.transfer.massConservationError < 1.0e-12);
        assert(evidence.transfer.momentumConservationError < 1.0e-12);
        assert(evidence.transfer.forceBalanceError < 1.0e-12);
        assert(evidence.momentumBalanceError < 1.0e-12);

        double maximumPositionError = 0.0;
        for (std::size_t index = 0; index < particles.size(); ++index) {
            const auto expected = initialParticles[index].position +
                (translationVelocity + matVec(affineVelocity, initialParticles[index].position)) * dt;
            maximumPositionError = std::max(
                maximumPositionError, math::length(particles[index].position - expected));
        }
        assert(maximumPositionError < 1.0e-12);

        coupling::updateGaussianCloudFromMpm(binding, particles, cloud);
        const math::Vec3 expectedCenter = translationVelocity * dt;
        assert(math::length(cloud.splats.front().position - expectedCenter) < 1.0e-12);

        const auto deformedScale = cloud.splats.front().linearScale();
        assert(std::abs(deformedScale[0] - initialScale[0] * (1.0 + dt * 0.50)) < 1.0e-10);
        assert(std::abs(deformedScale[1] - initialScale[1] * (1.0 - dt * 0.25)) < 1.0e-10);
        assert(std::abs(deformedScale[2] - initialScale[2] * (1.0 + dt * 0.10)) < 1.0e-10);
    }

    {
        auto particles = makeBlock();
        auto cloud = makeGaussian({0.02, 0.01, 0.0});
        const auto binding = coupling::bindGaussianCloudToMpm(cloud, particles, particles.size());

        const math::Vec3 appliedForce{0.0, -8.0, 1.5};
        const auto transfer = coupling::applyGaussianForceToMpm(
            binding, 0, appliedForce, cloud, particles);
        assert(transfer.forceConservationError < 1.0e-12);
        assert(transfer.torqueConservationError < 1.0e-12);

        const double dt = 0.01;
        const auto step = solvers::stepMpm(
            particles, gridSettings, zeroStress, dt, {0.0, 0.0, 0.0});
        assert(step.momentumBalanceError < 1.0e-11);

        const math::Vec3 expectedMomentum = appliedForce * dt;
        assert(math::length(step.finalMomentum - expectedMomentum) < 1.0e-11);

        coupling::updateGaussianCloudFromMpm(binding, particles, cloud);
        assert(std::isfinite(cloud.splats.front().position.x));
        assert(std::isfinite(cloud.splats.front().position.y));
        assert(std::isfinite(cloud.splats.front().position.z));
    }

    {
        auto particle = makeBlock().front();
        particle.deformationGradient = {
            1.08, 0.0, 0.0,
            0.0, 0.97, 0.0,
            0.0, 0.0, 1.02,
        };
        std::vector<solvers::MpmParticle> particles{particle};
        solvers::MpmMaterial elastic;
        elastic.youngModulus = 5.0e4;
        elastic.poissonRatio = 0.3;

        const auto step = solvers::stepMpm(
            particles, gridSettings, elastic, 1.0e-4, {0.0, 0.0, 0.0});
        assert(step.transfer.forceBalanceError < 1.0e-9);
        assert(step.minimumDeformationDeterminant > 0.0);
        assert(std::isfinite(step.maximumDeformationDeterminant));
    }

    return 0;
}
