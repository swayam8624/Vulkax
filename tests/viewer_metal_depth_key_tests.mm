#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "vulkax/viewer/gpu_sort_contract.hpp"
#include "vulkax/viewer/metal_shader_source.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <simd/simd.h>

namespace {

struct GPUSplat {
    simd_float4 positionScaleX{};
    simd_float4 scaleYZOpacity{};
    simd_float4 rotation{};
    simd_float4 colorMark{};
};

struct MetalDepthKey {
    float depth{};
    std::uint32_t sourceIndex{};
};

struct MetalDepthUniforms {
    simd_float4 cameraPosition{};
    simd_float4 cameraForward{};
    simd_uint4 countAndFlags{};
};

static_assert(sizeof(MetalDepthKey) == sizeof(vulkax::viewer::GpuDepthKey));
static_assert(sizeof(MetalDepthKey) == 8U);

[[nodiscard]] std::string errorText(NSError* error) {
    if (error == nil || error.localizedDescription == nil) return "unknown Metal error";
    return std::string(error.localizedDescription.UTF8String);
}

[[nodiscard]] simd_float3 normalized(simd_float3 value) {
    const float length = simd_length(value);
    if (!(length > 0.0F)) throw std::runtime_error("zero camera direction in Metal depth-key test");
    return value / length;
}

[[nodiscard]] std::vector<std::uint32_t> cpuOrder(const std::vector<GPUSplat>& splats,
                                                   simd_float3 eye,
                                                   simd_float3 forward) {
    std::vector<std::uint32_t> order(splats.size());
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(), [&](std::uint32_t lhs, std::uint32_t rhs) {
        const simd_float3 lp = splats[lhs].positionScaleX.xyz;
        const simd_float3 rp = splats[rhs].positionScaleX.xyz;
        return simd_dot(lp - eye, forward) > simd_dot(rp - eye, forward);
    });
    return order;
}

[[nodiscard]] std::vector<vulkax::viewer::GpuDepthKey> cpuKeys(
    const std::vector<GPUSplat>& splats,
    const std::vector<std::uint32_t>& order,
    simd_float3 eye,
    simd_float3 forward) {
    std::vector<vulkax::viewer::GpuDepthKey> keys;
    keys.reserve(order.size());
    for (const auto sourceIndex : order) {
        const simd_float3 position = splats[sourceIndex].positionScaleX.xyz;
        keys.push_back({simd_dot(position - eye, forward), sourceIndex});
    }
    return keys;
}

[[nodiscard]] std::vector<vulkax::viewer::GpuDepthKey> runGpu(
    id<MTLDevice> device,
    id<MTLCommandQueue> queue,
    id<MTLComputePipelineState> pipeline,
    const std::vector<GPUSplat>& splats,
    const std::vector<std::uint32_t>& order,
    simd_float3 eye,
    simd_float3 forward) {
    if (order.empty()) return {};

    id<MTLBuffer> splatBuffer = [device newBufferWithBytes:splats.data()
                                                   length:splats.size() * sizeof(GPUSplat)
                                                  options:MTLResourceStorageModeShared];
    id<MTLBuffer> orderBuffer = [device newBufferWithBytes:order.data()
                                                   length:order.size() * sizeof(std::uint32_t)
                                                  options:MTLResourceStorageModeShared];
    id<MTLBuffer> outputBuffer = [device newBufferWithLength:order.size() * sizeof(MetalDepthKey)
                                                    options:MTLResourceStorageModeShared];
    if (!splatBuffer || !orderBuffer || !outputBuffer)
        throw std::runtime_error("failed to allocate Metal depth-key test buffers");

    MetalDepthUniforms uniforms;
    uniforms.cameraPosition = (simd_float4){eye.x, eye.y, eye.z, 1.0F};
    uniforms.cameraForward = (simd_float4){forward.x, forward.y, forward.z, 0.0F};
    uniforms.countAndFlags = (simd_uint4){static_cast<std::uint32_t>(order.size()), 0U, 0U, 0U};

    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!command || !encoder) throw std::runtime_error("failed to create Metal depth-key command encoder");

    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:splatBuffer offset:0 atIndex:0];
    [encoder setBuffer:orderBuffer offset:0 atIndex:1];
    [encoder setBuffer:outputBuffer offset:0 atIndex:2];
    [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:3];

    const NSUInteger threadWidth = std::max<NSUInteger>(
        1U, std::min<NSUInteger>(64U, pipeline.maxTotalThreadsPerThreadgroup));
    [encoder dispatchThreads:MTLSizeMake(order.size(), 1U, 1U)
       threadsPerThreadgroup:MTLSizeMake(threadWidth, 1U, 1U)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];

    if (command.status == MTLCommandBufferStatusError)
        throw std::runtime_error("Metal depth-key command failed: " + errorText(command.error));

    const auto* metalKeys = static_cast<const MetalDepthKey*>(outputBuffer.contents);
    std::vector<vulkax::viewer::GpuDepthKey> result;
    result.reserve(order.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        result.push_back({metalKeys[i].depth, metalKeys[i].sourceIndex});
    return result;
}

