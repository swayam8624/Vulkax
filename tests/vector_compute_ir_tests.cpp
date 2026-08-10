#include <cassert>
#include <cmath>
#include <iostream>
#include <map>

#include "vulkax/equation/equation.hpp"
#include "vulkax/physics/vector_compute_ir.hpp"

int main() {
  using namespace vulkax::physics;

  PhysicsModel model{};
  model.name = "vector-field-ir";
  model.domain.minimum = {-2.0, -2.0, -2.0};
  model.domain.maximum = {2.0, 2.0, 2.0};
  model.domain.resolution = {32, 24, 16};
  model.fields.push_back({
      "velocity",
      ValueType::Vector3,
      Dimension::dimensionless(),
      FieldPlacement::CellCenter});

  const std::vector<vulkax::equation::ScalarExpression> components{
      vulkax::equation::parseScalarExpression("-y"),
      vulkax::equation::parseScalarExpression("x"),
      vulkax::equation::parseScalarExpression("gain*z")};
  const auto lowered = lowerVectorFieldProgram(model, "velocity", components, {"gain"});
  assert(lowered.valid());
  assert(lowered.program->components.size() == 3);
  assert(lowered.program->canonicalHash != 0);

  const auto value = executeVectorProgram(
      *lowered.program, {2.0, 3.0, 4.0}, 0.25, {{"gain", 0.5}});
  assert(std::abs(value[0] + 3.0) < 1e-12);
  assert(std::abs(value[1] - 2.0) < 1e-12);
  assert(std::abs(value[2] - 2.0) < 1e-12);

  const std::string glsl = emitVectorProgramGlsl(*lowered.program);
  assert(glsl.find("buffer OutputField") != std::string::npos);
  assert(glsl.find("c0r") != std::string::npos);
  assert(glsl.find("c1r") != std::string::npos);
  assert(glsl.find("c2r") != std::string::npos);
  assert(glsl.find("vec4") != std::string::npos);

  const std::string msl = emitVectorProgramMsl(*lowered.program);
  assert(msl.find("kernel void vectorField") != std::string::npos);
  assert(msl.find("device float4* outputField") != std::string::npos);
  assert(msl.find("c2r") != std::string::npos);

  const auto wrongComponentCount = lowerVectorFieldProgram(
      model,
      "velocity",
      {vulkax::equation::parseScalarExpression("x"),
       vulkax::equation::parseScalarExpression("y")});
  assert(!wrongComponentCount.valid());

  PhysicsModel scalarModel = model;
  scalarModel.fields[0].valueType = ValueType::Scalar;
  const auto scalarTarget = lowerVectorFieldProgram(
      scalarModel, "velocity", components, {"gain"});
  assert(!scalarTarget.valid());

  std::cout << "Vulkax executable vector-field IR tests passed\n";
  return 0;
}
