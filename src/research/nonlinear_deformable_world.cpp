#include "vulkax/research/nonlinear_deformable_world.hpp"

#include "vulkax/coupling/mpm_gaussian.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

namespace vulkax::research {
namespace {

using Matrix3 = solvers::Matrix3;
using Vector4 = std::array<double, 4>;
using Matrix4 = std::array<std::array<double, 4>, 4>;

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

[[nodiscard]] double kineticEnergy(const std::vector<solvers::MpmParticle>& particles) noexcept {
    double result = 0.0;
    for (const auto& particle : particles)
        result += 0.5 * particle.mass * math::dot(particle.velocity, particle.velocity);
    return result;
}

[[nodiscard]] double neoHookeanEnergyDensity(
    const Matrix3& deformationGradient,
    const solvers::MpmMaterial& material) {
    if (material.youngModulus < 0.0)
        throw std::invalid_argument("nonlinear experiment Young's modulus cannot be negative");
    if (!(material.poissonRatio > -1.0 && material.poissonRatio < 0.5))
        throw std::invalid_argument("nonlinear experiment Poisson ratio must lie in (-1, 0.5)");
    const double j = determinant(deformationGradient);
    if (!std::isfinite(j) || j <= 1.0e-12)
        throw std::runtime_error("nonlinear experiment encountered an inverted deformation");
    const double mu = material.youngModulus / (2.0 * (1.0 + material.poissonRatio));
    const double lambda = material.youngModulus * material.poissonRatio /
        ((1.0 + material.poissonRatio) * (1.0 - 2.0 * material.poissonRatio));
    double i1 = 0.0;
    for (const double entry : deformationGradient) i1 += entry * entry;
    const double logJ = std::log(j);
    return 0.5 * mu * (i1 - 3.0) - mu * logJ + 0.5 * lambda * logJ * logJ;
}

[[nodiscard]] double elasticEnergy(
    const std::vector<solvers::MpmParticle>& particles,
    const solvers::MpmMaterial& material) {
    double result = 0.0;
    for (const auto& particle : particles)
        result += particle.restVolume * neoHookeanEnergyDensity(particle.deformationGradient, material);
    return result;
}

[[nodiscard]] math::Vec3 centerOfMass(const std::vector<solvers::MpmParticle>& particles) {
    double mass = 0.0;
    math::Vec3 weighted{};
    for (const auto& particle : particles) {
        mass += particle.mass;
        weighted += particle.position * particle.mass;
    }
    if (!(mass > 0.0)) throw std::invalid_argument("nonlinear experiment requires positive particle mass");
    return weighted / mass;
}

[[nodiscard]] Vector4 basis(math::Vec3 position) noexcept {
    return {1.0, position.x, position.y, position.z};
}

[[nodiscard]] Vector4 solve4(Matrix4 matrix, Vector4 rhs) {
    constexpr double tolerance = 1.0e-13;
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        double magnitude = std::abs(matrix[pivot][column]);
        for (std::size_t row = column + 1; row < 4; ++row) {
            const double candidate = std::abs(matrix[row][column]);
            if (candidate > magnitude) {
                magnitude = candidate;
                pivot = row;
            }
        }
        if (magnitude < tolerance) throw std::runtime_error("nonlinear MLS support became rank deficient");
        if (pivot != column) {
            std::swap(matrix[pivot], matrix[column]);
            std::swap(rhs[pivot], rhs[column]);
        }
        const double inversePivot = 1.0 / matrix[column][column];
        for (std::size_t entry = column; entry < 4; ++entry) matrix[column][entry] *= inversePivot;
        rhs[column] *= inversePivot;
        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            for (std::size_t entry = column; entry < 4; ++entry)
                matrix[row][entry] -= factor * matrix[column][entry];
            rhs[row] -= factor * rhs[column];
        }
    }
    return rhs;
}

struct FitResidual {
    double rms{};
    double maximum{};
};

[[nodiscard]] FitResidual supportResidual(
    const coupling::GaussianSupport& support,
    const std::vector<coupling::PhysicalPoint>& points) {
    Matrix4 moment{};
    Vector4 rhsX{};
    Vector4 rhsY{};
    Vector4 rhsZ{};
    for (const auto& weighted : support.fitWeights) {
        if (weighted.physicalIndex >= points.size()) throw std::out_of_range("nonlinear MLS support index is invalid");
        const auto& point = points[weighted.physicalIndex];
        const auto b = basis(point.restPosition);
        for (std::size_t row = 0; row < 4; ++row) {
            rhsX[row] += weighted.weight * b[row] * point.position.x;
            rhsY[row] += weighted.weight * b[row] * point.position.y;
            rhsZ[row] += weighted.weight * b[row] * point.position.z;
            for (std::size_t column = 0; column < 4; ++column)
                moment[row][column] += weighted.weight * b[row] * b[column];
        }
    }
    const auto cx = solve4(moment, rhsX);
    const auto cy = solve4(moment, rhsY);
    const auto cz = solve4(moment, rhsZ);
    double weightedSquared = 0.0;
    double weightSum = 0.0;
    FitResidual result;
    for (const auto& weighted : support.fitWeights) {
        const auto& point = points[weighted.physicalIndex];
        const auto b = basis(point.restPosition);
        const math::Vec3 predicted{
            cx[0] + cx[1] * b[1] + cx[2] * b[2] + cx[3] * b[3],
            cy[0] + cy[1] * b[1] + cy[2] * b[2] + cy[3] * b[3],
            cz[0] + cz[1] * b[1] + cz[2] * b[2] + cz[3] * b[3],
        };
        const double error = math::length(predicted - point.position);
        result.maximum = std::max(result.maximum, error);
        weightedSquared += weighted.weight * error * error;
        weightSum += weighted.weight;
    }
    result.rms = std::sqrt(weightedSquared / std::max(weightSum, 1.0e-30));
    return result;
}

