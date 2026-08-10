#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vulkax::compute {

enum class OpCode : std::uint32_t {
    Copy = 0,
    Add = 1,
    Subtract = 2,
    Multiply = 3,
    Scale = 4,
    Axpy = 5,
    Clamp = 6,
    Laplacian1DPeriodic = 7,
};

struct Instruction {
    OpCode opcode{OpCode::Copy};
    std::uint32_t dst{};
    std::uint32_t a{};
    std::uint32_t b{};
    float scalar0{1.0F};
    float scalar1{};
};

struct ComputeProgram {
    std::uint32_t elementCount{};
    std::uint32_t bufferCount{};
    std::vector<Instruction> instructions;
};

struct ProgramValidation {
    std::vector<std::string> errors;
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

// Binary layout shared by the Vulkan and Metal interpreter kernels.
struct alignas(16) GpuInstruction {
    std::uint32_t opcode{};
    std::uint32_t dst{};
    std::uint32_t a{};
    std::uint32_t b{};
    float scalar0{};
    float scalar1{};
    float pad0{};
    float pad1{};
};

static_assert(sizeof(GpuInstruction) == 32);

[[nodiscard]] ProgramValidation validateProgram(const ComputeProgram& program);
[[nodiscard]] std::vector<GpuInstruction> encodeGpuInstructions(const ComputeProgram& program);
[[nodiscard]] std::vector<std::vector<float>> executeCpu(
    const ComputeProgram& program, std::vector<std::vector<float>> buffers);
[[nodiscard]] const char* vulkanInterpreterGlsl() noexcept;
[[nodiscard]] const char* metalInterpreterMsl() noexcept;

} // namespace vulkax::compute
