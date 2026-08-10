#include "vulkax/solvers/dem_broadphase.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace vulkax::solvers {

namespace {

struct Cell {
    int x{};
    int y{};
    int z{};
    bool operator==(const Cell&) const = default;
};

struct CellHash {
    std::size_t operator()(const Cell& cell) const noexcept {
        const auto mix = [](std::uint64_t value) {
            value ^= value >> 33u;
            value *= 0xff51afd7ed558ccdULL;
            value ^= value >> 33u;
            value *= 0xc4ceb9fe1a85ec53ULL;
            value ^= value >> 33u;
            return value;
        };
        const std::uint64_t a = mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(cell.x)));
        const std::uint64_t b = mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(cell.y)) + 0x9e3779b97f4a7c15ULL);
        const std::uint64_t c = mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(cell.z)) + 0x243f6a8885a308d3ULL);
        return static_cast<std::size_t>(a ^ (b << 1u) ^ (c << 7u));
    }
};

Cell cellFor(math::Vec3 position, double cellSize) {
    return {static_cast<int>(std::floor(position.x / cellSize)),
            static_cast<int>(std::floor(position.y / cellSize)),
            static_cast<int>(std::floor(position.z / cellSize))};
}

math::Vec3 pairForce(const DemParticle& a, const DemParticle& b, const DemConfig& config,
                     bool& contact) {
    const math::Vec3 delta = b.position - a.position;
    const double distance = math::length(delta);
    const double overlap = a.radius + b.radius - distance;
    if (overlap <= 0.0) {
        contact = false;
        return {};
    }
    contact = true;
    const math::Vec3 normal = distance > 1.0e-12 ? delta / distance : math::Vec3{1.0, 0.0, 0.0};
    const math::Vec3 relative = b.velocity - a.velocity;
    const double normalVelocity = math::dot(relative, normal);
    const double normalForce = std::max(0.0, config.normalStiffness * overlap + config.normalDamping * normalVelocity);
    const math::Vec3 tangent = relative - normal * normalVelocity;
    const double tangentSpeed = math::length(tangent);
    const math::Vec3 friction = tangentSpeed > 1.0e-12
                                    ? math::normalized(tangent) * (config.friction * normalForce)
                                    : math::Vec3{};
    return -normal * normalForce + friction;
}

void resolveWalls(DemParticle& particle, const DemBox& box, const DemConfig& config) {
    auto axis = [&](double& position, double& velocity, double minimum, double maximum) {
        if (position - particle.radius < minimum) {
            position = minimum + particle.radius;
            if (velocity < 0.0) velocity = -velocity * config.wallRestitution;
        }
        if (position + particle.radius > maximum) {
            position = maximum - particle.radius;
            if (velocity > 0.0) velocity = -velocity * config.wallRestitution;
        }
    };
    axis(particle.position.x, particle.velocity.x, box.minimum.x, box.maximum.x);
    axis(particle.position.y, particle.velocity.y, box.minimum.y, box.maximum.y);
    axis(particle.position.z, particle.velocity.z, box.minimum.z, box.maximum.z);
}

} // namespace

DemBroadphaseStats advanceDemSpatialHash(std::vector<DemParticle>& particles, const DemBox& box,
                                         const DemConfig& config, std::size_t steps) {
    if (config.dt <= 0.0 || particles.empty()) throw std::invalid_argument("invalid hashed DEM request");
    double maxRadius = 0.0;
    for (const auto& particle : particles) {
        if (particle.radius <= 0.0 || particle.mass <= 0.0) throw std::invalid_argument("invalid DEM particle");
        maxRadius = std::max(maxRadius, particle.radius);
    }
    const double cellSize = 2.0 * maxRadius;
    DemBroadphaseStats finalStats;
    std::vector<math::Vec3> forces(particles.size());

    for (std::size_t step = 0; step < steps; ++step) {
        std::unordered_map<Cell, std::vector<std::size_t>, CellHash> cells;
        cells.reserve(particles.size() * 2);
        for (std::size_t index = 0; index < particles.size(); ++index) {
            cells[cellFor(particles[index].position, cellSize)].push_back(index);
            forces[index] = config.gravity * particles[index].mass;
        }

        DemBroadphaseStats stats;
        for (std::size_t i = 0; i < particles.size(); ++i) {
            const Cell home = cellFor(particles[i].position, cellSize);
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const auto iterator = cells.find({home.x + dx, home.y + dy, home.z + dz});
                        if (iterator == cells.end()) continue;
                        for (std::size_t j : iterator->second) {
                            if (j <= i) continue;
                            ++stats.candidatePairs;
                            bool contact = false;
                            const math::Vec3 force = pairForce(particles[i], particles[j], config, contact);
                            if (contact) {
                                ++stats.contacts;
                                forces[i] += force;
                                forces[j] -= force;
                            }
                        }
                    }
                }
            }
        }

        for (std::size_t index = 0; index < particles.size(); ++index) {
            particles[index].velocity += forces[index] / particles[index].mass * config.dt;
            particles[index].position += particles[index].velocity * config.dt;
            resolveWalls(particles[index], box, config);
        }
        finalStats = stats;
    }
    return finalStats;
}

} // namespace vulkax::solvers
