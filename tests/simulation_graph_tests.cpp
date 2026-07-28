#include "vulkax/sim/simulation_graph.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  using namespace vulkax::sim;
  SimulationGraph wave{{SimulationKind::Wave, 64, 64, 1.0f / 120.0f, 0.1f, 1337}};
  const auto initial = wave.metrics();
  wave.step(120);
  const auto advanced = wave.metrics();
  assert(std::isfinite(advanced.meanSquare));
  assert(advanced.maximum > 0.0f);
  assert(advanced.meanSquare != initial.meanSquare);
  assert(wave.primaryField().front() == 0.0f);
  assert(wave.glslComputeKernel().find("nextField") != std::string::npos);

  SimulationGraph reaction{{SimulationKind::ReactionDiffusion, 64, 64, 1.0f / 60.0f, 1.0f, 1337}};
  reaction.step(60);
  const auto reactionMetrics = reaction.metrics();
  assert(reactionMetrics.minimum >= 0.0f);
  assert(reactionMetrics.maximum <= 1.0f);
  assert(reaction.secondaryField().size() == reaction.primaryField().size());
  std::cout << "Vulkax simulation graph tests passed\n";
  return 0;
}