void validateCamera(id<MTLDevice> device,
                    id<MTLCommandQueue> queue,
                    id<MTLComputePipelineState> pipeline,
                    const std::vector<GPUSplat>& splats,
                    simd_float3 eye,
                    simd_float3 forward) {
    forward = normalized(forward);
    const auto order = cpuOrder(splats, eye, forward);
    const auto reference = cpuKeys(splats, order, eye, forward);
    const auto gpu = runGpu(device, queue, pipeline, splats, order, eye, forward);
    const auto validation = vulkax::viewer::validateGpuDepthKeys(gpu, reference, 2.0e-5);
    if (!validation.valid()) {
        std::cerr << "GPU depth validation failed: size=" << validation.sizeMatches
                  << " finite=" << validation.finiteDepths
                  << " indices=" << validation.sourceIndicesMatch
                  << " order=" << validation.backToFront
                  << " max_error=" << validation.maximumDepthError << '\n';
    }
    assert(validation.valid());
    assert(validation.maximumDepthError <= 2.0e-5);
}

} // namespace

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::cerr << "No Metal device available\n";
            return 1;
        }

        NSError* libraryError = nil;
        NSString* source = [NSString stringWithUTF8String:vulkax::viewer::metal::shaderSource];
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&libraryError];
        if (!library) {
            std::cerr << "Embedded Metal shader failed to compile: " << errorText(libraryError) << '\n';
            return 1;
        }

        id<MTLFunction> function = [library newFunctionWithName:@"generateDepthKeys"];
        if (!function) {
            std::cerr << "generateDepthKeys kernel missing from embedded Metal library\n";
            return 1;
        }

        NSError* pipelineError = nil;
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:function error:&pipelineError];
        if (!pipeline) {
            std::cerr << "Could not create Metal depth-key pipeline: " << errorText(pipelineError) << '\n';
            return 1;
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) {
            std::cerr << "Could not create Metal command queue\n";
            return 1;
        }

        std::vector<GPUSplat> splats(6);
        const simd_float3 positions[6] = {
            {0.0F, 0.0F, -2.0F},
            {2.0F, 0.0F, -8.0F},
            {0.0F, 1.0F, -5.0F},
            {-2.0F, 0.5F, -11.0F},
            {4.0F, -1.0F, -4.0F},
            {-3.0F, 2.0F, -7.5F},
        };
        for (std::size_t i = 0; i < splats.size(); ++i) {
            splats[i].positionScaleX =
                (simd_float4){positions[i].x, positions[i].y, positions[i].z, 0.05F};
            splats[i].scaleYZOpacity = (simd_float4){0.05F, 0.05F, 0.9F, 0.0F};
            splats[i].rotation = (simd_float4){0.0F, 0.0F, 0.0F, 1.0F};
        }

        validateCamera(device, queue, pipeline, splats,
                       (simd_float3){0.0F, 0.0F, 0.0F},
                       (simd_float3){0.0F, 0.0F, -1.0F});
        validateCamera(device, queue, pipeline, splats,
                       (simd_float3){1.5F, 2.0F, 3.0F},
                       (simd_float3){-1.5F, -2.0F, -4.0F});
        validateCamera(device, queue, pipeline, splats,
                       (simd_float3){-4.0F, 1.0F, -1.0F},
                       (simd_float3){3.0F, -0.5F, -5.0F});

        return 0;
    }
}
