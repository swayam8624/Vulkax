#pragma once

#include "vulkax/core/math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace vulkax::solvers {

using Matrix3 = std::array<double, 9>;

[[nodiscard]] constexpr Matrix3 identityMatrix3() noexcept {
    return {1.0, 0.0, 0.0,
            0.0, 1.0, 0.0,
            0.0, 0.0, 1.0};
}

enum class MpmTransferScheme {
    PIC,
    FLIP,
    APIC,
    APIC_FLIP,
};

[[nodiscard]] constexpr std::string_view toString(MpmTransferScheme scheme) noexcept {
    switch (scheme) {
        case MpmTransferScheme::PIC: return "PIC";
        case MpmTransferScheme::FLIP: return "FLIP";
        case MpmTransferScheme::APIC: return "APIC";
        case MpmTransferScheme::APIC_FLIP: return "APIC-FLIP";
    }
    return "unknown";
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

// For APIC_FLIP, flipBlend controls only the G2P particle velocity blend:
//   v_new = (1-beta) * v_PIC + beta * (v_old + delta_v_FLIP).
// Affine APIC P2G and affine-state reconstruction remain active. Therefore
// beta=0 reproduces APIC, while beta=1 is an affine-P2G FLIP-like endpoint
// and is intentionally distinct from the pure FLIP scheme.
[[nodiscard]] MpmTransferEvidence particleToGridMpm(
    const std::vector<MpmParticle>& particles,
    const MpmGridSettings& settings,
    const MpmMaterial& material,
    std::vector<MpmGridNode>& grid,
    MpmTransferScheme transferScheme = MpmTransferScheme::APIC,
    double flipBlend = 0.0);

[[nodiscard]] MpmStepEvidence stepMpm(
    std::vector<MpmParticle>& particles,
    const MpmGridSettings& settings,
    const MpmMaterial& material,
    double dt,
    math::Vec3 gravity = {0.0, -9.81, 0.0},
    MpmTransferScheme transferScheme = MpmTransferScheme::APIC,
    double flipBlend = 0.0);

[[nodiscard]] double deformationDeterminant(const MpmParticle& particle) noexcept;
[[nodiscard]] math::Vec3 totalMpmMomentum(const std::vector<MpmParticle>& particles) noexcept;
[[nodiscard]] double totalMpmMass(const std::vector<MpmParticle>& particles) noexcept;

} // namespace vulkax::solvers
