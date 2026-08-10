#include "vulkax/solvers/dem.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::solvers {

namespace {

struct PairForce {
    math::Vec3 onA{};
    double overlap{};
    bool contact{false};
};

PairForce contactForce(const DemParticle& a, const DemParticle& b, const DemConfig& config) {
    const math::Vec3 displacement = b.position - a.position;
    const double distance = math::length(displacement);
    const double overlap = a.radius + b.radius - distance;
    if (overlap <= 0.0) {
        return {};
    }
    const math::Vec3 normal = distance > 1.0e-12 ? displacement / distance : math::Vec3{1.0, 0.0, 0.0};
    const math::Vec3 relativeVelocity = b.velocity - a.velocity;
    const double normalVelocity = math::dot(relativeVelocity, normal);
    const double normalMagnitude = std::max(0.0, config.normalStiffness * overlap +
                                                     config.normalDamping * normalVelocity);
    const math::Vec3 tangentVelocity = relativeVelocity - normal * normalVelocity;
    const double tangentSpeed = math::length(tangentVelocity);
    math::Vec3 tangential{};
    if (tangentSpeed > 1.0e-12) {
        tangential = (tangentVelocity / tangentSpeed) * (config.friction * normalMagnitude);
    }
    return {normal * (-normalMagnitude) + tangential, overlap, true};
}

void resolveWalls(DemParticle& particle, const DemBox& box, const DemConfig& config) {
    auto resolveAxis = [&](double& position, double& velocity, double minimum, double maximum) {
        if (position - particle.radius < minimum) {
            position = minimum + particle.radius;
            if (velocity < 0.0) {
                velocity = -velocity * config.wallRestitution;
            }
        }
        if (position + particle.radius > maximum) {
            position = maximum - particle.radius;
            if (velocity > 0.0) {
                velocity = -velocity * config.wallRestitution;
            }
        }
    };
    resolveAxis(particle.position.x, particle.velocity.x, box.minimum.x, box.maximum.x);
    resolveAxis(particle.position.y, particle.velocity.y, box.minimum.y, box.maximum.y);
    resolveAxis(particle.position.z, particle.velocity.z, box.minimum.z, box.maximum.z);
}

} // namespace

void advanceDem(std::vector<DemParticle>& particles, const DemBox& box, const DemConfig& config,
                std::size_t steps) {
    if (config.dt <= 0.0 || config.normalStiffness < 0.0 || config.normalDamping < 0.0 ||
        config.friction < 0.0 || config.wallRestitution < 0.0 || config.wallRestitution > 1.0) {
        throw std::invalid_argument("invalid DEM configuration");
    }
    for (const auto& particle : particles) {
        if (particle.mass <= 0.0 || particle.radius <= 0.0) {
            throw std::invalid_argument("DEM particles require positive mass and radius");
        }
    }

    std::vector<math::Vec3> forces(particles.size());
    for (std::size_t step = 0; step < steps; ++step) {
        for (std::size_t i = 0; i < particles.size(); ++i) {
            forces[i] = config.gravity * particles[i].mass;
        }
        for (std::size_t i = 0; i < particles.size(); ++i) {
            for (std::size_t j = i + 1; j < particles.size(); ++j) {
                const auto interaction = contactForce(particles[i], particles[j], config);
                if (interaction.contact) {
                    forces[i] += interaction.onA;
                    forces[j] -= interaction.onA;
                }
            }
        }
        for (std::size_t i = 0; i < particles.size(); ++i) {
            const math::Vec3 acceleration = forces[i] / particles[i].mass;
            particles[i].velocity += acceleration * config.dt;
            particles[i].position += particles[i].velocity * config.dt;
            resolveWalls(particles[i], box, config);
        }
    }
}

DemDiagnostics measureDem(const std::vector<DemParticle>& particles, const DemBox& box,
                          const DemConfig& config) {
    DemDiagnostics diagnostics;
    const double referenceY = box.minimum.y;
    for (const auto& particle : particles) {
        diagnostics.kineticEnergy += 0.5 * particle.mass * math::dot(particle.velocity, particle.velocity);
        diagnostics.potentialEnergy += -particle.mass * config.gravity.y * (particle.position.y - referenceY);
    }
    for (std::size_t i = 0; i < particles.size(); ++i) {
        for (std::size_t j = i + 1; j < particles.size(); ++j) {
            const double overlap = particles[i].radius + particles[j].radius -
                                   math::length(particles[j].position - particles[i].position);
            if (overlap > 0.0) {
                ++diagnostics.contacts;
                diagnostics.maximumOverlap = std::max(diagnostics.maximumOverlap, overlap);
            }
        }
    }
    return diagnostics;
}

} // namespace vulkax::solvers
