#pragma once

#include "vulkax/equation/equation.hpp"
#include "vulkax/physics/physics_ir.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vulkax::physics {

enum class StencilOpcode : uint8_t {
  Constant,
  CoordinateX,
  CoordinateY,
  CoordinateZ,
  Time,
  DeltaTime,
  Parameter,
  FieldSample,
  Add,
  Subtract,
  Multiply,
  Divide,
  Power,
  Negate,
  Sin,
  Cos,
  Tan,
  Exp,
  Sqrt,
  Abs,
  Log,
  Min,
  Max,
  Clamp,
};

struct StencilInstruction {
  StencilOpcode opcode = StencilOpcode::Constant;
  std::array<uint32_t, 3> operands{};
  std::array<int8_t, 3> sampleOffset{};
  uint8_t operandCount = 0;
  double immediate = 0.0;
  uint32_t parameterIndex = 0;
  uint32_t fieldIndex = 0;
};

// One explicit Euler update for a scalar cell-centred field:
//
//   field(t + dt) = field(t) + dt * rhs(field, parameters)
//
// Differential functions in rhs are lowered to concrete centred finite-
// difference samples. The declared domain resolution is therefore part of the
// executable ABI and canonical hash.
struct ScalarEvolutionProgram {
  std::string field;
  Domain3D domain{};
  std::vector<std::string> inputFields;
  std::vector<BoundaryKind> inputBoundaries;
  std::vector<double> inputFixedBoundaryValues;
  BoundaryKind boundary = BoundaryKind::Open;
  double fixedBoundaryValue = 0.0;
  double defaultTimestepSeconds = 1.0 / 60.0;
  std::vector<std::string> parameterNames;
  std::vector<StencilInstruction> instructions;
  uint32_t outputRegister = 0;
  uint64_t canonicalHash = 0;
};

struct StencilLoweringResult {
  std::optional<ScalarEvolutionProgram> program;
  std::vector<ValidationIssue> issues;
  [[nodiscard]] bool valid() const { return program.has_value() && issues.empty(); }
};

struct ScalarEvolutionEquationSource {
  std::string field;
  equation::ScalarExpression rhs;
};

// A simultaneous explicit update of multiple scalar, cell-centred fields.
// Every equation reads the same old state and writes one field in the new
// state, which makes reaction systems deterministic and race-free on GPU.
struct CoupledScalarEvolutionProgram {
  Domain3D domain{};
  double defaultTimestepSeconds = 1.0 / 60.0;
  std::vector<std::string> fields;
  std::vector<BoundaryKind> boundaries;
  std::vector<double> fixedBoundaryValues;
  std::vector<std::string> parameterNames;
  std::vector<ScalarEvolutionProgram> equations;
  uint64_t canonicalHash = 0;
};

struct CoupledStencilLoweringResult {
  std::optional<CoupledScalarEvolutionProgram> program;
  std::vector<ValidationIssue> issues;
  [[nodiscard]] bool valid() const { return program.has_value() && issues.empty(); }
};

// Supported differential functions are laplacian(field), gradient_x(field),
// gradient_y(field), and gradient_z(field). The expression may also contain
// the evolved field, x/y/z/t, declared parameters, and the scalar operations
// supported by ScalarComputeProgram.
[[nodiscard]] StencilLoweringResult lowerScalarEvolutionProgram(
    const PhysicsModel& model,
    const std::string& field,
    const equation::ScalarExpression& rhs,
    std::vector<std::string> parameterNames = {});

[[nodiscard]] CoupledStencilLoweringResult lowerCoupledScalarEvolutionProgram(
    const PhysicsModel& model,
    const std::vector<ScalarEvolutionEquationSource>& equations,
    std::vector<std::string> parameterNames = {});

[[nodiscard]] std::vector<float> executeScalarEvolution3D(
    const ScalarEvolutionProgram& program,
    const std::vector<float>& input,
    double timestepSeconds,
    double timeSeconds,
    const std::map<std::string, double>& parameters = {});

[[nodiscard]] std::map<std::string, std::vector<float>> executeCoupledScalarEvolution3D(
    const CoupledScalarEvolutionProgram& program,
    const std::map<std::string, std::vector<float>>& input,
    double timestepSeconds,
    double timeSeconds,
    const std::map<std::string, double>& parameters = {});

// Bindings are input field 0, output field 1, and evolution parameters 2.
[[nodiscard]] std::string emitScalarEvolutionGlsl(const ScalarEvolutionProgram& program);
[[nodiscard]] std::string emitScalarEvolutionMsl(const ScalarEvolutionProgram& program);

// Coupled kernels use two interleaved buffers. Layout is
// values[fieldIndex * cellCount + cellIndex] for both old and new states.
[[nodiscard]] std::string emitCoupledScalarEvolutionGlsl(
    const CoupledScalarEvolutionProgram& program);
[[nodiscard]] std::string emitCoupledScalarEvolutionMsl(
    const CoupledScalarEvolutionProgram& program);

}  // namespace vulkax::physics
