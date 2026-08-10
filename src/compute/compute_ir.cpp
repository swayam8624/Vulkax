#include "vulkax/compute/compute_ir.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::compute {

ProgramValidation validateProgram(const ComputeProgram& program) {
    ProgramValidation result;
    if (program.elementCount == 0) {
        result.errors.emplace_back("elementCount must be positive");
    }
    if (program.bufferCount == 0 || program.bufferCount > 4) {
        result.errors.emplace_back("bufferCount must be in [1, 4]");
    }
    if (program.instructions.empty()) {
        result.errors.emplace_back("program must contain at least one instruction");
    }
    for (std::size_t i = 0; i < program.instructions.size(); ++i) {
        const auto& op = program.instructions[i];
        const auto indexOk = [&](std::uint32_t index) { return index < program.bufferCount; };
        if (!indexOk(op.dst) || !indexOk(op.a)) {
            result.errors.emplace_back("instruction " + std::to_string(i) + " references an invalid buffer");
            continue;
        }
        if ((op.opcode == OpCode::Add || op.opcode == OpCode::Subtract ||
             op.opcode == OpCode::Multiply || op.opcode == OpCode::Axpy) &&
            !indexOk(op.b)) {
            result.errors.emplace_back("instruction " + std::to_string(i) + " references an invalid b buffer");
        }
        if (op.opcode == OpCode::Clamp && op.scalar0 > op.scalar1) {
            result.errors.emplace_back("clamp lower bound exceeds upper bound");
        }
    }
    return result;
}

std::vector<GpuInstruction> encodeGpuInstructions(const ComputeProgram& program) {
    const auto validation = validateProgram(program);
    if (!validation.ok()) {
        throw std::invalid_argument(validation.errors.front());
    }
    std::vector<GpuInstruction> encoded;
    encoded.reserve(program.instructions.size());
    for (const auto& instruction : program.instructions) {
        encoded.push_back({static_cast<std::uint32_t>(instruction.opcode), instruction.dst,
                           instruction.a, instruction.b, instruction.scalar0,
                           instruction.scalar1, 0.0F, 0.0F});
    }
    return encoded;
}

std::vector<std::vector<float>> executeCpu(const ComputeProgram& program,
                                           std::vector<std::vector<float>> buffers) {
    const auto validation = validateProgram(program);
    if (!validation.ok()) {
        throw std::invalid_argument(validation.errors.front());
    }
    if (buffers.size() != program.bufferCount) {
        throw std::invalid_argument("buffer vector count does not match program.bufferCount");
    }
    for (auto& buffer : buffers) {
        if (buffer.size() != program.elementCount) {
            throw std::invalid_argument("all buffers must match elementCount");
        }
    }

    for (const auto& op : program.instructions) {
        auto& dst = buffers[op.dst];
        const auto& a = buffers[op.a];
        const auto& b = buffers[op.b < buffers.size() ? op.b : 0U];
        for (std::uint32_t i = 0; i < program.elementCount; ++i) {
            switch (op.opcode) {
            case OpCode::Copy:
                dst[i] = a[i];
                break;
            case OpCode::Add:
                dst[i] = a[i] + b[i];
                break;
            case OpCode::Subtract:
                dst[i] = a[i] - b[i];
                break;
            case OpCode::Multiply:
                dst[i] = a[i] * b[i];
                break;
            case OpCode::Scale:
                dst[i] = op.scalar0 * a[i];
                break;
            case OpCode::Axpy:
                dst[i] = op.scalar0 * a[i] + b[i];
                break;
            case OpCode::Clamp:
                dst[i] = std::clamp(a[i], op.scalar0, op.scalar1);
                break;
            case OpCode::Laplacian1DPeriodic: {
                const auto left = i == 0 ? program.elementCount - 1 : i - 1;
                const auto right = (i + 1) % program.elementCount;
                dst[i] = (a[left] - 2.0F * a[i] + a[right]) * op.scalar0;
                break;
            }
            }
        }
    }
    return buffers;
}

