#pragma once

#include "vulkax/solvers/mpm.hpp"

#include <cstdint>
#include <vector>

namespace vulkax::research {

struct PrescribedParticleTarget {
    std::uint64_t particleId{};
    math::Vec3 position{};
    math::Vec3 velocity{};
    bool zeroAffineVelocity{true};
};

struct PrescribedMpmConstraintEvidence {
    solvers::MpmStepEvidence unconstrainedStep;
    std::size_t constrainedParticleCount{};
    math::Vec3 totalConstraintImpulse{};
    double totalConstraintImpulseMagnitude{};
    double kineticConstraintWork{};
    double maximumPositionCorrection{};
    double maximumVelocityCorrection{};
    math::Vec3 momentumBeforeProjection{};
    math::Vec3 momentumAfterProjection{};
    math::Vec3 expectedMomentumAfterProjection{};
    double momentumAccountingError{};
};

// Research-only hard Dirichlet/kinematic particle projection.
//
// The ordinary solver step is executed first without modification. Selected
// particles are then projected to explicit target positions and velocities.
// The projection is intentionally NOT hidden inside stepMpm: every momentum
// impulse, kinetic-energy change and positional correction introduced by the
// prescribed boundary is returned as evidence. This is intended for controlled
// boundary-driven experiments such as GAUGE-style fixed/kinematic fixtures.
//
// Positional projection can also change elastic energy through the next step;
// kineticConstraintWork therefore reports only the exact kinetic-energy change
// induced by the velocity projection. Callers must not interpret it as complete
// actuator work.
[[nodiscard]] PrescribedMpmConstraintEvidence stepMpmWithPrescribedParticles(
    std::vector<solvers::MpmParticle>& particles,
    const solvers::MpmGridSettings& grid,
    const solvers::MpmMaterial& material,
    double dt,
    const std::vector<PrescribedParticleTarget>& targets,
    math::Vec3 gravity = {0.0, -9.81, 0.0},
    solvers::MpmTransferScheme transferScheme = solvers::MpmTransferScheme::APIC,
    double flipBlend = 0.0);

} // namespace vulkax::research
