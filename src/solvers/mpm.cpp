#include "vulkax/solvers/mpm.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vulkax::solvers {
namespace {

[[nodiscard]] constexpr std::size_t at(std::size_t row, std::size_t column) noexcept { return row * 3U + column; }

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
    return {matrix[0] * vector.x + matrix[1] * vector.y + matrix[2] * vector.z,
            matrix[3] * vector.x + matrix[4] * vector.y + matrix[5] * vector.z,
            matrix[6] * vector.x + matrix[7] * vector.y + matrix[8] * vector.z};
}

[[nodiscard]] Matrix3 outer(math::Vec3 lhs, math::Vec3 rhs) noexcept {
    return {lhs.x * rhs.x, lhs.x * rhs.y, lhs.x * rhs.z,
            lhs.y * rhs.x, lhs.y * rhs.y, lhs.y * rhs.z,
            lhs.z * rhs.x, lhs.z * rhs.y, lhs.z * rhs.z};
}

void addScaled(Matrix3& target, const Matrix3& value, double scale) noexcept {
    for (std::size_t index = 0; index < target.size(); ++index) target[index] += value[index] * scale;
}

[[nodiscard]] double determinant(const Matrix3& matrix) noexcept {
    return matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
           matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
           matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
}

[[nodiscard]] Matrix3 inverse(const Matrix3& matrix) {
    const double determinantValue = determinant(matrix);
    if (!std::isfinite(determinantValue) || determinantValue <= 1.0e-12)
        throw std::runtime_error("MPM deformation gradient became singular or inverted");
    const double inv = 1.0 / determinantValue;
    return {(matrix[4] * matrix[8] - matrix[5] * matrix[7]) * inv,
            (matrix[2] * matrix[7] - matrix[1] * matrix[8]) * inv,
            (matrix[1] * matrix[5] - matrix[2] * matrix[4]) * inv,
            (matrix[5] * matrix[6] - matrix[3] * matrix[8]) * inv,
            (matrix[0] * matrix[8] - matrix[2] * matrix[6]) * inv,
            (matrix[2] * matrix[3] - matrix[0] * matrix[5]) * inv,
            (matrix[3] * matrix[7] - matrix[4] * matrix[6]) * inv,
            (matrix[1] * matrix[6] - matrix[0] * matrix[7]) * inv,
            (matrix[0] * matrix[4] - matrix[1] * matrix[3]) * inv};
}

[[nodiscard]] Matrix3 firstPiolaNeoHookean(const Matrix3& deformationGradient, const MpmMaterial& material) {
    if (material.youngModulus < 0.0) throw std::invalid_argument("MPM Young's modulus cannot be negative");
    if (!(material.poissonRatio > -1.0 && material.poissonRatio < 0.5))
        throw std::invalid_argument("MPM Poisson ratio must lie in (-1, 0.5)");
    const double determinantValue = determinant(deformationGradient);
    if (!std::isfinite(determinantValue) || determinantValue <= 1.0e-12)
        throw std::runtime_error("MPM deformation gradient became singular or inverted");
    const double mu = material.youngModulus / (2.0 * (1.0 + material.poissonRatio));
    const double lambda = material.youngModulus * material.poissonRatio /
                          ((1.0 + material.poissonRatio) * (1.0 - 2.0 * material.poissonRatio));
    const Matrix3 inverseTranspose = transpose(inverse(deformationGradient));
    Matrix3 result{};
    const double logJ = std::log(determinantValue);
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = mu * (deformationGradient[index] - inverseTranspose[index]) +
                        lambda * logJ * inverseTranspose[index];
    return result;
}

struct AxisKernel {
    long long base{};
    std::array<double, 3> weight{};
    std::array<double, 3> derivative{};
};

[[nodiscard]] AxisKernel quadraticKernel(double gridCoordinate) {
    AxisKernel result;
    result.base = static_cast<long long>(std::floor(gridCoordinate - 0.5));
    const double fractional = gridCoordinate - static_cast<double>(result.base);
    result.weight = {0.5 * (1.5 - fractional) * (1.5 - fractional),
                     0.75 - (fractional - 1.0) * (fractional - 1.0),
                     0.5 * (fractional - 0.5) * (fractional - 0.5)};
    result.derivative = {fractional - 1.5, -2.0 * (fractional - 1.0), fractional - 0.5};
    return result;
}

struct ParticleStencil { AxisKernel x; AxisKernel y; AxisKernel z; };

