#include "vulkax/compute/runtime.hpp"

#include "vulkax/backend/probe.hpp"

#include <chrono>
#include <exception>

#ifndef VULKAX_HAS_VULKAN_RUNTIME
#define VULKAX_HAS_VULKAN_RUNTIME 0
#endif
#ifndef VULKAX_HAS_METAL_RUNTIME
#define VULKAX_HAS_METAL_RUNTIME 0
#endif

namespace vulkax::compute {

#if VULKAX_HAS_VULKAN_RUNTIME
ExecutionResult executeVulkan(const ComputeProgram&, std::vector<std::vector<float>>);
#endif
#if VULKAX_HAS_METAL_RUNTIME
ExecutionResult executeMetal(const ComputeProgram&, std::vector<std::vector<float>>);
#endif

ExecutionResult executeReference(const ComputeProgram& program,
                                 std::vector<std::vector<float>> buffers) {
    ExecutionResult result;
    result.backend = backend::BackendKind::CPUReference;
    result.deviceName = "CPU reference";
    const auto start = std::chrono::steady_clock::now();
    try {
        result.buffers = executeCpu(program, std::move(buffers));
        result.ok = true;
    } catch (const std::exception& error) {
        result.diagnostic = error.what();
    }
    const auto end = std::chrono::steady_clock::now();
    result.wallMilliseconds = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

ExecutionResult executeWithBackend(backend::BackendKind kind, const ComputeProgram& program,
                                   std::vector<std::vector<float>> buffers) {
    switch (kind) {
    case backend::BackendKind::CPUReference:
        return executeReference(program, std::move(buffers));
    case backend::BackendKind::Vulkan:
#if VULKAX_HAS_VULKAN_RUNTIME
        return executeVulkan(program, std::move(buffers));
#else
        return {false, kind, {}, "Vulkan compute runtime was not built", 0.0, {}};
#endif
    case backend::BackendKind::Metal:
#if VULKAX_HAS_METAL_RUNTIME
        return executeMetal(program, std::move(buffers));
#else
        return {false, kind, {}, "Metal compute runtime was not built", 0.0, {}};
#endif
    case backend::BackendKind::OpenGL:
        return {false, kind, {}, "OpenGL compute runtime is not implemented yet", 0.0, {}};
    }
    return {false, kind, {}, "unknown backend", 0.0, {}};
}

ExecutionResult executeBest(const ComputeProgram& program, std::vector<std::vector<float>> buffers) {
    backend::WorkloadRequirements requirements;
    requirements.requiredFeatures = {backend::Feature::StorageBuffers};
    const auto candidates = backend::probeAvailableBackends();
    const auto selection = backend::selectBackend(candidates, requirements, backend::currentPlatform());
    if (selection.kind) {
        auto gpu = executeWithBackend(*selection.kind, program, buffers);
        if (gpu.ok) {
            return gpu;
        }
    }
    return executeReference(program, std::move(buffers));
}

} // namespace vulkax::compute
