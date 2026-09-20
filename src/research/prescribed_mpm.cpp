#include "vulkax/research/prescribed_mpm.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace vulkax::research {
namespace {

[[nodiscard]] double squaredLength(math::Vec3 v) noexcept {
    return math::dot(v, v);
}

} // namespace

PrescribedMpmConstraintEvidence stepMpmWithPrescribedParticles(
    std::vector<solvers::MpmParticle>& particles,
    const solvers::MpmGridSettings& grid,
    const solvers::MpmMaterial& material,
    double dt,
    const std::vector<PrescribedParticleTarget>& targets,
    math::Vec3 gravity,
    solvers::MpmTransferScheme transferScheme,
    double flipBlend) {

    std::unordered_map<std::uint64_t, std::size_t> particleIndex;
    particleIndex.reserve(particles.size());
    for (std::size_t i = 0; i < particles.size(); ++i) {
        if (!particleIndex.emplace(particles[i].id, i).second)
            throw std::invalid_argument("prescribed MPM requires unique particle IDs");
    }

    std::unordered_set<std::uint64_t> requested;
    requested.reserve(targets.size());
    for (const auto& target : targets) {
        if (!requested.insert(target.particleId).second)
            throw std::invalid_argument("prescribed MPM target IDs must be unique");
        if (!particleIndex.contains(target.particleId))
            throw std::invalid_argument("prescribed MPM target references an unknown particle ID");
        const auto finite = [](math::Vec3 v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        if (!finite(target.position) || !finite(target.velocity))
            throw std::invalid_argument("prescribed MPM target state must be finite");
    }

    PrescribedMpmConstraintEvidence evidence;
    evidence.unconstrainedStep = solvers::stepMpm(
        particles, grid, material, dt, gravity, transferScheme, flipBlend);
    evidence.momentumBeforeProjection = solvers::totalMpmMomentum(particles);

    for (const auto& target : targets) {
        auto& particle = particles[particleIndex.at(target.particleId)];
        const math::Vec3 positionCorrection = target.position - particle.position;
        const math::Vec3 velocityCorrection = target.velocity - particle.velocity;
        const math::Vec3 impulse = velocityCorrection * particle.mass;

        evidence.totalConstraintImpulse += impulse;
        evidence.totalConstraintImpulseMagnitude += math::length(impulse);
        evidence.maximumPositionCorrection = std::max(
            evidence.maximumPositionCorrection, math::length(positionCorrection));
        evidence.maximumVelocityCorrection = std::max(
            evidence.maximumVelocityCorrection, math::length(velocityCorrection));

        // Exact kinetic-energy change from v_before -> v_target.
        evidence.kineticConstraintWork +=
            0.5 * particle.mass *
            (squaredLength(target.velocity) - squaredLength(particle.velocity));

        particle.position = target.position;
        particle.velocity = target.velocity;
        if (target.zeroAffineVelocity) particle.affineVelocity = {};
        ++evidence.constrainedParticleCount;
    }

    evidence.momentumAfterProjection = solvers::totalMpmMomentum(particles);
    evidence.expectedMomentumAfterProjection =
        evidence.momentumBeforeProjection + evidence.totalConstraintImpulse;
    evidence.momentumAccountingError = math::length(
        evidence.momentumAfterProjection - evidence.expectedMomentumAfterProjection);
    return evidence;
}

} // namespace vulkax::research
