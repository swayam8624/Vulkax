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

enum class ScalarOpcode : uint8_t {
  Constant,
  CoordinateX,
  CoordinateY,
  CoordinateZ,
  Time,
  Parameter,
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

struct ScalarInstruction {
  ScalarOpcode opcode = ScalarOpcode::Constant;
  std::array<uint32_t, 3> operands{};
  uint8_t operandCount = 0;
  double immediate = 0.0;
  uint32_t parameterIndex = 0;
};

// Backend-neutral straight-line scalar compute program. Coordinates are
// evaluated in the declared physical domain, not a backend-specific fixed
// interval. Every instruction produces one scalar register.
struct ScalarComputeProgram {
  std::string outputField;
  Domain3D domain{};
  std::vector<std::string> parameterNames;
  std::vector<ScalarInstruction> instructions;
  uint32_t outputRegister = 0;
  uint64_t canonicalHash = 0;
};

struct ComputeLoweringResult {
  std::optional<ScalarComputeProgram> program;
  std::vector<ValidationIssue> issues;
  [[nodiscard]] bool valid() const { return program.has_value() && issues.empty(); }
};

// Lowers one canonical equation AST into executable Physics IR. The output
// must name a scalar, cell-centred field in the typed model. Symbols other
// than x, y, z, t must be declared in parameterNames.
[[nodiscard]] ComputeLoweringResult lowerScalarFieldProgram(
    const PhysicsModel& model,
    const std::string& outputField,
    const equation::ScalarExpression& expression,
    std::vector<std::string> parameterNames = {});

[[nodiscard]] double executeScalarProgram(
    const ScalarComputeProgram& program,
    const std::array<double, 3>& position,
    double timeSeconds,
    const std::map<std::string, double>& parameters = {});

[[nodiscard]] std::vector<float> executeScalarField2D(
    const ScalarComputeProgram& program,
    uint32_t width,
    uint32_t height,
    double timeSeconds,
    const std::map<std::string, double>& parameters = {});

// Shader contracts use binding 0 for a tightly packed scalar output buffer
// and binding 1 for width, height, time, then scalar parameters in declaration
// order. This matches the checked Vulkan executor ABI.
[[nodiscard]] std::string emitScalarProgramGlsl(const ScalarComputeProgram& program);
[[nodiscard]] std::string emitScalarProgramMsl(const ScalarComputeProgram& program);

}  // namespace vulkax::physics
