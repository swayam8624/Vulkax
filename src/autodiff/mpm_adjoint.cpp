#include "vulkax/autodiff/mpm_adjoint.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vulkax::autodiff {
namespace {

using solvers::Matrix3;
using solvers::MpmGridNode;
using solvers::MpmGridSettings;
using solvers::MpmMaterial;
using solvers::MpmParticle;

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

[[nodiscard]] Matrix3 outer(math::Vec3 lhs, math::Vec3 rhs) noexcept {
    return {
        lhs.x * rhs.x, lhs.x * rhs.y, lhs.x * rhs.z,
        lhs.y * rhs.x, lhs.y * rhs.y, lhs.y * rhs.z,
        lhs.z * rhs.x, lhs.z * rhs.y, lhs.z * rhs.z,
    };
}

void add(Matrix3& target, const Matrix3& value) noexcept {
    for (std::size_t index = 0; index < target.size(); ++index) target[index] += value[index];
}

void addScaled(Matrix3& target, const Matrix3& value, double scale) noexcept {
    for (std::size_t index = 0; index < target.size(); ++index) target[index] += value[index] * scale;
}

[[nodiscard]] double frobenius(const Matrix3& lhs, const Matrix3& rhs) noexcept {
    double result = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) result += lhs[index] * rhs[index];
    return result;
}

[[nodiscard]] double trace(const Matrix3& matrix) noexcept {
    return matrix[0] + matrix[4] + matrix[8];
}

[[nodiscard]] double determinant(const Matrix3& matrix) noexcept {
    return matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
           matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
           matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
}

[[nodiscard]] Matrix3 inverseTranspose(const Matrix3& matrix) {
    const double j = determinant(matrix);
    if (!std::isfinite(j) || j <= 1.0e-12)
        throw std::runtime_error("MPM adjoint encountered a singular or inverted deformation");
    const double inverseJ = 1.0 / j;
    // Cofactor matrix divided by det(F) is F^{-T}.
    return {
        (matrix[4] * matrix[8] - matrix[5] * matrix[7]) * inverseJ,
        (matrix[5] * matrix[6] - matrix[3] * matrix[8]) * inverseJ,
        (matrix[3] * matrix[7] - matrix[4] * matrix[6]) * inverseJ,
        (matrix[2] * matrix[7] - matrix[1] * matrix[8]) * inverseJ,
        (matrix[0] * matrix[8] - matrix[2] * matrix[6]) * inverseJ,
        (matrix[1] * matrix[6] - matrix[0] * matrix[7]) * inverseJ,
        (matrix[1] * matrix[5] - matrix[2] * matrix[4]) * inverseJ,
        (matrix[2] * matrix[3] - matrix[0] * matrix[5]) * inverseJ,
        (matrix[0] * matrix[4] - matrix[1] * matrix[3]) * inverseJ,
    };
}

[[nodiscard]] Matrix3 identity() noexcept { return solvers::identityMatrix3(); }

struct AxisKernel {
    long long base{};
    std::array<double, 3> weight{};
    std::array<double, 3> derivative{};
    std::array<double, 3> secondDerivative{1.0, -2.0, 1.0};
};

[[nodiscard]] AxisKernel quadraticKernel(double gridCoordinate) {
    AxisKernel result;
    result.base = static_cast<long long>(std::floor(gridCoordinate - 0.5));
    const double fractional = gridCoordinate - static_cast<double>(result.base);
    result.weight = {
        0.5 * (1.5 - fractional) * (1.5 - fractional),
        0.75 - (fractional - 1.0) * (fractional - 1.0),
        0.5 * (fractional - 0.5) * (fractional - 0.5),
    };
    result.derivative = {
        fractional - 1.5,
        -2.0 * (fractional - 1.0),
        fractional - 0.5,
    };
    return result;
}

struct ParticleStencil {
    AxisKernel x;
    AxisKernel y;
    AxisKernel z;
};

