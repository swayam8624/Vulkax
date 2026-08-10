#include "vulkax/equation/equation.hpp"
#include "vulkax/physics/tensor_compute_ir.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <map>
#include <string>

namespace {

bool near(double a, double b, double tolerance = 1e-10) {
  return std::abs(a - b) <= tolerance;
}

vulkax::physics::PhysicsModel model() {
  vulkax::physics::PhysicsModel value{};
  value.name = "tensor-compute-test";
  value.domain.minimum = {-2.0, -3.0, -4.0};
  value.domain.maximum = {2.0, 3.0, 4.0};
  value.domain.resolution = {32, 24, 8};
  value.solver.timestepSeconds = 1.0 / 60.0;
  return value;
}

}  // namespace

int main() {
  using vulkax::equation::parseScalarExpression;
  using namespace vulkax::physics;

  const std::array expressions{
      parseScalarExpression("x"),
      parseScalarExpression("y"),
      parseScalarExpression("z"),
      parseScalarExpression("x+y"),
      parseScalarExpression("x*y"),
      parseScalarExpression("sin(t)"),
      parseScalarExpression("a"),
      parseScalarExpression("a*x"),
      parseScalarExpression("1")};

  const auto lowered = lowerTensor3Program(model(), "stress", expressions, {"a"});
  assert(lowered.valid());
  assert(lowered.program->canonicalHash != 0);
  assert(lowered.program->components.size() == 9);
  for (const auto& component : lowered.program->components) {
    assert(component.parameterNames.size() == 1);
    assert(component.parameterNames.front() == "a");
  }

  const std::array<double, 3> position{0.5, -2.0, 1.25};
  const double time = 0.25;
  const std::map<std::string, double> parameters{{"a", 3.0}};
  const auto value = executeTensor3Program(*lowered.program, position, time, parameters);
  assert(near(value[0], 0.5));
  assert(near(value[1], -2.0));
  assert(near(value[2], 1.25));
  assert(near(value[3], -1.5));
  assert(near(value[4], -1.0));
  assert(near(value[5], std::sin(time)));
  assert(near(value[6], 3.0));
  assert(near(value[7], 1.5));
  assert(near(value[8], 1.0));

  const std::string glsl = emitTensor3ProgramGlsl(*lowered.program);
  assert(glsl.find("buffer TensorField") != std::string::npos);
  assert(glsl.find("outputField.rows[base + 2u]") != std::string::npos);
  assert(glsl.find("tensor3 Physics IR hash") != std::string::npos);

  const std::string msl = emitTensor3ProgramMsl(*lowered.program);
  assert(msl.find("kernel void tensor3Field") != std::string::npos);
  assert(msl.find("outputRows[base + 2u]") != std::string::npos);

  auto changedExpressions = expressions;
  changedExpressions[8] = parseScalarExpression("2");
  const auto changed = lowerTensor3Program(model(), "stress", changedExpressions, {"a"});
  assert(changed.valid());
  assert(changed.program->canonicalHash != lowered.program->canonicalHash);

  return 0;
}