const char* vulkanInterpreterGlsl() noexcept {
    return R"glsl(#version 450
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer B0 { float b0[]; };
layout(std430, binding = 1) buffer B1 { float b1[]; };
layout(std430, binding = 2) buffer B2 { float b2[]; };
layout(std430, binding = 3) buffer B3 { float b3[]; };
struct Op { uvec4 meta; vec4 params; };
layout(std430, binding = 4) readonly buffer Ops { Op ops[]; };
layout(push_constant) uniform Push { uint count; uint opIndex; } pc;
float loadB(uint id, uint i) {
    if (id == 0u) return b0[i];
    if (id == 1u) return b1[i];
    if (id == 2u) return b2[i];
    return b3[i];
}
void storeB(uint id, uint i, float v) {
    if (id == 0u) b0[i] = v;
    else if (id == 1u) b1[i] = v;
    else if (id == 2u) b2[i] = v;
    else b3[i] = v;
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    Op op = ops[pc.opIndex];
    uint code = op.meta.x;
    uint dst = op.meta.y;
    uint a = op.meta.z;
    uint b = op.meta.w;
    float av = loadB(a, i);
    float outv = av;
    if (code == 1u) outv = av + loadB(b, i);
    else if (code == 2u) outv = av - loadB(b, i);
    else if (code == 3u) outv = av * loadB(b, i);
    else if (code == 4u) outv = op.params.x * av;
    else if (code == 5u) outv = op.params.x * av + loadB(b, i);
    else if (code == 6u) outv = clamp(av, op.params.x, op.params.y);
    else if (code == 7u) {
        uint l = (i == 0u) ? pc.count - 1u : i - 1u;
        uint r = (i + 1u) % pc.count;
        outv = (loadB(a, l) - 2.0 * av + loadB(a, r)) * op.params.x;
    }
    storeB(dst, i, outv);
}
)glsl";
}

const char* metalInterpreterMsl() noexcept {
    return R"msl(#include <metal_stdlib>
using namespace metal;
struct Op { uint4 meta; float4 params; };
struct Push { uint count; uint opIndex; };
inline float loadB(uint id, uint i, device float* b0, device float* b1, device float* b2, device float* b3) {
    if (id == 0u) return b0[i];
    if (id == 1u) return b1[i];
    if (id == 2u) return b2[i];
    return b3[i];
}
inline void storeB(uint id, uint i, float v, device float* b0, device float* b1, device float* b2, device float* b3) {
    if (id == 0u) b0[i] = v;
    else if (id == 1u) b1[i] = v;
    else if (id == 2u) b2[i] = v;
    else b3[i] = v;
}
kernel void vulkax_compute_ir(device float* b0 [[buffer(0)]], device float* b1 [[buffer(1)]],
                              device float* b2 [[buffer(2)]], device float* b3 [[buffer(3)]],
                              device const Op* ops [[buffer(4)]], constant Push& pc [[buffer(5)]],
                              uint i [[thread_position_in_grid]]) {
    if (i >= pc.count) return;
    Op op = ops[pc.opIndex];
    uint code = op.meta.x, dst = op.meta.y, a = op.meta.z, b = op.meta.w;
    float av = loadB(a, i, b0, b1, b2, b3);
    float outv = av;
    if (code == 1u) outv = av + loadB(b, i, b0, b1, b2, b3);
    else if (code == 2u) outv = av - loadB(b, i, b0, b1, b2, b3);
    else if (code == 3u) outv = av * loadB(b, i, b0, b1, b2, b3);
    else if (code == 4u) outv = op.params.x * av;
    else if (code == 5u) outv = op.params.x * av + loadB(b, i, b0, b1, b2, b3);
    else if (code == 6u) outv = clamp(av, op.params.x, op.params.y);
    else if (code == 7u) {
        uint l = (i == 0u) ? pc.count - 1u : i - 1u;
        uint r = (i + 1u) % pc.count;
        outv = (loadB(a, l, b0, b1, b2, b3) - 2.0f * av + loadB(a, r, b0, b1, b2, b3)) * op.params.x;
    }
    storeB(dst, i, outv, b0, b1, b2, b3);
}
)msl";
}

} // namespace vulkax::compute
