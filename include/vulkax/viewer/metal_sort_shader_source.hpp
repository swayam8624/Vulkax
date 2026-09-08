#pragma once

namespace vulkax::viewer::metal {

inline constexpr char sortShaderSource[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct GPUSplat {
    float4 positionScaleX;
    float4 scaleYZOpacity;
    float4 rotation;
    float4 colorMark;
};

struct GPUDepthKey {
    float depth;
    uint sourceIndex;
};

struct GPUDepthUniforms {
    float4 cameraPosition;
    float4 cameraForward;
    uint4 countAndFlags;
};

struct BitonicParameters {
    uint j;
    uint k;
    uint validCount;
    uint paddedCount;
};

bool comesBefore(GPUDepthKey lhs, GPUDepthKey rhs) {
    if (lhs.depth > rhs.depth) return true;
    if (lhs.depth < rhs.depth) return false;
    return lhs.sourceIndex < rhs.sourceIndex;
}

kernel void generateDepthKeys(device const GPUSplat* splats [[buffer(0)]],
                              device const uint* order [[buffer(1)]],
                              device GPUDepthKey* output [[buffer(2)]],
                              constant GPUDepthUniforms& uniforms [[buffer(3)]],
                              uint gid [[thread_position_in_grid]]) {
    uint count = uniforms.countAndFlags.x;
    if (gid >= count) return;
    uint sourceIndex = order[gid];
    float3 delta = splats[sourceIndex].positionScaleX.xyz - uniforms.cameraPosition.xyz;
    output[gid].depth = dot(delta, uniforms.cameraForward.xyz);
    output[gid].sourceIndex = sourceIndex;
}

kernel void bitonicSortStep(device GPUDepthKey* keys [[buffer(0)]],
                            constant BitonicParameters& parameters [[buffer(1)]],
                            uint gid [[thread_position_in_grid]]) {
    if (gid >= parameters.paddedCount) return;
    uint partner = gid ^ parameters.j;
    if (partner <= gid || partner >= parameters.paddedCount) return;

    GPUDepthKey a = keys[gid];
    GPUDepthKey b = keys[partner];
    bool descending = (gid & parameters.k) == 0u;
    bool shouldSwap = descending ? comesBefore(b, a) : comesBefore(a, b);
    if (shouldSwap) {
        keys[gid] = b;
        keys[partner] = a;
    }
}
)METAL";

} // namespace vulkax::viewer::metal
