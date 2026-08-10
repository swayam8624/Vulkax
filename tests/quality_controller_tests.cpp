#include "vulkax/research/quality_controller.hpp"

#include <cassert>
#include <iostream>
#include <optional>

int main() {
  using namespace vulkax::research;

  QualityBudget budget{16.67, 0.01, 0.35, 1.0, 1, 8};
  budget.targetNumericalError = 0.005;

  QualityController equationAware{budget, QualityPolicy::EquationAware};
  equationAware.reset({1.0, 4, 2});
  // Unknown numerical quality is not silently treated as perfect agreement.
  for (int index = 0; index < 20; ++index) {
    equationAware.update({35.0, std::nullopt, 0.001, std::nullopt});
  }
  assert(!equationAware.numericalErrorEwma().has_value());
  assert(!equationAware.hasRequiredMeasurements());
  assert(equationAware.state().resolutionScale == 1.0);
  assert(equationAware.state().samplesPerPixel == 4);

  // With both error domains measured, timing pressure may trade quality down.
  for (int index = 0; index < 20; ++index) {
    equationAware.update({35.0, 0.001, 0.001, std::nullopt});
  }
  assert(equationAware.hasRequiredMeasurements());
  assert(equationAware.state().resolutionScale < 1.0);
  assert(equationAware.state().samplesPerPixel < 4);
  const uint32_t changesAfterPressure = equationAware.changeCount();

  // Numerical error pressure must push quality back upward instead of
  // confusing a slow frame with permission to reduce solver quality.
  for (int index = 0; index < 30; ++index) {
    equationAware.update({6.0, 0.03, 0.001, std::nullopt});
  }
  assert(equationAware.changeCount() > changesAfterPressure);

  QualityController screenSpace{budget, QualityPolicy::ScreenSpaceOnly};
  screenSpace.reset({1.0, 4, 1});
  for (int index = 0; index < 20; ++index) {
    screenSpace.update({35.0, std::nullopt, 0.001, std::nullopt});
  }
  assert(screenSpace.hasRequiredMeasurements());
  assert(screenSpace.state().resolutionScale < 1.0);

  QualityController numerical{budget, QualityPolicy::NumericalOnly};
  numerical.reset({1.0, 4, 1});
  for (int index = 0; index < 20; ++index) {
    numerical.update({35.0, 0.001, std::nullopt, std::nullopt});
  }
  assert(numerical.hasRequiredMeasurements());
  assert(numerical.state().resolutionScale < 1.0);

  QualityController fixed{budget, QualityPolicy::Fixed};
  fixed.reset({0.75, 3, 2});
  for (int index = 0; index < 50; ++index) {
    fixed.update({100.0, 1.0, 1.0, 1ull << 30u});
  }
  assert(fixed.state().resolutionScale == 0.75);
  assert(fixed.state().samplesPerPixel == 3);
  assert(fixed.state().simulationSubsteps == 2);
  assert(fixed.changeCount() == 0);

  QualityBudget memoryBudget = budget;
  memoryBudget.targetGpuMemoryBytes = 512ull * 1024ull * 1024ull;
  QualityController memoryAware{memoryBudget, QualityPolicy::ScreenSpaceOnly};
  memoryAware.reset({1.0, 4, 1});
  for (int index = 0; index < 20; ++index) {
    memoryAware.update({8.0, std::nullopt, 0.001, 1024ull * 1024ull * 1024ull});
  }
  assert(memoryAware.state().resolutionScale < 1.0);
  assert(memoryAware.gpuMemoryBytesEwma().has_value());

  std::cout << "Vulkax equation-aware quality controller tests passed\n";
  return 0;
}
