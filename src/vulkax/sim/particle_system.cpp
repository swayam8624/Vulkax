#include "vulkax/sim/particle_system.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace vulkax::sim {
namespace {

double lengthSquared(const Vec2d& value) { return value.x * value.x + value.y * value.y; }

}  // namespace

ParticleGravitySystem::ParticleGravitySystem(ParticleGravityConfig config) : config_(config) {
  if (config_.orbiters == 0 || config_.centralMass <= 0.0 || config_.orbiterMass <= 0.0 ||
      config_.gravitationalConstant <= 0.0 || config_.softening <= 0.0 || config_.timestepSeconds <= 0.0) {
    throw std::invalid_argument("particle gravity requires positive masses, softening, and timestep");
  }
  reset();
}

void ParticleGravitySystem::reset() {
  particles_.clear();
  particles_.reserve(config_.orbiters + 1);
  particles_.push_back({0, config_.centralMass, {}, {}});
  // The deterministic phase offset avoids perfectly symmetric cancellation while
  // leaving the initial state reproducible from the config and seed.
  const double phaseOffset = static_cast<double>(config_.seed % 360) * std::numbers::pi / 180.0;
  for (uint32_t index = 0; index < config_.orbiters; ++index) {
    const double radius = 3.0 + 1.2 * static_cast<double>(index);
    const double angle = phaseOffset + 2.0 * std::numbers::pi * static_cast<double>(index) /
        static_cast<double>(config_.orbiters);
    const Vec2d position{radius * std::cos(angle), radius * std::sin(angle)};
    const double speed = std::sqrt(config_.gravitationalConstant * config_.centralMass / radius);
    const Vec2d velocity{-speed * std::sin(angle), speed * std::cos(angle)};
    particles_.push_back({index + 1, config_.orbiterMass, position, velocity});
  }
  // Shift all velocities into the center-of-mass frame so total momentum is zero.
  Vec2d momentum{};
  double totalMass = 0.0;
  for (const auto& particle : particles_) {
    momentum += particle.velocity * particle.mass;
    totalMass += particle.mass;
  }
  const Vec2d frameVelocity = momentum * (1.0 / totalMass);
  for (auto& particle : particles_) particle.velocity = particle.velocity - frameVelocity;
  timeSeconds_ = 0.0;
}

std::vector<Vec2d> ParticleGravitySystem::accelerations() const {
  std::vector<Vec2d> result(particles_.size());
  const double softeningSquared = config_.softening * config_.softening;
  for (size_t target = 0; target < particles_.size(); ++target) {
    for (size_t source = target + 1; source < particles_.size(); ++source) {
      const Vec2d delta = particles_[source].position - particles_[target].position;
      const double inverseDistance = 1.0 / std::sqrt(lengthSquared(delta) + softeningSquared);
      const double inverseDistanceCubed = inverseDistance * inverseDistance * inverseDistance;
      const Vec2d direction = delta * (config_.gravitationalConstant * inverseDistanceCubed);
      result[target] += direction * particles_[source].mass;
      result[source] += direction * -particles_[target].mass;
    }
  }
  return result;
}

void ParticleGravitySystem::step(uint32_t count) {
  for (uint32_t iteration = 0; iteration < count; ++iteration) {
    const auto firstAcceleration = accelerations();
    const double halfTimestep = 0.5 * config_.timestepSeconds;
    for (size_t index = 0; index < particles_.size(); ++index) {
      particles_[index].velocity += firstAcceleration[index] * halfTimestep;
      particles_[index].position += particles_[index].velocity * config_.timestepSeconds;
    }
    const auto secondAcceleration = accelerations();
    for (size_t index = 0; index < particles_.size(); ++index) {
      particles_[index].velocity += secondAcceleration[index] * halfTimestep;
    }
    timeSeconds_ += config_.timestepSeconds;
  }
}

ParticleGravityMetrics ParticleGravitySystem::metrics() const {
  ParticleGravityMetrics result{};
  double totalMass = 0.0;
  for (const auto& particle : particles_) {
    result.kineticEnergy += 0.5 * particle.mass * lengthSquared(particle.velocity);
    result.linearMomentum += particle.velocity * particle.mass;
    result.centerOfMass += particle.position * particle.mass;
    totalMass += particle.mass;
  }
  const double softeningSquared = config_.softening * config_.softening;
  for (size_t left = 0; left < particles_.size(); ++left) {
    for (size_t right = left + 1; right < particles_.size(); ++right) {
      const Vec2d delta = particles_[right].position - particles_[left].position;
      const double distance = std::sqrt(lengthSquared(delta) + softeningSquared);
      result.potentialEnergy -= config_.gravitationalConstant * particles_[left].mass * particles_[right].mass / distance;
    }
  }
  result.centerOfMass = result.centerOfMass * (1.0 / totalMass);
  result.totalEnergy = result.kineticEnergy + result.potentialEnergy;
  return result;
}

}  // namespace vulkax::sim
