#pragma once

#include "vulkax/physics/compute_ir.hpp"

#include <array>
#include <cstdint>
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
  std::vector<ComputeIrIssue> issues;

  [[nodiscard]] bool valid() const { return program.has_value() && issues.empty(); }
};

TensorComputeLoweringResult lowerTensor3Program(
    const PhysicsModel& model,
    std::string outputName,
    const std::array<equation::ScalarExpression, 9>& componentExpressions,
    std::vector<std::string> parameterNames = {});

std::array<double, 9> executeTensor3Program(
    const Tensor3ComputeProgram& program,
    std::array<double, 3> coordinates,
    double timeSeconds,
    const std::vector<double>& parameters);

std::string emitTensor3ProgramGlsl(
    const Tensor3ComputeProgram& program,
    uint32_t outputBinding = 0,
    uint32_t localSizeX = 8,
    uint32_t localSizeY = 8,
    uint32_t localSizeZ = 1);

std::string emitTensor3ProgramMsl(
    const Tensor3ComputeProgram& program,
    uint32_t outputBufferIndex = 0);

}  // namespace vulkax::physics
