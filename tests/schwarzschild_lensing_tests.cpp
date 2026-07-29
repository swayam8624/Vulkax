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

  SchwarzschildGeodesicConfig geodesicConfig{};
  geodesicConfig.affineStep = 0.01;
  geodesicConfig.maximumAffineDistance = 400.0;
  const auto captured3d = integrateSchwarzschildGeodesic(
      geodesicConfig, {50.0, 0.0, 0.0}, {-1.0, 0.08, 0.04});
  assert(captured3d.captured);
  assert(!captured3d.escaped);
  assert(captured3d.minimumRadius <= 2.0001);
  assert(captured3d.maximumRelativeEnergyDrift < 1e-5);

  const auto escaped3d = integrateSchwarzschildGeodesic(
      geodesicConfig, {50.0, 0.0, 0.0}, {-1.0, 0.32, 0.18});
  assert(escaped3d.escaped);
  assert(!escaped3d.captured);
  assert(escaped3d.path.size() > 20);
  assert(escaped3d.maximumRelativeEnergyDrift < 1e-5);
  bool leftEquatorialPlane = false;
  for (const auto& point : escaped3d.path) leftEquatorialPlane = leftEquatorialPlane || std::abs(point.z) > 1e-6;
  assert(leftEquatorialPlane);
  std::cout << "Vulkax Schwarzschild lensing tests passed\n";
  return 0;
}
