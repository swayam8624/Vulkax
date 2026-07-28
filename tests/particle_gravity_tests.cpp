#include "vulkax/sim/particle_system.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  using namespace vulkax::sim;
  ParticleGravitySystem first{{6, 100.0, 1.0, 1.0, 0.05, 1.0 / 480.0, 1337}};
  ParticleGravitySystem second{{6, 100.0, 1.0, 1.0, 0.05, 1.0 / 480.0, 1337}};
  const auto initial = first.metrics();
  assert(std::abs(initial.linearMomentum.x) < 1e-12);
  assert(std::abs(initial.linearMomentum.y) < 1e-12);
  first.step(4'800);
  second.step(4'800);
  const auto advanced = first.metrics();
  assert(std::isfinite(advanced.totalEnergy));
  assert(std::abs(advanced.linearMomentum.x) < 1e-10);
  assert(std::abs(advanced.linearMomentum.y) < 1e-10);
  // Velocity Verlet should bound energy drift for this fixed, softened test system.
  assert(std::abs(advanced.totalEnergy - initial.totalEnergy) / std::abs(initial.totalEnergy) < 0.02);
  assert(first.particles().size() == second.particles().size());
  for (size_t index = 0; index < first.particles().size(); ++index) {
    assert(first.particles()[index].position.x == second.particles()[index].position.x);
    assert(first.particles()[index].position.y == second.particles()[index].position.y);
  }
  std::cout << "Vulkax particle gravity tests passed\n";
  return 0;
}
