#include "vulkax/relativity/schwarzschild_lensing.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  using namespace vulkax::relativity;
  SchwarzschildThinDiskRenderer renderer{};
  const auto shadow = renderer.sample(0.0, 0.0);
  assert(shadow.captured);
  assert(!shadow.diskHit);

  uint32_t diskHits = 0;
  uint32_t luminous = 0;
  double maximum = 0.0;
  for (uint32_t y = 0; y < 120; ++y) {
    for (uint32_t x = 0; x < 214; ++x) {
      const double screenX = (static_cast<double>(x) / 213.0 - 0.5) * 2.0;
      const double screenY = (static_cast<double>(y) / 119.0 - 0.5) * 2.0;
      const auto sample = renderer.sample(screenX, screenY, 0.35);
      const double radiance = sample.red + sample.green + sample.blue;
      diskHits += sample.diskHit ? 1U : 0U;
      luminous += radiance > 0.02 ? 1U : 0U;
      maximum = std::max(maximum, radiance);
      assert(std::isfinite(radiance));
    }
  }
  assert(diskHits > 300);
  assert(luminous > 500);
  assert(maximum > 0.5);
  const auto near = renderer.sample(0.36, 0.0);
  const auto far = renderer.sample(0.82, 0.0);
  assert(!near.captured && !far.captured);
  assert(near.deflectionRadians > far.deflectionRadians);
  std::cout << "Vulkax Schwarzschild thin disk tests passed: diskHits=" << diskHits << '\n';
  return 0;
}
