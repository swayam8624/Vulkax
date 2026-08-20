#include "vulkax/coupling/mpm_gaussian.hpp"
#include "vulkax/solvers/mpm.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

using Matrix3 = vulkax::solvers::Matrix3;

vulkax::math::Vec3 matVec(const Matrix3& matrix,
                          vulkax::math::Vec3 vector) {
    return {
        matrix[0] * vector.x + matrix[1] * vector.y + matrix[2] * vector.z,
        matrix[3] * vector.x + matrix[4] * vector.y + matrix[5] * vector.z,
        matrix[6] * vector.x + matrix[7] * vector.y + matrix[8] * vector.z,
    };
}

Matrix3 transpose(const Matrix3& matrix) {
    return {
        matrix[0], matrix[3], matrix[6],
        matrix[1], matrix[4], matrix[7],
        matrix[2], matrix[5], matrix[8],
    };
}

Matrix3 matMul(const Matrix3& lhs, const Matrix3& rhs) {
    Matrix3 result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t inner = 0; inner < 3; ++inner) {
                result[row * 3 + column] +=
                    lhs[row * 3 + inner] * rhs[inner * 3 + column];
            }
        }
    }
    return result;
}

Matrix3 quaternionRotation(const std::array<double, 4>& quaternion) {
    const double w = quaternion[0];
    const double x = quaternion[1];
    const double y = quaternion[2];
    const double z = quaternion[3];
    return {
        1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w),
        2.0 * (x * z + y * w),
        2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z),
        2.0 * (y * z - x * w),
        2.0 * (x * z - y * w), 2.0 * (y * z + x * w),
        1.0 - 2.0 * (x * x + y * y),
    };
}

Matrix3 gaussianCovariance(const vulkax::gaussian::GaussianSplat& splat) {
    const Matrix3 rotation = quaternionRotation(splat.rotation);
    Matrix3 variance{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double scale = std::exp(splat.logScale[axis]);
        variance[axis * 3 + axis] = scale * scale;
    }
    return matMul(matMul(rotation, variance), transpose(rotation));
}

double maximumAbsoluteDifference(const Matrix3& lhs, const Matrix3& rhs) {
    double maximum = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index)
        maximum = std::max(maximum, std::abs(lhs[index] - rhs[index]));
    return maximum;
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
        const Matrix3 initialCovariance = gaussianCovariance(cloud.splats.front());
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

        // Gaussian scale-axis order is not a physical invariant: covariance
        // eigendecomposition may permute principal axes while compensating in
        // the stored quaternion. Compare the represented covariance instead.
        const Matrix3 deformation{
            1.0 + dt * 0.50, 0.0, 0.0,
            0.0, 1.0 - dt * 0.25, 0.0,
            0.0, 0.0, 1.0 + dt * 0.10,
        };
        const Matrix3 expectedCovariance =
            matMul(matMul(deformation, initialCovariance), transpose(deformation));
        const Matrix3 actualCovariance = gaussianCovariance(cloud.splats.front());
        assert(maximumAbsoluteDifference(actualCovariance, expectedCovariance) < 1.0e-10);
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
