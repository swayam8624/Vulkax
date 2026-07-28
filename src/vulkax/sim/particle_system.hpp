#pragma once

#include <cstdint>
#include <vector>

namespace vulkax::sim {

struct Vec2d {
  double x = 0.0;
  double y = 0.0;

  [[nodiscard]] Vec2d operator+(const Vec2d& rhs) const { return {x + rhs.x, y + rhs.y}; }
  [[nodiscard]] Vec2d operator-(const Vec2d& rhs) const { return {x - rhs.x, y - rhs.y}; }
  [[nodiscard]] Vec2d operator*(double scalar) const { return {x * scalar, y * scalar}; }
  Vec2d& operator+=(const Vec2d& rhs) { x += rhs.x; y += rhs.y; return *this; }
};

struct GravityParticle {
  uint32_t id = 0;
  double mass = 1.0;
  Vec2d position{};
  Vec2d velocity{};
};

struct ParticleGravityConfig {
  uint32_t orbiters = 6;
  double centralMass = 100.0;
  double orbiterMass = 1.0;
  double gravitationalConstant = 1.0;
  double softening = 0.05;
  double timestepSeconds = 1.0 / 240.0;
  uint32_t seed = 1337;
};

struct ParticleGravityMetrics {
  double kineticEnergy = 0.0;
  double potentialEnergy = 0.0;
  double totalEnergy = 0.0;
  Vec2d linearMomentum{};
  Vec2d centerOfMass{};
};

// Deterministic two-dimensional N-body reference using kick-drift-kick velocity
// Verlet integration. Softening is explicit to make close encounters finite.
class ParticleGravitySystem {
 public:
  explicit ParticleGravitySystem(ParticleGravityConfig config = {});

  void reset();
  void step(uint32_t count = 1);

  [[nodiscard]] const ParticleGravityConfig& config() const { return config_; }
  [[nodiscard]] const std::vector<GravityParticle>& particles() const { return particles_; }
  [[nodiscard]] ParticleGravityMetrics metrics() const;
  [[nodiscard]] double timeSeconds() const { return timeSeconds_; }

 private:
  [[nodiscard]] std::vector<Vec2d> accelerations() const;

  ParticleGravityConfig config_;
  std::vector<GravityParticle> particles_;
  double timeSeconds_ = 0.0;
};

}  // namespace vulkax::sim