[[nodiscard]] double gaussianStateDifference(
    const gaussian::GaussianSplat& lhs,
    const gaussian::GaussianSplat& rhs) noexcept {
    double result = math::length(lhs.position - rhs.position);
    for (std::size_t axis = 0; axis < 3; ++axis)
        result = std::max(result, std::abs(lhs.logScale[axis] - rhs.logScale[axis]));
    double sameSign = 0.0;
    double oppositeSign = 0.0;
    for (std::size_t component = 0; component < 4; ++component) {
        const double a = lhs.rotation[component] - rhs.rotation[component];
        const double b = lhs.rotation[component] + rhs.rotation[component];
        sameSign += a * a;
        oppositeSign += b * b;
    }
    result = std::max(result, std::sqrt(std::min(sameSign, oppositeSign)));
    return result;
}

void updateMaximum(double& target, double value) noexcept { target = std::max(target, value); }

} // namespace

NonlinearDeformableWorldResult runNonlinearDeformableWorld(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    const NonlinearDeformableWorldSettings& settings,
    const NonlinearDeformableWorldObserver& observer) {
    if (world.empty() || activeGaussianIndices.empty() || particles.size() < 4)
        throw std::invalid_argument("nonlinear deformable-world experiment has insufficient state");
    if (settings.steps == 0 || !std::isfinite(settings.dt) || settings.dt <= 0.0)
        throw std::invalid_argument("nonlinear deformable-world integration settings are invalid");
    if (settings.couplingNeighborCount < 4)
        throw std::invalid_argument("nonlinear deformable-world coupling needs at least four neighbors");
    if (determinant(settings.initialDeformation) <= 0.0)
        throw std::invalid_argument("nonlinear deformable-world initial deformation must preserve orientation");

    gaussian::GaussianCloud activeCloud;
    activeCloud.shRestCoefficientsPerSplat = world.shRestCoefficientsPerSplat;
    std::vector<bool> activeMask(world.size(), false);
    for (const std::size_t index : activeGaussianIndices) {
        if (index >= world.size() || activeMask[index])
            throw std::invalid_argument("nonlinear deformable-world active Gaussian indices are invalid");
        activeMask[index] = true;
        activeCloud.splats.push_back(world.splats[index]);
    }
    const gaussian::GaussianCloud localityReference = world;

    for (auto& particle : particles) {
        particle.position = multiply(settings.initialDeformation, particle.restPosition);
        particle.deformationGradient = settings.initialDeformation;
        particle.velocity = {};
        particle.affineVelocity = {};
        particle.externalForce = {};
    }

    const auto binding = coupling::bindGaussianCloudToMpm(
        activeCloud, particles,
        std::min(settings.couplingNeighborCount, particles.size()));
    coupling::updateGaussianCloudFromMpm(binding, particles, activeCloud);
    for (std::size_t local = 0; local < activeGaussianIndices.size(); ++local)
        world.splats[activeGaussianIndices[local]] = activeCloud.splats[local];
    const gaussian::GaussianCloud initialActiveCloud = activeCloud;
    const math::Vec3 initialCenterOfMass = centerOfMass(particles);

    NonlinearDeformableWorldResult result;
    result.frames.reserve(settings.steps);
    result.minimumDeformationDeterminant = std::numeric_limits<double>::infinity();
    const double initialKinetic = kineticEnergy(particles);
    const double initialElastic = elasticEnergy(particles, settings.material);
    result.initialMechanicalEnergy = initialKinetic + initialElastic;
    if (!(result.initialMechanicalEnergy > 0.0) || !std::isfinite(result.initialMechanicalEnergy))
        throw std::runtime_error("nonlinear deformable-world initial mechanical energy is invalid");

    for (std::size_t step = 1; step <= settings.steps; ++step) {
        const auto mpm = solvers::stepMpm(
            particles, grid, settings.material, settings.dt, {}, settings.transferScheme, settings.flipBlend);
        coupling::updateGaussianCloudFromMpm(binding, particles, activeCloud);
        for (std::size_t local = 0; local < activeGaussianIndices.size(); ++local)
            world.splats[activeGaussianIndices[local]] = activeCloud.splats[local];

        NonlinearDeformableWorldFrameEvidence frame;
        frame.step = step;
        frame.time = static_cast<double>(step) * settings.dt;
        frame.massConservationError = mpm.transfer.massConservationError;
        frame.momentumConservationError = mpm.transfer.momentumConservationError;
        frame.forceBalanceError = mpm.transfer.forceBalanceError;
        frame.momentumBalanceError = mpm.momentumBalanceError;
        frame.minimumDeformationDeterminant = mpm.minimumDeformationDeterminant;
        frame.maximumDeformationDeterminant = mpm.maximumDeformationDeterminant;
        frame.kineticEnergy = kineticEnergy(particles);
        frame.elasticEnergy = elasticEnergy(particles, settings.material);
        frame.mechanicalEnergy = frame.kineticEnergy + frame.elasticEnergy;
        frame.relativeMechanicalEnergyDrift =
            std::abs(frame.mechanicalEnergy - result.initialMechanicalEnergy) / result.initialMechanicalEnergy;
        frame.centerOfMassDrift = math::length(centerOfMass(particles) - initialCenterOfMass);

        const auto physicalPoints = coupling::mpmPhysicalPoints(particles);
        for (const auto& support : binding.embedding.supports) {
            const auto residual = supportResidual(support, physicalPoints);
            frame.maximumMlsRmsResidual = std::max(frame.maximumMlsRmsResidual, residual.rms);
            frame.maximumMlsResidual = std::max(frame.maximumMlsResidual, residual.maximum);
        }
        for (std::size_t local = 0; local < activeCloud.size(); ++local)
            frame.maximumGaussianDisplacement = std::max(
                frame.maximumGaussianDisplacement,
                math::length(activeCloud.splats[local].position - initialActiveCloud.splats[local].position));
        for (std::size_t index = 0; index < world.size(); ++index) {
            if (activeMask[index]) continue;
            frame.unaffectedRegionDrift = std::max(
                frame.unaffectedRegionDrift,
                gaussianStateDifference(world.splats[index], localityReference.splats[index]));
        }

        updateMaximum(result.maximumMassConservationError, frame.massConservationError);
        updateMaximum(result.maximumMomentumConservationError, frame.momentumConservationError);
        updateMaximum(result.maximumForceBalanceError, frame.forceBalanceError);
        updateMaximum(result.maximumMomentumBalanceError, frame.momentumBalanceError);
        result.minimumDeformationDeterminant = std::min(
            result.minimumDeformationDeterminant, frame.minimumDeformationDeterminant);
        updateMaximum(result.maximumDeformationDeterminant, frame.maximumDeformationDeterminant);
        updateMaximum(result.maximumRelativeMechanicalEnergyDrift, frame.relativeMechanicalEnergyDrift);
        updateMaximum(result.maximumCenterOfMassDrift, frame.centerOfMassDrift);
        updateMaximum(result.maximumMlsRmsResidual, frame.maximumMlsRmsResidual);
        updateMaximum(result.maximumMlsResidual, frame.maximumMlsResidual);
        updateMaximum(result.maximumGaussianDisplacement, frame.maximumGaussianDisplacement);
        updateMaximum(result.maximumUnaffectedRegionDrift, frame.unaffectedRegionDrift);
        result.frames.push_back(frame);
        if (observer) observer(result.frames.back(), world);
    }

    result.finalMechanicalEnergy = result.frames.back().mechanicalEnergy;
    result.finalWorld = std::move(world);
    result.finalParticles = std::move(particles);
    return result;
}

