#pragma once

#include "vulkax/physics/compute_ir.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vulkax::physics {

// A dense 3x3 tensor field is represented as nine canonical scalar programs
// in row-major order. This keeps tensor execution compatible with the existing
// scalar compiler while avoiding invasive changes to the stable fluid DSL.
struct Tensor3ComputeProgram {
  std::string outputName;
  std::array<ScalarComputeProgram, 9> components;
  uint64_t canonicalHash = 0;
};

struct TensorComputeLoweringResult {
  std::optional<Tensor3ComputeProgram> program;
  std::vector<ValidationIssue> issues;

  [[nodiscard]] bool valid() const { return program.has_value() && issues.empty(); }
};

[[nodiscard]] TensorComputeLoweringResult lowerTensor3Program(
    const PhysicsModel& model,
    std::string outputName,
    const std::array<equation::ScalarExpression, 9>& componentExpressions,
    std::vector<std::string> parameterNames = {});

[[nodiscard]] std::array<double, 9> executeTensor3Program(
    const Tensor3ComputeProgram& program,
    const std::array<double, 3>& coordinates,
    double timeSeconds,
    const std::map<std::string, double>& parameters = {});

// GPU representation is three packed float4 rows per output sample. xyz holds
// the tensor row and w is reserved for future metadata/alignment.
[[nodiscard]] std::string emitTensor3ProgramGlsl(const Tensor3ComputeProgram& program);
[[nodiscard]] std::string emitTensor3ProgramMsl(const Tensor3ComputeProgram& program);

}  // namespace vulkax::physics
