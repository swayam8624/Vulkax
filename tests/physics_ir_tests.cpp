#include "vulkax/physics/physics_ir.hpp"
#include "vulkax/physics/compute_ir.hpp"
#include "vulkax/physics/stencil_ir.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <cmath>
#include <numbers>
#include <numeric>

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

  PhysicsModel diffusionModel{};
  diffusionModel.name = "periodic-diffusion";
  diffusionModel.domain.minimum = {0.0, 0.0, 0.0};
  diffusionModel.domain.maximum = {2.0 * std::numbers::pi, 1.0, 1.0};
  diffusionModel.domain.resolution = {24, 8, 8};
  diffusionModel.fields = {{"u", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter}};
  diffusionModel.boundaries = {{"u", BoundaryKind::Periodic, std::nullopt}};
  diffusionModel.solver.timestepSeconds = 0.0005;
  const auto diffusion = lowerScalarEvolutionProgram(
      diffusionModel, "u", vulkax::equation::parseScalarExpression(
          "diffusivity * laplacian(u) - decay * u"),
      {"diffusivity", "decay"});
  assert(diffusion.valid());
  assert(diffusion.program->canonicalHash != 0);

  const auto extent = diffusionModel.domain.resolution;
  std::vector<float> initial(static_cast<size_t>(extent[0]) * extent[1] * extent[2]);
  const double dx = (diffusionModel.domain.maximum[0] - diffusionModel.domain.minimum[0]) / extent[0];
  for (uint32_t z = 0; z < extent[2]; ++z) {
    for (uint32_t y = 0; y < extent[1]; ++y) {
      for (uint32_t x = 0; x < extent[0]; ++x) {
        initial[(static_cast<size_t>(z) * extent[1] + y) * extent[0] + x] =
            static_cast<float>(std::sin((x + 0.5) * dx));
      }
    }
  }
  constexpr double diffusivity = 0.15;
  constexpr double decay = 0.05;
  constexpr double dt = 0.0005;
  const auto evolved = executeScalarEvolution3D(
      *diffusion.program, initial, dt, 0.0,
      {{"diffusivity", diffusivity}, {"decay", decay}});
  const double discreteEigenvalue = 2.0 * (std::cos(dx) - 1.0) / (dx * dx);
  const double expectedScale = 1.0 + dt * (diffusivity * discreteEigenvalue - decay);
  double maximumDiffusionError = 0.0;
  for (size_t index = 0; index < evolved.size(); ++index) {
    maximumDiffusionError = std::max(
        maximumDiffusionError,
        std::abs(static_cast<double>(evolved[index]) - expectedScale * initial[index]));
  }
  assert(maximumDiffusionError < 2e-7);
  assert(std::abs(std::accumulate(evolved.begin(), evolved.end(), 0.0) -
                  expectedScale * std::accumulate(initial.begin(), initial.end(), 0.0)) < 1e-5);

  const std::string diffusionGlsl = emitScalarEvolutionGlsl(*diffusion.program);
  const std::string diffusionMsl = emitScalarEvolutionMsl(*diffusion.program);
  assert(diffusionGlsl.find("readonly buffer FieldInput") != std::string::npos);
  assert(diffusionGlsl.find("cell = ivec3((cell.x %") != std::string::npos);
  assert(diffusionMsl.find("kernel void executeScalarEvolution") != std::string::npos);
  assert(diffusionMsl.find("parameters.diffusivity") != std::string::npos);

  PhysicsModel fixedModel = diffusionModel;
  fixedModel.name = "fixed-diffusion";
  fixedModel.domain.maximum = {1.0, 1.0, 1.0};
  fixedModel.domain.resolution = {5, 5, 5};
  fixedModel.boundaries = {{"u", BoundaryKind::FixedValue, 0.0}};
  const auto fixed = lowerScalarEvolutionProgram(
      fixedModel, "u", vulkax::equation::parseScalarExpression("laplacian(u)"));
  assert(fixed.valid());
  std::vector<float> constantField(125, 1.0f);
  const auto fixedStep = executeScalarEvolution3D(*fixed.program, constantField, 0.001, 0.0);
  assert(fixedStep[0] < 1.0f);
  assert(std::abs(fixedStep[62] - 1.0f) < 1e-6f);

  fixedModel.name = "open-diffusion";
  fixedModel.boundaries = {{"u", BoundaryKind::Open, std::nullopt}};
  const auto open = lowerScalarEvolutionProgram(
      fixedModel, "u", vulkax::equation::parseScalarExpression("laplacian(u)"));
  assert(open.valid());
  const auto openStep = executeScalarEvolution3D(*open.program, constantField, 0.001, 0.0);
  assert(std::all_of(openStep.begin(), openStep.end(), [](float value) {
    return std::abs(value - 1.0f) < 1e-6f;
  }));

  fixedModel.boundaries = {{"u", BoundaryKind::NoSlip, std::nullopt}};
  assert(!lowerScalarEvolutionProgram(
      fixedModel, "u", vulkax::equation::parseScalarExpression("laplacian(u)")).valid());
  assert(!lowerScalarEvolutionProgram(
      diffusionModel, "u", vulkax::equation::parseScalarExpression("laplacian(missing)")).valid());

  PhysicsModel reactionModel{};
  reactionModel.name = "coupled-gray-scott";
  reactionModel.domain.minimum = {0.0, 0.0, 0.0};
  reactionModel.domain.maximum = {1.0, 1.0, 1.0};
  reactionModel.domain.resolution = {12, 8, 4};
  reactionModel.fields = {
      {"a", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter},
      {"b", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter}};
  reactionModel.boundaries = {
      {"a", BoundaryKind::Periodic, std::nullopt},
      {"b", BoundaryKind::Periodic, std::nullopt}};
  reactionModel.solver.timestepSeconds = 0.001;
  const auto reaction = lowerCoupledScalarEvolutionProgram(
      reactionModel,
      {
          {"a", vulkax::equation::parseScalarExpression(
                    "diffusion_a * laplacian(a) - a*b*b + feed*(1-a)")},
          {"b", vulkax::equation::parseScalarExpression(
                    "diffusion_b * laplacian(b) + a*b*b - (feed+kill)*b")},
      },
      {"diffusion_a", "diffusion_b", "feed", "kill"});
  assert(reaction.valid());
  assert(reaction.program->canonicalHash != 0);
  assert(reaction.program->equations.size() == 2);
  assert(std::any_of(
      reaction.program->equations[0].instructions.begin(),
      reaction.program->equations[0].instructions.end(),
      [](const StencilInstruction& instruction) {
        return instruction.opcode == StencilOpcode::FieldSample && instruction.fieldIndex == 1;
      }));

  const size_t reactionCells = 12u * 8u * 4u;
  const std::map<std::string, std::vector<float>> reactionInput{
      {"a", std::vector<float>(reactionCells, 0.9f)},
      {"b", std::vector<float>(reactionCells, 0.2f)}};
  const std::map<std::string, double> reactionParameters{
      {"diffusion_a", 1.0}, {"diffusion_b", 0.5}, {"feed", 0.0367}, {"kill", 0.0649}};
  const auto reactionOutput = executeCoupledScalarEvolution3D(
      *reaction.program, reactionInput, 0.001, 0.0, reactionParameters);
  const double reactionRate = 0.9 * 0.2 * 0.2;
  const double expectedA = 0.9 + 0.001 * (-reactionRate + 0.0367 * 0.1);
  const double expectedB = 0.2 + 0.001 * (reactionRate - (0.0367 + 0.0649) * 0.2);
  assert(std::all_of(reactionOutput.at("a").begin(), reactionOutput.at("a").end(),
                     [&](float value) { return std::abs(value - expectedA) < 1e-6; }));
  assert(std::all_of(reactionOutput.at("b").begin(), reactionOutput.at("b").end(),
                     [&](float value) { return std::abs(value - expectedB) < 1e-6; }));

  const std::string reactionGlsl = emitCoupledScalarEvolutionGlsl(*reaction.program);
  const std::string reactionMsl = emitCoupledScalarEvolutionMsl(*reaction.program);
  assert(reactionGlsl.find("readonly buffer CoupledInput") != std::string::npos);
  assert(reactionGlsl.find("sampleField(1u") != std::string::npos);
  assert(reactionMsl.find("kernel void executeCoupledScalarEvolution") != std::string::npos);
  assert(reactionMsl.find("inputState[field * cells") != std::string::npos);
  assert(!lowerCoupledScalarEvolutionProgram(
      reactionModel,
      {{"a", vulkax::equation::parseScalarExpression("laplacian(missing)")},
       {"b", vulkax::equation::parseScalarExpression("a")}}).valid());

  std::cout << "Vulkax typed and executable physics IR tests passed\n";
  return 0;
}
