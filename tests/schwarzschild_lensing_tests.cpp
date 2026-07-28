#include "vulkax/relativity/schwarzschild_lensing.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  using namespace vulkax::relativity;
  const auto captured = integrateSchwarzschildRay(1.0, 5.0);
  assert(!captured.escaped);
  const auto weak = integrateSchwarzschildRay(1.0, 100.0);
  assert(weak.escaped);
  assert(weak.integrationSteps > 0);
  const double approximation = weakFieldDeflectionRadians(1.0, 100.0);
  assert(std::abs(weak.deflectionRadians - approximation) < 0.002);
  const auto strong = integrateSchwarzschildRay(1.0, 6.0);
  assert(strong.escaped);
  assert(strong.deflectionRadians > weak.deflectionRadians);
  SchwarzschildDeflectionLut lut(1.0, 32.0, 24, 0.002);
  assert(lut.captured(std::sqrt(27.0)));
  assert(!lut.captured(8.0));
  assert(lut.deflectionRadians(6.0) > lut.deflectionRadians(16.0));
  assert(std::abs(lut.deflectionRadians(32.0) - weakFieldDeflectionRadians(1.0, 32.0)) < 0.01);
  std::cout << "Vulkax Schwarzschild lensing tests passed\n";
  return 0;
}
