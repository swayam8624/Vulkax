#include "vulkax/solvers/rotating_drum.hpp"

#include "vulkax/solvers/dem_broadphase.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::solvers {

DrumDiagnostics advanceRotatingDrum(std::vector<DemParticle>& particles,
                                    const RotatingDrum& drum,
                                    const DemConfig& config,
                                    std::size_t steps) {
    if (particles.empty() || drum.radius <= 0.0 || drum.halfLength <= 0.0 || steps == 0) {
        throw std::invalid_argument("invalid rotating drum request");
    }
    DemBox broadphaseBox{{-drum.radius, -drum.radius, -drum.halfLength},
                         {drum.radius, drum.radius, drum.halfLength}};
    DrumDiagnostics total;

    for (std::size_t step = 0; step < steps; ++step) {
        const auto broadphase = advanceDemSpatialHash(particles, broadphaseBox, config, 1);
        total.particleContacts += broadphase.contacts;
        total.broadphaseCandidates += broadphase.candidatePairs;

        for (auto& particle : particles) {
            const double radial = std::hypot(particle.position.x, particle.position.y);
            const double allowed = drum.radius - particle.radius;
            if (radial > allowed) {
                ++total.wallCollisions;
                const math::Vec3 normal = radial > 1.0e-12
                                              ? math::Vec3{particle.position.x / radial,
                                                           particle.position.y / radial, 0.0}
                                              : math::Vec3{1.0, 0.0, 0.0};
                particle.position.x = normal.x * allowed;
                particle.position.y = normal.y * allowed;

                const math::Vec3 wallVelocity{-drum.angularVelocity * particle.position.y,
                                               drum.angularVelocity * particle.position.x, 0.0};
                const math::Vec3 relative = particle.velocity - wallVelocity;
                const double normalSpeed = math::dot(relative, normal);
                const math::Vec3 before = particle.velocity;
                if (normalSpeed > 0.0) {
                    total.wallImpactEnergy += 0.5 * particle.mass * normalSpeed * normalSpeed;
                    particle.velocity -= normal * ((1.0 + config.wallRestitution) * normalSpeed);
                }
                const math::Vec3 tangent{-normal.y, normal.x, 0.0};
                const double currentTangential = math::dot(particle.velocity, tangent);
                const double wallTangential = math::dot(wallVelocity, tangent);
                const double coupling = std::clamp(config.friction, 0.0, 1.0);
                particle.velocity += tangent * ((wallTangential - currentTangential) * coupling);
                const math::Vec3 deltaV = particle.velocity - before;
                total.wallEnergyTransfer += particle.mass * std::abs(math::dot(deltaV, wallVelocity));
            }

            const double zLimit = drum.halfLength - particle.radius;
            if (particle.position.z > zLimit) {
                ++total.wallCollisions;
                particle.position.z = zLimit;
                if (particle.velocity.z > 0.0) particle.velocity.z *= -config.wallRestitution;
            } else if (particle.position.z < -zLimit) {
                ++total.wallCollisions;
                particle.position.z = -zLimit;
                if (particle.velocity.z < 0.0) particle.velocity.z *= -config.wallRestitution;
            }
        }
    }

    double speedSum = 0.0;
    double tangentialSum = 0.0;
    for (const auto& particle : particles) {
        const double radial = std::hypot(particle.position.x, particle.position.y);
        speedSum += math::length(particle.velocity);
        if (radial > 1.0e-12) {
            const math::Vec3 tangent{-particle.position.y / radial, particle.position.x / radial, 0.0};
            tangentialSum += math::dot(particle.velocity, tangent);
        }
    }
    const double count = static_cast<double>(particles.size());
    total.meanSpeed = speedSum / count;
    total.meanTangentialVelocity = tangentialSum / count;
    return total;
}

} // namespace vulkax::solvers
