#include "vulkax/research/deformable_world.hpp"

#include "vulkax/coupling/mpm_gaussian.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vulkax::research {
namespace {

using Matrix3 = solvers::Matrix3;

[[nodiscard]] constexpr std::size_t at(std::size_t row, std::size_t column) noexcept {
    return row * 3U + column;
}

[[nodiscard]] Matrix3 transpose(const Matrix3& matrix) noexcept {
    Matrix3 result{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            result[at(row, column)] = matrix[at(column, row)];
    return result;
}

[[nodiscard]] Matrix3 multiply(const Matrix3& lhs, const Matrix3& rhs) noexcept {
    Matrix3 result{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            for (std::size_t inner = 0; inner < 3; ++inner)
                result[at(row, column)] += lhs[at(row, inner)] * rhs[at(inner, column)];
    return result;
}

[[nodiscard]] math::Vec3 multiply(const Matrix3& matrix, math::Vec3 vector) noexcept {
    return {
        matrix[0] * vector.x + matrix[1] * vector.y + matrix[2] * vector.z,
        matrix[3] * vector.x + matrix[4] * vector.y + matrix[5] * vector.z,
        matrix[6] * vector.x + matrix[7] * vector.y + matrix[8] * vector.z,
    };
}

[[nodiscard]] double determinant(const Matrix3& matrix) noexcept {
    return matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
           matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
           matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
}

[[nodiscard]] Matrix3 quaternionRotation(const std::array<double, 4>& quaternion) noexcept {
    const double w = quaternion[0];
    const double x = quaternion[1];
    const double y = quaternion[2];
    const double z = quaternion[3];
    return {
        1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w),       2.0 * (x * z + y * w),
        2.0 * (x * y + z * w),       1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w),
        2.0 * (x * z - y * w),       2.0 * (y * z + x * w),       1.0 - 2.0 * (x * x + y * y),
    };
}

[[nodiscard]] Matrix3 covariance(const gaussian::GaussianSplat& splat) noexcept {
    const Matrix3 rotation = quaternionRotation(splat.rotation);
    Matrix3 variance{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double scale = std::exp(splat.logScale[axis]);
        variance[at(axis, axis)] = scale * scale;
    }
    return multiply(multiply(rotation, variance), transpose(rotation));
}

[[nodiscard]] double maximumAbsoluteDifference(const Matrix3& lhs, const Matrix3& rhs) noexcept {
    double result = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index)
        result = std::max(result, std::abs(lhs[index] - rhs[index]));
    return result;
}

[[nodiscard]] Matrix3 affineStepMatrix(const Matrix3& velocityGradient, double dt) noexcept {
    Matrix3 result = solvers::identityMatrix3();
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] += dt * velocityGradient[index];
    return result;
}

void updateMaximum(double& destination, double value) noexcept {
    destination = std::max(destination, value);
}

} // namespace

