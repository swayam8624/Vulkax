#include "vulkax/equation/equation.hpp"
#include "vulkax/physics/generated_transport.hpp"

#include <cassert>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace {

vulkax::physics::PhysicsModel model() {
  using namespace vulkax::physics;
  PhysicsModel value{};
  value.name = "generated-transport-test";
  value.domain.minimum = {0.0, 0.0, 0.0};
  value.domain.maximum = {1.0, 1.0, 1.0};
  value.domain.resolution = {8, 8, 8};
  value.fields.push_back({
      "density", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter});
  value.boundaries.push_back({"density", BoundaryKind::Periodic, std::nullopt});
  value.solver.timestepSeconds = 1.0 / 30.0;
  return value;
}

bool near(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

}  // namespace

int main() {
  using namespace vulkax::physics;

  GeneratedTransportSpec spec{};
  spec.field = "density";
  spec.velocity = {1.0, -0.5, 0.25};
  spec.diffusivity = 0.05;
  spec.source = vulkax::equation::parseScalarExpression("source_gain * sin(t)");
  spec.parameterNames = {"source_gain"};

  const auto generated = generateTransportDiffusionPlan(model(), spec);
  assert(generated.valid());
  assert(generated.plan->canonicalHash != 0);
  assert(generated.plan->advectiveTimestepLimit > 0.0);
  assert(generated.plan->diffusiveTimestepLimit > 0.0);
  assert(generated.plan->recommendedTimestep > 0.0);
  assert(generated.plan->recommendedTimestep <= model().solver.timestepSeconds + 1e-12);
  assert(generated.plan->program.parameterNames.size() == 1);
  assert(generated.plan->program.parameterNames.front() == "source_gain");

  const std::string glsl = emitScalarEvolutionGlsl(generated.plan->program);
  const std::string msl = emitScalarEvolutionMsl(generated.plan->program);
  assert(glsl.find("gradient_x") == std::string::npos);  // lowered to concrete samples
  assert(glsl.find("sampleField") != std::string::npos);
  assert(msl.find("sampleField") != std::string::npos);

  // A spatially uniform field has zero centred gradient/laplacian. With source
  // gain zero it must remain exactly uniform under the generated update.
  const size_t cellCount = 8u * 8u * 8u;
  const std::vector<float> input(cellCount, 2.0f);
  const auto output = executeScalarEvolution3D(
      generated.plan->program,
      input,
      generated.plan->recommendedTimestep,
      0.25,
      {{"source_gain", 0.0}});
  assert(output.size() == input.size());
  for (const float value : output) assert(near(value, 2.0));

  // Pure transport has no diffusive restriction; pure diffusion has no
  // advective restriction. Both still cap at the model timestep.
  GeneratedTransportSpec advection{};
  advection.field = "density";
  advection.velocity = {2.0, 0.0, 0.0};
  const auto advective = generateTransportDiffusionPlan(model(), advection);
  assert(advective.valid());
  assert(std::isinf(advective.plan->diffusiveTimestepLimit));

  GeneratedTransportSpec diffusion{};
  diffusion.field = "density";
  diffusion.diffusivity = 0.2;
  const auto diffusive = generateTransportDiffusionPlan(model(), diffusion);
  assert(diffusive.valid());
  assert(std::isinf(diffusive.plan->advectiveTimestepLimit));

  GeneratedTransportSpec invalid = spec;
  invalid.diffusivity = -1.0;
  assert(!generateTransportDiffusionPlan(model(), invalid).valid());

  return 0;
}
