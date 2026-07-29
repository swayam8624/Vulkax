#include "vulkax/physics/physics_ir.hpp"
#include "vulkax/physics/compute_ir.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <cmath>

int main() {
  using namespace vulkax::physics;
  auto model = makeIncompressibleSmokeModel();
  const auto velocityDt = typeOf(model, {DifferentialOperator::TimeDerivative, "velocity"});
  const auto divergence = typeOf(model, {DifferentialOperator::Divergence, "velocity"});
  assert(velocityDt.has_value());
  assert(divergence.has_value());
  assert(velocityDt->valueType == ValueType::Vector3);
  assert(divergence->valueType == ValueType::Scalar);
  assert(divergence->dimension == Dimension::dimensionless().dividedBy(Dimension::time()));

  auto valid = validate(model);
  assert(valid.valid());

  model.fields.push_back({"velocity", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter});
  assert(!validate(model).valid());

  model = makeIncompressibleSmokeModel();
  model.constraints.front().lhs = {DifferentialOperator::Gradient, "velocity"};
  assert(!validate(model).valid());

  const auto parsed = parsePhysicsDsl(R"(
    model smoke
    field velocity: vector3 [m/s] face_x
    field pressure: scalar [pa] cell
    field divergence_source: scalar [1/s] cell
    field density: scalar [1] cell
    field temperature: scalar [1] cell
    domain -2 0 -2 2 6 2
    resolution 64 96 64
    constraint incompressibility: div(velocity) = divergence_source
    initial velocity = 0
    initial pressure = 0
    initial density = 0
    initial temperature = 0
    boundary velocity: no_slip
    boundary density: open
    solver advection maccormack pressure multigrid timestep 0.0166667
    visualize volume extinction 1.7 scattering 0.6 phase 0.25
  )");
  assert(parsed.valid());
  assert(parsed.model->visualization.volume);
  assert(parsed.model->domain.resolution[1] == 96);
  const auto graph = lowerToSolverGraph(*parsed.model);
  assert(graph.has_value());
  assert(graph->passes.size() == 8);
  assert(graph->initialConditions.size() == 4);
  assert(graph->initialConditions.front().field == "velocity");
  assert(graph->passes.back().kind == SolverPassKind::DeriveOpticalProperties);
  assert(graph->canonicalHash != 0);
  const auto repeatedGraph = lowerToSolverGraph(*parsed.model);
  assert(repeatedGraph->canonicalHash == graph->canonicalHash);

  const auto invalid = parsePhysicsDsl("field density: scalar [bogus] cell\n");
  assert(!invalid.valid());
  assert(!invalid.diagnostics.empty());
  assert(invalid.diagnostics.front().line == 1);

  const auto invalidInitial = parsePhysicsDsl("model invalid\nfield density: scalar [1] cell\ninitial missing = 0\n");
  assert(!invalidInitial.valid());

  PhysicsModel incomplete{};
  incomplete.name = "incomplete";
  incomplete.fields = {{"density", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter}};
  std::vector<ValidationIssue> loweringIssues;
  assert(!lowerToSolverGraph(incomplete, &loweringIssues).has_value());
  assert(!loweringIssues.empty());

  PhysicsModel fieldModel{};
  fieldModel.name = "wave-compute";
  fieldModel.domain.minimum = {-4.0, -2.0, -1.0};
  fieldModel.domain.maximum = {4.0, 2.0, 1.0};
  fieldModel.domain.resolution = {17, 9, 2};
  fieldModel.fields = {{"height", ValueType::Scalar, Dimension::length(), FieldPlacement::CellCenter}};
  const auto waveExpression = vulkax::equation::parseScalarExpression(
      "amplitude * sin(wavenumber * x - angularFrequency * t)");
  const auto compute = lowerScalarFieldProgram(
      fieldModel, "height", waveExpression,
      {"amplitude", "wavenumber", "angularFrequency"});
  assert(compute.valid());
  assert(compute.program->canonicalHash != 0);
  const std::map<std::string, double> waveParameters{
      {"amplitude", 1.25}, {"wavenumber", 2.0}, {"angularFrequency", 3.0}};
  const double sample = executeScalarProgram(*compute.program, {0.75, 0.0, 0.0}, 0.5, waveParameters);
  assert(std::abs(sample - 1.25 * std::sin(2.0 * 0.75 - 3.0 * 0.5)) < 1e-12);
  const auto field = executeScalarField2D(*compute.program, 17, 9, 0.5, waveParameters);
  assert(field.size() == 17 * 9);
  assert(std::abs(field[8] - static_cast<float>(1.25 * std::sin(-1.5))) < 1e-6f);
  const std::string glsl = emitScalarProgramGlsl(*compute.program);
  const std::string msl = emitScalarProgramMsl(*compute.program);
  assert(glsl.find("layout(std430, binding = 0)") != std::string::npos);
  assert(glsl.find("parameters.angularFrequency") != std::string::npos);
  assert(msl.find("kernel void executeScalarField") != std::string::npos);
  assert(msl.find("device float* outputField") != std::string::npos);

  const auto optimized = lowerScalarFieldProgram(
      fieldModel, "height", vulkax::equation::parseScalarExpression("x * x + x * x + (2 + 3)"));
  assert(optimized.valid());
  const auto multiplyCount = std::count_if(
      optimized.program->instructions.begin(), optimized.program->instructions.end(),
      [](const ScalarInstruction& instruction) { return instruction.opcode == ScalarOpcode::Multiply; });
  assert(multiplyCount == 1);
  assert(std::any_of(
      optimized.program->instructions.begin(), optimized.program->instructions.end(),
      [](const ScalarInstruction& instruction) {
        return instruction.opcode == ScalarOpcode::Constant && std::abs(instruction.immediate - 5.0) < 1e-12;
      }));

  const auto unbound = lowerScalarFieldProgram(
      fieldModel, "height", vulkax::equation::parseScalarExpression("missing * x"));
  assert(!unbound.valid());
  fieldModel.fields.front().valueType = ValueType::Vector3;
  assert(!lowerScalarFieldProgram(fieldModel, "height", waveExpression,
                                  {"amplitude", "wavenumber", "angularFrequency"}).valid());

  std::cout << "Vulkax typed and executable physics IR tests passed\n";
  return 0;
}