void writeNonlinearDeformableWorldEvidenceCsv(
    const NonlinearDeformableWorldResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open nonlinear deformable-world CSV output");
    stream << "step,time,mass_error,momentum_error,force_balance_error,momentum_balance_error,"
              "min_J,max_J,kinetic_energy,elastic_energy,mechanical_energy,relative_energy_drift,"
              "center_of_mass_drift,max_mls_rms_residual,max_mls_residual,"
              "max_gaussian_displacement,unaffected_region_drift\n";
    stream << std::setprecision(17);
    for (const auto& frame : result.frames) {
        stream << frame.step << ',' << frame.time << ','
               << frame.massConservationError << ',' << frame.momentumConservationError << ','
               << frame.forceBalanceError << ',' << frame.momentumBalanceError << ','
               << frame.minimumDeformationDeterminant << ',' << frame.maximumDeformationDeterminant << ','
               << frame.kineticEnergy << ',' << frame.elasticEnergy << ',' << frame.mechanicalEnergy << ','
               << frame.relativeMechanicalEnergyDrift << ',' << frame.centerOfMassDrift << ','
               << frame.maximumMlsRmsResidual << ',' << frame.maximumMlsResidual << ','
               << frame.maximumGaussianDisplacement << ',' << frame.unaffectedRegionDrift << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing nonlinear deformable-world CSV output");
}

} // namespace vulkax::research
