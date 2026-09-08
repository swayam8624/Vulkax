#pragma once

namespace vulkax::viewer::metal {

inline constexpr char sortShaderSource[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct GPUDepthKey {
    float depth;
    uint sourceIndex;
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

kernel void bitonicSortStep(device GPUDepthKey* keys [[buffer(0)]],
                            constant BitonicParameters& parameters [[buffer(1)]],
                            uint gid [[thread_position_in_grid]]) {
    if (gid >= parameters.paddedCount) return;
    uint partner = gid ^ parameters.j;
    if (partner <= gid || partner >= parameters.paddedCount) return;

    GPUDepthKey a = keys[gid];
    GPUDepthKey b = keys[partner];
    bool descending = (gid & parameters.k) == 0u;
    bool aBeforeB = comesBefore(a, b);
    bool shouldSwap = descending ? !aBeforeB : aBeforeB;
    if (shouldSwap) {
        keys[gid] = b;
        keys[partner] = a;
    }
}
)METAL";

} // namespace vulkax::viewer::metal