[[nodiscard]] ParticleStencil stencilFor(const MpmParticle& particle, const MpmGridSettings& settings) {
    const math::Vec3 gridPosition = (particle.position - settings.origin) / settings.cellSize;
    ParticleStencil stencil{
        quadraticKernel(gridPosition.x),
        quadraticKernel(gridPosition.y),
        quadraticKernel(gridPosition.z),
    };
    const auto inside = [](long long base, std::size_t extent) {
        return base >= 0 && base + 2 < static_cast<long long>(extent);
    };
    if (!inside(stencil.x.base, settings.nx) ||
        !inside(stencil.y.base, settings.ny) ||
        !inside(stencil.z.base, settings.nz))
        throw std::out_of_range("MPM adjoint particle left the grid support");
    return stencil;
}

[[nodiscard]] double stencilKnotMargin(const MpmParticle& particle, const MpmGridSettings& settings) noexcept {
    const math::Vec3 gridPosition = (particle.position - settings.origin) / settings.cellSize;
    const auto axisMargin = [](double coordinate) {
        const double shifted = coordinate - 0.5;
        return std::abs(shifted - std::round(shifted));
    };
    return std::min({axisMargin(gridPosition.x), axisMargin(gridPosition.y), axisMargin(gridPosition.z)});
}

[[nodiscard]] std::size_t nodeIndex(
    std::size_t x,
    std::size_t y,
    std::size_t z,
    const MpmGridSettings& settings) noexcept {
    return (z * settings.ny + y) * settings.nx + x;
}

[[nodiscard]] math::Vec3 nodePosition(
    std::size_t x,
    std::size_t y,
    std::size_t z,
    const MpmGridSettings& settings) noexcept {
    return {
        settings.origin.x + settings.cellSize * static_cast<double>(x),
        settings.origin.y + settings.cellSize * static_cast<double>(y),
        settings.origin.z + settings.cellSize * static_cast<double>(z),
    };
}

template <typename Callback>
void visitStencil(
    const MpmParticle& particle,
    const MpmGridSettings& settings,
    const ParticleStencil& stencil,
    Callback&& callback) {
    const double inverseCell = 1.0 / settings.cellSize;
    const double inverseCellSquared = inverseCell * inverseCell;
    for (std::size_t localZ = 0; localZ < 3; ++localZ)
        for (std::size_t localY = 0; localY < 3; ++localY)
            for (std::size_t localX = 0; localX < 3; ++localX) {
                const std::size_t x = static_cast<std::size_t>(
                    stencil.x.base + static_cast<long long>(localX));
                const std::size_t y = static_cast<std::size_t>(
                    stencil.y.base + static_cast<long long>(localY));
                const std::size_t z = static_cast<std::size_t>(
                    stencil.z.base + static_cast<long long>(localZ));
                const double wx = stencil.x.weight[localX];
                const double wy = stencil.y.weight[localY];
                const double wz = stencil.z.weight[localZ];
                const double dx = stencil.x.derivative[localX];
                const double dy = stencil.y.derivative[localY];
                const double dz = stencil.z.derivative[localZ];
                const double weight = wx * wy * wz;
                const math::Vec3 gradient{
                    dx * wy * wz * inverseCell,
                    wx * dy * wz * inverseCell,
                    wx * wy * dz * inverseCell,
                };
                Matrix3 hessian{};
                hessian[0] = stencil.x.secondDerivative[localX] * wy * wz * inverseCellSquared;
                hessian[4] = wx * stencil.y.secondDerivative[localY] * wz * inverseCellSquared;
                hessian[8] = wx * wy * stencil.z.secondDerivative[localZ] * inverseCellSquared;
                hessian[1] = hessian[3] = dx * dy * wz * inverseCellSquared;
                hessian[2] = hessian[6] = dx * wy * dz * inverseCellSquared;
                hessian[5] = hessian[7] = wx * dy * dz * inverseCellSquared;
                const math::Vec3 offset = nodePosition(x, y, z, settings) - particle.position;
                callback(x, y, z, weight, gradient, hessian, offset);
            }
}

