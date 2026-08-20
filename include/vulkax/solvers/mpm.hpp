#pragma once

#include "vulkax/core/math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vulkax::solvers {

using Matrix3 = std::array<double, 9>;

[[nodiscard]] constexpr Matrix3 identityMatrix3() noexcept {
    return {1.0, 0.0, 0.0,
            0.0, 1.0, 0.0,
            0.0, 0.0, 1.0};
}

struct MpmMaterial {
    double density{1000.0};
    double youngModulus{1.0e5};
    double poissonRatio{0.3};
};

struct MpmParticle {
    std::uint64_t id{};
    math::Vec3 restPosition{};
    math::Vec3 position{};
    math::Vec3 velocity{};
    double mass{1.0};
    double restVolume{1.0};
    Matrix3 deformationGradient{identityMatrix3()};
    Matrix3 affineVelocity{};
    math::Vec3 externalForce{};
};

struct MpmGridSettings {
    math::Vec3 origin{-1.0, -1.0, -1.0};
    std::size_t nx{32};
    std::size_t ny{32};
    std::size_t nz{32};
    double cellSize{0.0625};
    std::size_t boundaryCells{2};
};

struct MpmGridNode {
    double mass{};
    math::Vec3 momentum{};
    math::Vec3 force{};
    math::Vec3 velocity{};
};

struct MpmTransferEvidence {
    double particleMass{};
    double gridMass{};
    double massConservationError{};
    math::Vec3 particleMomentum{};
    math::Vec3 gridMomentum{};
    double momentumConservationError{};
    math::Vec3 appliedExternalForce{};
    math::Vec3 gridForce{};
    double forceBalanceError{};
};

struct MpmStepEvidence {
    MpmTransferEvidence transfer;
    std::size_t activeGridNodes{};
    math::Vec3 initialMomentum{};
    math::Vec3 finalMomentum{};
    math::Vec3 expectedMomentumWithoutBoundary{};
    double momentumBalanceError{};
    double minimumDeformationDeterminant{1.0};
    double maximumDeformationDeterminant{1.0};
};

[[nodiscard]] MpmTransferEvidence particleToGridMpm(
    const std::vector<MpmParticle>& particles,
    const MpmGridSettings& settings,
    const MpmMaterial& material,
    std::vector<MpmGridNode>& grid);

[[nodiscard]] MpmStepEvidence stepMpm(
    std::vector<MpmParticle>& particles,
    const MpmGridSettings& settings,
    const MpmMaterial& material,
    double dt,
    math::Vec3 gravity = {0.0, -9.81, 0.0});

[[nodiscard]] double deformationDeterminant(const MpmParticle& particle) noexcept;
[[nodiscard]] math::Vec3 totalMpmMomentum(const std::vector<MpmParticle>& particles) noexcept;
[[nodiscard]] double totalMpmMass(const std::vector<MpmParticle>& particles) noexcept;

} // namespace vulkax::solvers
