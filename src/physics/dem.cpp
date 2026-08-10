#include "vulkax/physics/dem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace vulkax::physics::dem {
namespace {
using field::Vec3;

struct Cell { int x{}, y{}, z{}; bool operator==(const Cell&) const = default; };
struct CellHash {
    std::size_t operator()(const Cell& c) const noexcept {
        std::size_t h = static_cast<std::size_t>(static_cast<std::uint32_t>(c.x) * 73856093u);
        h ^= static_cast<std::size_t>(static_cast<std::uint32_t>(c.y) * 19349663u);
        h ^= static_cast<std::size_t>(static_cast<std::uint32_t>(c.z) * 83492791u);
        return h;
    }
};

Vec3 add(Vec3 a, Vec3 b) { return a + b; }
void addTo(Vec3& a, Vec3 b) { a = a + b; }

Vec3 contactForce(Vec3 delta, Vec3 relativeVelocity, double overlap, const Material& material,
                  double* dissipation) {
    const double distance = field::length(delta);
    if (distance <= 1.0e-14 || overlap <= 0.0) return {};
    const Vec3 n = delta / distance;
    const double vn = field::dot(relativeVelocity, n);
    const double elastic = material.normalStiffness * overlap * std::sqrt(overlap);
    const double damping = -material.normalDamping * vn;
    const double fn = std::max(0.0, elastic + damping);
    const Vec3 tangentialVelocity = relativeVelocity - n * vn;
    const double tangentialSpeed = field::length(tangentialVelocity);
    Vec3 tangential{};
    if (tangentialSpeed > 1.0e-14) {
        const double requested = material.tangentialDamping * tangentialSpeed;
        const double magnitude = std::min(material.friction * fn, requested);
        tangential = tangentialVelocity * (-magnitude / tangentialSpeed);
        if (dissipation) *dissipation += magnitude * tangentialSpeed + std::max(0.0, -damping * vn);
    }
    return n * fn + tangential;
}

} // namespace

Solver::Solver(field::ParticleSet particles, Settings settings)
    : particles_(std::move(particles)), settings_(settings) {
    const auto n = particles_.positions.size();
    if (particles_.velocities.size() != n || particles_.radii.size() != n || particles_.masses.size() != n)
        throw std::invalid_argument("DEM particle arrays must have identical lengths");
    for (std::size_t i = 0; i < n; ++i) {
        if (!(particles_.radii[i] > 0.0) || !(particles_.masses[i] > 0.0))
            throw std::invalid_argument("DEM particle radii and masses must be positive");
    }
    if (!(settings_.drum.radius > 0.0) || !(settings_.drum.halfHeight > 0.0))
        throw std::invalid_argument("DEM drum dimensions must be positive");
}

void Solver::step(double dt) {
    if (!(dt > 0.0)) throw std::invalid_argument("DEM timestep must be positive");
    statistics_ = {};
    const std::size_t n = particles_.positions.size();
    std::vector<Vec3> forces(n);
    double maxDiameter = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        forces[i] = settings_.gravity * particles_.masses[i];
        maxDiameter = std::max(maxDiameter, 2.0 * particles_.radii[i]);
    }
    if (n == 0) return;

    const double cellSize = std::max(maxDiameter, 1.0e-8);
    std::unordered_map<Cell, std::vector<std::size_t>, CellHash> grid;
    grid.reserve(n * 2);
    auto cellOf = [&](Vec3 p) {
        return Cell{static_cast<int>(std::floor(p.x / cellSize)),
                    static_cast<int>(std::floor(p.y / cellSize)),
                    static_cast<int>(std::floor(p.z / cellSize))};
    };
    for (std::size_t i = 0; i < n; ++i) grid[cellOf(particles_.positions[i])].push_back(i);

    for (std::size_t i = 0; i < n; ++i) {
        const Cell base = cellOf(particles_.positions[i]);
        for (int dz = -1; dz <= 1; ++dz) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const auto it = grid.find({base.x + dx, base.y + dy, base.z + dz});
            if (it == grid.end()) continue;
            for (const std::size_t j : it->second) {
                if (j <= i) continue;
                const Vec3 delta = particles_.positions[j] - particles_.positions[i];
                const double distance = field::length(delta);
                const double overlap = particles_.radii[i] + particles_.radii[j] - distance;
                if (overlap <= 0.0) continue;
                const Vec3 relative = particles_.velocities[j] - particles_.velocities[i];
                const Vec3 forceOnJ = contactForce(delta, relative, overlap, settings_.particleMaterial,
                                                   &statistics_.dissipatedPower);
                addTo(forces[j], forceOnJ);
                addTo(forces[i], forceOnJ * -1.0);
                ++statistics_.particleContacts;
            }
        }
    }

    const auto& drum = settings_.drum;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 p = particles_.positions[i];
        const double radial = std::sqrt(p.x * p.x + p.y * p.y);
        const double radius = particles_.radii[i];
        if (radial + radius > drum.radius && radial > 1.0e-14) {
            const Vec3 inward{-p.x / radial, -p.y / radial, 0.0};
            const double overlap = radial + radius - drum.radius;
            const Vec3 wallVelocity{-drum.angularVelocity * p.y, drum.angularVelocity * p.x, 0.0};
            const Vec3 relative = particles_.velocities[i] - wallVelocity;
            const Vec3 delta = inward;
            const Vec3 wallForce = contactForce(delta, relative * -1.0, overlap, drum.wallMaterial,
                                                 &statistics_.dissipatedPower);
            addTo(forces[i], wallForce);
            ++statistics_.wallContacts;
        }
        const double topPenetration = p.z + radius - drum.halfHeight;
        if (topPenetration > 0.0) {
            const Vec3 force = contactForce({0.0, 0.0, -1.0}, particles_.velocities[i] * -1.0,
                                            topPenetration, drum.wallMaterial, &statistics_.dissipatedPower);
            addTo(forces[i], force); ++statistics_.wallContacts;
        }
        const double bottomPenetration = -drum.halfHeight - (p.z - radius);
        if (bottomPenetration > 0.0) {
            const Vec3 force = contactForce({0.0, 0.0, 1.0}, particles_.velocities[i] * -1.0,
                                            bottomPenetration, drum.wallMaterial, &statistics_.dissipatedPower);
            addTo(forces[i], force); ++statistics_.wallContacts;
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        particles_.velocities[i] = particles_.velocities[i] + forces[i] * (dt / particles_.masses[i]);
        particles_.positions[i] = particles_.positions[i] + particles_.velocities[i] * dt;
        const double speed = field::length(particles_.velocities[i]);
        statistics_.maximumSpeed = std::max(statistics_.maximumSpeed, speed);
        statistics_.kineticEnergy += 0.5 * particles_.masses[i] * speed * speed;
    }
}

} // namespace vulkax::physics::dem
