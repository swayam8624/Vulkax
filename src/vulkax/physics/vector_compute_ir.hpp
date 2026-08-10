#pragma once

#include "vulkax/equation/equation.hpp"
#include "vulkax/physics/compute_ir.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vulkax::physics {

struct VectorComputeProgram {
  std::string outputField;
  ValueType valueType = ValueType::Vector3;
  std::vector<ScalarComputeProgram> components;
  uint64_t canonicalHash = 0;
};

struct VectorComputeLoweringResult {
  std::optional<VectorComputeProgram> program;
  std::vector<ValidationIssue> issues;
  [[nodiscard]] bool valid() const { return program.has_value() && issues.empty(); }
};

[[nodiscard]] VectorComputeLoweringResult lowerVectorFieldProgram(
    const PhysicsModel& model,
    const std::string& outputField,
    const std::vector<equation::ScalarExpression>& componentExpressions,
    std::vector<std::string> parameterNames = {});

[[nodiscard]] std::array<double, 3> executeVectorProgram(
    const VectorComputeProgram& program,
    const std::array<double, 3>& position,
    double timeSeconds,
    const std::map<std::string, double>& parameters = {});

// Vector shaders use binding 0 for tightly packed vec4/float4 output and the
// same binding-1 uniform/parameter ABI as scalar compute programs. Vector2
// writes z=0; all variants write w=0.
[[nodiscard]] std::string emitVectorProgramGlsl(const VectorComputeProgram& program);
[[nodiscard]] std::string emitVectorProgramMsl(const VectorComputeProgram& program);

}  // namespace vulkax::physics