[[nodiscard]] ParticleStencil stencilFor(const MpmParticle& particle, const MpmGridSettings& settings) {
    const math::Vec3 gridPosition = (particle.position - settings.origin) / settings.cellSize;
    ParticleStencil stencil{quadraticKernel(gridPosition.x), quadraticKernel(gridPosition.y), quadraticKernel(gridPosition.z)};
    const auto inside = [](long long base, std::size_t extent) {
        return base >= 0 && base + 2 < static_cast<long long>(extent);
    };
    if (!inside(stencil.x.base, settings.nx) || !inside(stencil.y.base, settings.ny) || !inside(stencil.z.base, settings.nz))
        throw std::out_of_range("MPM particle left the grid support; enlarge the domain or reduce the timestep");
    return stencil;
}

[[nodiscard]] std::size_t nodeIndex(std::size_t x, std::size_t y, std::size_t z, const MpmGridSettings& settings) noexcept {
    return (z * settings.ny + y) * settings.nx + x;
}

[[nodiscard]] math::Vec3 nodePosition(std::size_t x, std::size_t y, std::size_t z, const MpmGridSettings& settings) noexcept {
    return {settings.origin.x + settings.cellSize * static_cast<double>(x),
            settings.origin.y + settings.cellSize * static_cast<double>(y),
            settings.origin.z + settings.cellSize * static_cast<double>(z)};
}

template <typename Callback>
void visitStencil(const MpmParticle& particle, const MpmGridSettings& settings, const ParticleStencil& stencil, Callback&& callback) {
    const double inverseCell = 1.0 / settings.cellSize;
    for (std::size_t localZ = 0; localZ < 3; ++localZ)
        for (std::size_t localY = 0; localY < 3; ++localY)
            for (std::size_t localX = 0; localX < 3; ++localX) {
                const std::size_t x = static_cast<std::size_t>(stencil.x.base + static_cast<long long>(localX));
                const std::size_t y = static_cast<std::size_t>(stencil.y.base + static_cast<long long>(localY));
                const std::size_t z = static_cast<std::size_t>(stencil.z.base + static_cast<long long>(localZ));
                const double weight = stencil.x.weight[localX] * stencil.y.weight[localY] * stencil.z.weight[localZ];
                const math::Vec3 gradient{
                    stencil.x.derivative[localX] * stencil.y.weight[localY] * stencil.z.weight[localZ] * inverseCell,
                    stencil.x.weight[localX] * stencil.y.derivative[localY] * stencil.z.weight[localZ] * inverseCell,
                    stencil.x.weight[localX] * stencil.y.weight[localY] * stencil.z.derivative[localZ] * inverseCell};
                callback(x, y, z, weight, gradient, nodePosition(x, y, z, settings) - particle.position);
            }
}

void validateSettings(const MpmGridSettings& settings) {
    if (settings.nx < 3 || settings.ny < 3 || settings.nz < 3)
        throw std::invalid_argument("MPM grid must contain at least 3 nodes per axis");
    if (!std::isfinite(settings.cellSize) || settings.cellSize <= 0.0)
        throw std::invalid_argument("MPM cell size must be positive");
    if (settings.boundaryCells * 2U >= std::min({settings.nx, settings.ny, settings.nz}))
        throw std::invalid_argument("MPM boundary layer consumes the whole grid");
}

void applyBoundary(math::Vec3& velocity, std::size_t x, std::size_t y, std::size_t z, const MpmGridSettings& settings) noexcept {
    const std::size_t boundary = settings.boundaryCells;
    if (boundary == 0) return;
    if (x < boundary && velocity.x < 0.0) velocity.x = 0.0;
    if (x >= settings.nx - boundary && velocity.x > 0.0) velocity.x = 0.0;
    if (y < boundary && velocity.y < 0.0) velocity.y = 0.0;
    if (y >= settings.ny - boundary && velocity.y > 0.0) velocity.y = 0.0;
    if (z < boundary && velocity.z < 0.0) velocity.z = 0.0;
    if (z >= settings.nz - boundary && velocity.z > 0.0) velocity.z = 0.0;
}

} // namespace

double deformationDeterminant(const MpmParticle& particle) noexcept { return determinant(particle.deformationGradient); }

math::Vec3 totalMpmMomentum(const std::vector<MpmParticle>& particles) noexcept {
    math::Vec3 result{};
    for (const auto& particle : particles) result += particle.velocity * particle.mass;
    return result;
}

double totalMpmMass(const std::vector<MpmParticle>& particles) noexcept {
    double result = 0.0;
    for (const auto& particle : particles) result += particle.mass;
    return result;
}