[[nodiscard]] Matrix3 kirchhoffStress(const MpmParticle& particle, const MpmMaterial& material) {
    const double j = determinant(particle.deformationGradient);
    if (!std::isfinite(j) || j <= 1.0e-12)
        throw std::runtime_error("MPM adjoint encountered a singular or inverted deformation");
    const double baseMu = material.youngModulus / (2.0 * (1.0 + material.poissonRatio));
    const double baseLambda = material.youngModulus * material.poissonRatio /
        ((1.0 + material.poissonRatio) * (1.0 - 2.0 * material.poissonRatio));
    const double mu = baseMu * particle.youngModulusScale;
    const double lambda = baseLambda * particle.youngModulusScale;
    const Matrix3 b = multiply(particle.deformationGradient, transpose(particle.deformationGradient));
    Matrix3 result{};
    const double logJ = std::log(j);
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = mu * (b[index] - (index % 4U == 0U ? 1.0 : 0.0));
    result[0] += lambda * logJ;
    result[4] += lambda * logJ;
    result[8] += lambda * logJ;
    return result;
}

struct ParticleAdjoint {
    math::Vec3 position{};
    math::Vec3 velocity{};
    Matrix3 deformationGradient{};
    Matrix3 affineVelocity{};
};

struct GridAdjoint {
    double mass{};
    math::Vec3 momentum{};
    math::Vec3 force{};
    math::Vec3 velocity{};
};

[[nodiscard]] std::vector<MpmGridNode> reconstructUpdatedGrid(
    const std::vector<MpmParticle>& particles,
    const MpmGridSettings& gridSettings,
    const MpmMaterial& material,
    double dt) {
    std::vector<MpmGridNode> grid;
    (void)solvers::particleToGridMpm(
        particles, gridSettings, material, grid, solvers::MpmTransferScheme::APIC, 0.0);
    for (auto& node : grid) {
        if (node.mass <= std::numeric_limits<double>::epsilon()) continue;
        node.velocity = node.momentum / node.mass + (node.force / node.mass) * dt;
    }
    return grid;
}

void reverseStress(
    const MpmParticle& particle,
    const MpmMaterial& material,
    const Matrix3& barTau,
    ParticleAdjoint& barParticle,
    double& barScale) {
    const Matrix3& f = particle.deformationGradient;
    const double j = determinant(f);
    if (!std::isfinite(j) || j <= 1.0e-12)
        throw std::runtime_error("MPM adjoint encountered a singular or inverted deformation");
    const double baseMu = material.youngModulus / (2.0 * (1.0 + material.poissonRatio));
    const double baseLambda = material.youngModulus * material.poissonRatio /
        ((1.0 + material.poissonRatio) * (1.0 - 2.0 * material.poissonRatio));
    const double mu = baseMu * particle.youngModulusScale;
    const double lambda = baseLambda * particle.youngModulusScale;

    Matrix3 symmetricBar = barTau;
    add(symmetricBar, transpose(barTau));
    addScaled(barParticle.deformationGradient, multiply(symmetricBar, f), mu);
    addScaled(
        barParticle.deformationGradient,
        inverseTranspose(f),
        lambda * trace(barTau));

    const Matrix3 b = multiply(f, transpose(f));
    Matrix3 bMinusIdentity = b;
    bMinusIdentity[0] -= 1.0;
    bMinusIdentity[4] -= 1.0;
    bMinusIdentity[8] -= 1.0;
    barScale += baseMu * frobenius(barTau, bMinusIdentity) +
                baseLambda * std::log(j) * trace(barTau);
}

} // namespace

