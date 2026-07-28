#include "vulkax/research/quality_controller.hpp"

#include <cassert>
#include <iostream>

int main() {
  using namespace vulkax::research;
  QualityController controller{{16.67, 0.01, 0.35, 1.0, 1, 8}};
  controller.reset({1.0, 4, 1});
  for (int index = 0; index < 20; ++index) controller.update({35.0, 0.001, 0.001});
  assert(controller.state().resolutionScale < 1.0);
  assert(controller.state().samplesPerPixel < 4);
  const uint32_t changesAfterPressure = controller.changeCount();
  for (int index = 0; index < 20; ++index) controller.update({6.0, 0.001, 0.03});
  assert(controller.state().resolutionScale >= 0.35);
  assert(controller.state().samplesPerPixel >= 1);
  assert(controller.changeCount() > changesAfterPressure);
  std::cout << "Vulkax quality controller tests passed\n";
  return 0;
}