MpmTransferEvidence particleToGridMpm(const std::vector<MpmParticle>& particles, const MpmGridSettings& settings,
                                      const MpmMaterial& material, std::vector<MpmGridNode>& grid) {
    validateSettings(settings);
    if (particles.empty()) throw std::invalid_argument("MPM transfer requires at least one particle");
    grid.assign(settings.nx * settings.ny * settings.nz, MpmGridNode{});
    MpmTransferEvidence evidence;
    evidence.particleMass = totalMpmMass(particles);
    evidence.particleMomentum = totalMpmMomentum(particles);
    for (const auto& particle : particles) {
        if (!std::isfinite(particle.mass) || particle.mass <= 0.0 || !std::isfinite(particle.restVolume) || particle.restVolume <= 0.0)
            throw std::invalid_argument("MPM particle mass and rest volume must be positive");
        evidence.appliedExternalForce += particle.externalForce;
        const ParticleStencil stencil = stencilFor(particle, settings);
        const Matrix3 firstPiola = firstPiolaNeoHookean(particle.deformationGradient, material);
        const Matrix3 kirchhoff = multiply(firstPiola, transpose(particle.deformationGradient));
        visitStencil(particle, settings, stencil,
            [&](std::size_t x, std::size_t y, std::size_t z, double weight, math::Vec3 gradient, math::Vec3 nodeOffset) {
                auto& node = grid[nodeIndex(x, y, z, settings)];
                const math::Vec3 apicVelocity = particle.velocity + multiply(particle.affineVelocity, nodeOffset);
                node.mass += weight * particle.mass;
                node.momentum += apicVelocity * (weight * particle.mass);
                node.force += multiply(kirchhoff, gradient) * (-particle.restVolume);
                node.force += particle.externalForce * weight;
            });
    }
    for (const auto& node : grid) {
        evidence.gridMass += node.mass;
        evidence.gridMomentum += node.momentum;
        evidence.gridForce += node.force;
    }
    evidence.massConservationError = std::abs(evidence.gridMass - evidence.particleMass);
    evidence.momentumConservationError = math::length(evidence.gridMomentum - evidence.particleMomentum);
    evidence.forceBalanceError = math::length(evidence.gridForce - evidence.appliedExternalForce);
    return evidence;
}

MpmStepEvidence stepMpm(std::vector<MpmParticle>& particles, const MpmGridSettings& settings,
                        const MpmMaterial& material, double dt, math::Vec3 gravity) {
    if (!std::isfinite(dt) || dt <= 0.0) throw std::invalid_argument("MPM timestep must be positive");
    MpmStepEvidence evidence;
    evidence.initialMomentum = totalMpmMomentum(particles);
    const double totalMass = totalMpmMass(particles);
    std::vector<MpmGridNode> grid;
    evidence.transfer = particleToGridMpm(particles, settings, material, grid);
    for (std::size_t z = 0; z < settings.nz; ++z)
        for (std::size_t y = 0; y < settings.ny; ++y)
            for (std::size_t x = 0; x < settings.nx; ++x) {
                auto& node = grid[nodeIndex(x, y, z, settings)];
                if (node.mass <= std::numeric_limits<double>::epsilon()) continue;
                ++evidence.activeGridNodes;
                node.velocity = node.momentum / node.mass + (node.force / node.mass + gravity) * dt;
                applyBoundary(node.velocity, x, y, z, settings);
            }
    const double apicFactor = 4.0 / (settings.cellSize * settings.cellSize);
    for (auto& particle : particles) {
        const ParticleStencil stencil = stencilFor(particle, settings);
        math::Vec3 newVelocity{};
        Matrix3 newAffine{};
        visitStencil(particle, settings, stencil,
            [&](std::size_t x, std::size_t y, std::size_t z, double weight, math::Vec3, math::Vec3 nodeOffset) {
                const auto& node = grid[nodeIndex(x, y, z, settings)];
                newVelocity += node.velocity * weight;
                addScaled(newAffine, outer(node.velocity, nodeOffset), apicFactor * weight);
            });
        particle.velocity = newVelocity;
        particle.affineVelocity = newAffine;
        particle.position += particle.velocity * dt;
        Matrix3 update = identityMatrix3();
        for (std::size_t index = 0; index < update.size(); ++index) update[index] += dt * newAffine[index];
        particle.deformationGradient = multiply(update, particle.deformationGradient);
        particle.externalForce = {};
    }
    evidence.finalMomentum = totalMpmMomentum(particles);
    evidence.expectedMomentumWithoutBoundary = evidence.initialMomentum +
        (evidence.transfer.appliedExternalForce + gravity * totalMass) * dt;
    evidence.momentumBalanceError = math::length(evidence.finalMomentum - evidence.expectedMomentumWithoutBoundary);
    if (!particles.empty()) {
        evidence.minimumDeformationDeterminant = std::numeric_limits<double>::infinity();
        evidence.maximumDeformationDeterminant = 0.0;
        for (const auto& particle : particles) {
            const double determinantValue = deformationDeterminant(particle);
            evidence.minimumDeformationDeterminant = std::min(evidence.minimumDeformationDeterminant, determinantValue);
            evidence.maximumDeformationDeterminant = std::max(evidence.maximumDeformationDeterminant, determinantValue);
        }
    }
    return evidence;
}

} // namespace vulkax::solvers