MpmMaterialScaleAdjointResult differentiateMpmApicMaterialScales(
    std::vector<MpmParticle> initialParticles,
    const MpmGridSettings& grid,
    const MpmMaterial& material,
    double dt,
    std::size_t steps,
    std::size_t objectiveParticleIndex,
    math::Vec3 objectiveDirection) {
    if (initialParticles.empty() || steps == 0U || objectiveParticleIndex >= initialParticles.size())
        throw std::invalid_argument("invalid MPM material-scale adjoint request");
    if (!std::isfinite(dt) || dt <= 0.0)
        throw std::invalid_argument("MPM material-scale adjoint timestep must be positive");
    if (grid.boundaryCells != 0U)
        throw std::invalid_argument("MPM material-scale adjoint currently requires boundaryCells == 0");
    if (!(material.poissonRatio > -1.0 && material.poissonRatio < 0.5) ||
        !std::isfinite(material.youngModulus) || material.youngModulus < 0.0)
        throw std::invalid_argument("MPM material-scale adjoint material is invalid");
    const double directionLength = math::length(objectiveDirection);
    if (!std::isfinite(directionLength) || directionLength <= 1.0e-15)
        throw std::invalid_argument("MPM material-scale adjoint objective direction must be non-zero");
    objectiveDirection = objectiveDirection / directionLength;

    for (const auto& particle : initialParticles) {
        if (math::length(particle.externalForce) > 1.0e-15)
            throw std::invalid_argument("MPM material-scale adjoint currently requires zero external force");
        if (!std::isfinite(particle.youngModulusScale) || particle.youngModulusScale < 0.0)
            throw std::invalid_argument("MPM material-scale adjoint particle scale is invalid");
    }

    std::vector<std::vector<MpmParticle>> trajectory;
    trajectory.reserve(steps + 1U);
    trajectory.push_back(initialParticles);
    MpmMaterialScaleAdjointResult result;
    result.minimumStencilKnotMargin = 1.0;
    for (std::size_t step = 0; step < steps; ++step) {
        for (const auto& particle : trajectory.back())
            result.minimumStencilKnotMargin = std::min(
                result.minimumStencilKnotMargin, stencilKnotMargin(particle, grid));
        auto next = trajectory.back();
        (void)solvers::stepMpm(
            next, grid, material, dt, {}, solvers::MpmTransferScheme::APIC, 0.0);
        trajectory.push_back(std::move(next));
    }
    for (const auto& particle : trajectory.back())
        result.minimumStencilKnotMargin = std::min(
            result.minimumStencilKnotMargin, stencilKnotMargin(particle, grid));
    if (result.minimumStencilKnotMargin <= 1.0e-10)
        throw std::runtime_error("MPM material-scale adjoint trajectory lies on a spline stencil knot");

    result.objective = math::dot(
        trajectory.back()[objectiveParticleIndex].position -
            trajectory.front()[objectiveParticleIndex].position,
        objectiveDirection);
    result.particleScaleGradient.assign(initialParticles.size(), 0.0);

    std::vector<ParticleAdjoint> barAfter(initialParticles.size());
    barAfter[objectiveParticleIndex].position = objectiveDirection;

    const double affineFactor = 4.0 / (grid.cellSize * grid.cellSize);
    for (std::size_t reverse = 0; reverse < steps; ++reverse) {
        const std::size_t step = steps - 1U - reverse;
        const auto& before = trajectory[step];
        const auto& after = trajectory[step + 1U];
        const auto updatedGrid = reconstructUpdatedGrid(before, grid, material, dt);
        std::vector<GridAdjoint> barGrid(updatedGrid.size());
        std::vector<ParticleAdjoint> barBefore(before.size());

        // Reverse particle state update and APIC G2P.
        for (std::size_t particleIndex = 0; particleIndex < before.size(); ++particleIndex) {
            const auto& particleBefore = before[particleIndex];
            const auto& particleAfter = after[particleIndex];
            auto& barParticleBefore = barBefore[particleIndex];
            const auto& barParticleAfter = barAfter[particleIndex];

            barParticleBefore.position += barParticleAfter.position;
            math::Vec3 barVelocityOut = barParticleAfter.velocity + barParticleAfter.position * dt;
            Matrix3 barAffineOut = barParticleAfter.affineVelocity;

            Matrix3 update = identity();
            for (std::size_t index = 0; index < update.size(); ++index)
                update[index] += dt * particleAfter.affineVelocity[index];
            add(
                barParticleBefore.deformationGradient,
                multiply(transpose(update), barParticleAfter.deformationGradient));
            const Matrix3 barUpdate = multiply(
                barParticleAfter.deformationGradient,
                transpose(particleBefore.deformationGradient));
            addScaled(barAffineOut, barUpdate, dt);

            const ParticleStencil stencil = stencilFor(particleBefore, grid);
            visitStencil(
                particleBefore, grid, stencil,
                [&](std::size_t x,
                    std::size_t y,
                    std::size_t z,
                    double weight,
                    math::Vec3 gradient,
                    const Matrix3&,
                    math::Vec3 nodeOffset) {
                    const std::size_t index = nodeIndex(x, y, z, grid);
                    const auto& node = updatedGrid[index];
                    auto& barNode = barGrid[index];
                    const math::Vec3 barAffineTimesOffset = multiply(barAffineOut, nodeOffset);
                    barNode.velocity += barVelocityOut * weight +
                                        barAffineTimesOffset * (affineFactor * weight);
                    const double barWeight = math::dot(barVelocityOut, node.velocity) +
                        affineFactor * math::dot(node.velocity, barAffineTimesOffset);
                    const math::Vec3 barOffset =
                        multiply(transpose(barAffineOut), node.velocity) * (affineFactor * weight);
                    barParticleBefore.position += gradient * barWeight - barOffset;
                });
        }

        // Reverse grid update u = (momentum + dt * force) / mass.
        for (std::size_t index = 0; index < updatedGrid.size(); ++index) {
            const auto& node = updatedGrid[index];
            auto& barNode = barGrid[index];
            if (node.mass <= std::numeric_limits<double>::epsilon()) continue;
            barNode.momentum += barNode.velocity / node.mass;
            barNode.force += barNode.velocity * (dt / node.mass);
            barNode.mass -= math::dot(barNode.velocity, node.velocity) / node.mass;
        }

        // Reverse P2G and constitutive stress.
        for (std::size_t particleIndex = 0; particleIndex < before.size(); ++particleIndex) {
            const auto& particle = before[particleIndex];
            auto& barParticle = barBefore[particleIndex];
            const ParticleStencil stencil = stencilFor(particle, grid);
            const Matrix3 tau = kirchhoffStress(particle, material);
            Matrix3 barTau{};
            visitStencil(
                particle, grid, stencil,
                [&](std::size_t x,
                    std::size_t y,
                    std::size_t z,
                    double weight,
                    math::Vec3 gradient,
                    const Matrix3& hessian,
                    math::Vec3 nodeOffset) {
                    const std::size_t index = nodeIndex(x, y, z, grid);
                    const auto& barNode = barGrid[index];
                    const math::Vec3 transferVelocity =
                        particle.velocity + multiply(particle.affineVelocity, nodeOffset);

                    double barWeight = particle.mass * barNode.mass +
                        particle.mass * math::dot(barNode.momentum, transferVelocity);
                    const math::Vec3 barTransferVelocity =
                        barNode.momentum * (weight * particle.mass);
                    barParticle.velocity += barTransferVelocity;
                    add(barParticle.affineVelocity, outer(barTransferVelocity, nodeOffset));
                    const math::Vec3 barOffset =
                        multiply(transpose(particle.affineVelocity), barTransferVelocity);

                    addScaled(barTau, outer(barNode.force, gradient), -particle.restVolume);
                    const math::Vec3 barGradient =
                        multiply(transpose(tau), barNode.force) * (-particle.restVolume);

                    barParticle.position += gradient * barWeight - barOffset +
                                            multiply(hessian, barGradient);
                });
            reverseStress(
                particle,
                material,
                barTau,
                barParticle,
                result.particleScaleGradient[particleIndex]);
        }

        barAfter = std::move(barBefore);
    }

    return result;
}

} // namespace vulkax::autodiff
