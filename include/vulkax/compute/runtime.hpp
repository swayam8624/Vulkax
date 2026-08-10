#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/compute/compute_ir.hpp"

#include <string>
#include <vector>

namespace vulkax::compute {

struct ExecutionResult {
    bool ok{false};
    backend::BackendKind backend{backend::BackendKind::CPUReference};
    std::string deviceName;
    std::string diagnostic;
    double wallMilliseconds{};
    std::vector<std::vector<float>> buffers;
};

[[nodiscard]] ExecutionResult executeReference(const ComputeProgram& program,
                                               std::vector<std::vector<float>> buffers);
[[nodiscard]] ExecutionResult executeWithBackend(backend::BackendKind kind,
                                                 const ComputeProgram& program,
                                                 std::vector<std::vector<float>> buffers);
[[nodiscard]] ExecutionResult executeBest(const ComputeProgram& program,
                                          std::vector<std::vector<float>> buffers);

} // namespace vulkax::compute
