#include "vulkax/sim/buoyant_smoke.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  using namespace vulkax::sim;
  BuoyantSmokeConfig config{};
  config.width = 64;
  config.height = 64;
  config.pressureIterations = 50;

  BuoyantSmokeSimulation first(config);
  BuoyantSmokeSimulation second(config);
  first.step(120);
  second.step(120);

  assert(first.densityMass() > 1.0);
  assert(first.divergenceL2() < 0.03);
  assert(std::abs(first.densityMass() - second.densityMass()) < 1e-9);
  assert(std::abs(first.densityCenterY() - second.densityCenterY()) < 1e-9);
  assert(first.densityCenterY() < static_cast<double>(config.height) * 0.82);
  for (size_t index = 0; index < first.density().size(); ++index) {
    assert(std::isfinite(first.density()[index]));
    assert(std::isfinite(first.temperature()[index]));
    assert(std::abs(first.density()[index] - second.density()[index]) < 1e-9f);
  }
  std::cout << "Vulkax buoyant smoke tests passed: divergence=" << first.divergenceL2()
            << " centerY=" << first.densityCenterY() << '\n';
  return 0;
}