DeformableWorldExperimentResult runAffineDeformableWorldReference(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    const AffineDeformableWorldSettings& settings) {
    if (world.empty()) throw std::invalid_argument("deformable-world experiment requires a Gaussian world");
    if (particles.empty()) throw std::invalid_argument("deformable-world experiment requires MPM particles");
    if (activeGaussianIndices.empty()) throw std::invalid_argument("deformable-world experiment requires an active Gaussian region");
    if (settings.steps == 0) throw std::invalid_argument("deformable-world experiment requires at least one step");
    if (!std::isfinite(settings.dt) || settings.dt <= 0.0)
        throw std::invalid_argument("deformable-world experiment timestep must be positive");

    std::vector<bool> activeMask(world.size(), false);
    gaussian::GaussianCloud activeCloud;
    activeCloud.shRestCoefficientsPerSplat = world.shRestCoefficientsPerSplat;
    activeCloud.splats.reserve(activeGaussianIndices.size());
    for (const std::size_t index : activeGaussianIndices) {
        if (index >= world.size()) throw std::out_of_range("active Gaussian index is out of range");
        if (activeMask[index]) throw std::invalid_argument("active Gaussian indices must be unique");
        activeMask[index] = true;
        activeCloud.splats.push_back(world.splats[index]);
    }

    const gaussian::GaussianCloud restWorld = world;
    const gaussian::GaussianCloud restActiveCloud = activeCloud;
    const coupling::MpmGaussianBinding binding = coupling::bindGaussianCloudToMpm(
        activeCloud, particles, std::min<std::size_t>(12, particles.size()));

    solvers::MpmMaterial zeroStress;
    zeroStress.youngModulus = 0.0;
    zeroStress.poissonRatio = 0.3;

    const Matrix3 stepMatrix = affineStepMatrix(settings.velocityGradient, settings.dt);
    Matrix3 cumulativeLinear = solvers::identityMatrix3();
    math::Vec3 cumulativeTranslation{};

    DeformableWorldExperimentResult result;
    result.frames.reserve(settings.steps);

    for (std::size_t step = 1; step <= settings.steps; ++step) {
        // Reimpose the controlled affine field each frame. This makes the experiment
        // a transfer/coupling oracle rather than a constitutive-material benchmark.
        for (auto& particle : particles) {
            particle.affineVelocity = settings.velocityGradient;
            particle.velocity = settings.translationVelocity +
                                multiply(settings.velocityGradient, particle.position);
            particle.externalForce = {};
        }

        const auto mpmEvidence = solvers::stepMpm(
            particles, grid, zeroStress, settings.dt, {0.0, 0.0, 0.0});

        cumulativeTranslation = multiply(stepMatrix, cumulativeTranslation) +
                                settings.translationVelocity * settings.dt;
        cumulativeLinear = multiply(stepMatrix, cumulativeLinear);

        coupling::updateGaussianCloudFromMpm(binding, particles, activeCloud);
        for (std::size_t local = 0; local < activeGaussianIndices.size(); ++local)
            world.splats[activeGaussianIndices[local]] = activeCloud.splats[local];

        DeformableWorldFrameEvidence frame;
        frame.step = step;
        frame.time = static_cast<double>(step) * settings.dt;
        frame.massConservationError = mpmEvidence.transfer.massConservationError;
        frame.momentumConservationError = mpmEvidence.transfer.momentumConservationError;
        frame.forceBalanceError = mpmEvidence.transfer.forceBalanceError;
        frame.momentumBalanceError = mpmEvidence.momentumBalanceError;
        frame.minimumDeformationDeterminant = mpmEvidence.minimumDeformationDeterminant;
        frame.maximumDeformationDeterminant = mpmEvidence.maximumDeformationDeterminant;
        frame.expectedDeformationDeterminant = determinant(cumulativeLinear);

        for (const auto& particle : particles) {
            frame.deformationDeterminantError = std::max(
                frame.deformationDeterminantError,
                std::abs(solvers::deformationDeterminant(particle) - frame.expectedDeformationDeterminant));
        }

        for (std::size_t local = 0; local < activeCloud.size(); ++local) {
            const auto& restSplat = restActiveCloud.splats[local];
            const auto& actualSplat = activeCloud.splats[local];
            const math::Vec3 expectedPosition =
                multiply(cumulativeLinear, restSplat.position) + cumulativeTranslation;
            frame.maximumGaussianPositionError = std::max(
                frame.maximumGaussianPositionError,
                math::length(actualSplat.position - expectedPosition));

            const Matrix3 restCovariance = covariance(restSplat);
            const Matrix3 expectedCovariance = multiply(
                multiply(cumulativeLinear, restCovariance), transpose(cumulativeLinear));
            frame.maximumGaussianCovarianceError = std::max(
                frame.maximumGaussianCovarianceError,
                maximumAbsoluteDifference(covariance(actualSplat), expectedCovariance));
        }

        for (std::size_t index = 0; index < world.size(); ++index) {
            if (activeMask[index]) continue;
            frame.unaffectedPositionDrift = std::max(
                frame.unaffectedPositionDrift,
                math::length(world.splats[index].position - restWorld.splats[index].position));
            frame.unaffectedCovarianceDrift = std::max(
                frame.unaffectedCovarianceDrift,
                maximumAbsoluteDifference(covariance(world.splats[index]), covariance(restWorld.splats[index])));
        }
        frame.unaffectedRegionDrift = std::max(
            frame.unaffectedPositionDrift, frame.unaffectedCovarianceDrift);

        // Probe the reverse coupling without perturbing the controlled trajectory.
        const auto probe = coupling::applyGaussianForceToMpm(
            binding, 0, settings.interactionProbeForce, activeCloud, particles);
        frame.forceTransferError = probe.forceConservationError;
        frame.torqueTransferError = probe.torqueConservationError;
        for (auto& particle : particles) particle.externalForce = {};

        updateMaximum(result.maximumMassConservationError, frame.massConservationError);
        updateMaximum(result.maximumMomentumConservationError, frame.momentumConservationError);
        updateMaximum(result.maximumForceBalanceError, frame.forceBalanceError);
        updateMaximum(result.maximumMomentumBalanceError, frame.momentumBalanceError);
        updateMaximum(result.maximumDeformationDeterminantError, frame.deformationDeterminantError);
        updateMaximum(result.maximumGaussianPositionError, frame.maximumGaussianPositionError);
        updateMaximum(result.maximumGaussianCovarianceError, frame.maximumGaussianCovarianceError);
        updateMaximum(result.maximumForceTransferError, frame.forceTransferError);
        updateMaximum(result.maximumTorqueTransferError, frame.torqueTransferError);
        updateMaximum(result.maximumUnaffectedRegionDrift, frame.unaffectedRegionDrift);
        result.frames.push_back(frame);
    }

    result.finalWorld = std::move(world);
    result.finalParticles = std::move(particles);
    return result;
}

} // namespace vulkax::research
