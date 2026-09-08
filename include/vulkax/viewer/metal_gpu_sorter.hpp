#pragma once

#if defined(__APPLE__) && defined(__OBJC__)

#import <Metal/Metal.h>

#include "vulkax/core/math.hpp"
#include "vulkax/viewer/gpu_sort_contract.hpp"
#include "vulkax/viewer/metal_sort_shader_source.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <simd/simd.h>

namespace vulkax::viewer {

struct MetalGpuSortResult {
    bool success{};
    std::vector<std::uint32_t> order;
    std::vector<GpuDepthKey> depthKeys;
    double gpuSeconds{};
    std::string error;
};

class MetalGpuSorter {
public:
    explicit MetalGpuSorter(id<MTLDevice> device) : device_(device) {
        if (!device_) {
            error_ = "no Metal device";
            return;
        }
        queue_ = [device_ newCommandQueue];
        if (!queue_) {
            error_ = "could not create Metal GPU-sort command queue";
            return;
        }

        NSError* libraryError = nil;
        NSString* source = [NSString stringWithUTF8String:metal::sortShaderSource];
        id<MTLLibrary> library = [device_ newLibraryWithSource:source options:nil error:&libraryError];
        if (!library) {
            error_ = "Metal GPU-sort shader compilation failed: " + errorText(libraryError);
            return;
        }

        id<MTLFunction> depthFunction = [library newFunctionWithName:@"generateDepthKeys"];
        id<MTLFunction> sortFunction = [library newFunctionWithName:@"bitonicSortStep"];
        if (!depthFunction || !sortFunction) {
            error_ = "Metal GPU-sort shader is missing a required compute kernel";
            return;
        }

        NSError* depthError = nil;
        depthPipeline_ = [device_ newComputePipelineStateWithFunction:depthFunction error:&depthError];
        if (!depthPipeline_) {
            error_ = "Metal depth-key pipeline creation failed: " + errorText(depthError);
            return;
        }
        NSError* sortError = nil;
        sortPipeline_ = [device_ newComputePipelineStateWithFunction:sortFunction error:&sortError];
        if (!sortPipeline_) {
            error_ = "Metal bitonic-sort pipeline creation failed: " + errorText(sortError);
            return;
        }
        available_ = true;
    }

    [[nodiscard]] bool available() const noexcept { return available_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    [[nodiscard]] MetalGpuSortResult sort(id<MTLBuffer> splatBuffer,
                                          std::span<const std::uint32_t> retained,
                                          math::Vec3 cameraPosition,
                                          math::Vec3 cameraForward) const {
        MetalGpuSortResult result;
        if (!available_) {
            result.error = error_;
            return result;
        }
        if (retained.empty()) {
            result.success = true;
            return result;
        }
        if (!splatBuffer) {
            result.error = "Metal GPU-sort received a null splat buffer";
            return result;
        }
        if (retained.size() > std::numeric_limits<std::uint32_t>::max()) {
            result.error = "Metal GPU-sort retained set exceeds uint32 range";
            return result;
        }

        cameraForward = math::normalized(cameraForward);
        if (math::length(cameraForward) <= 1.0e-12) {
            result.error = "Metal GPU-sort received a zero camera-forward vector";
            return result;
        }

        const std::size_t paddedCount = nextPowerOfTwo(retained.size());
        if (paddedCount > std::numeric_limits<std::uint32_t>::max()) {
            result.error = "Metal GPU-sort padded set exceeds uint32 range";
            return result;
        }

        id<MTLBuffer> orderBuffer = [device_ newBufferWithBytes:retained.data()
                                                       length:retained.size_bytes()
                                                      options:MTLResourceStorageModeShared];
        const GpuDepthKey sentinel{-FLT_MAX, std::numeric_limits<std::uint32_t>::max()};
        std::vector<GpuDepthKey> initialKeys(paddedCount, sentinel);
        id<MTLBuffer> keyBuffer = [device_ newBufferWithBytes:initialKeys.data()
                                                     length:initialKeys.size() * sizeof(GpuDepthKey)
                                                    options:MTLResourceStorageModeShared];
        if (!orderBuffer || !keyBuffer) {
            result.error = "Metal GPU-sort buffer allocation failed";
            return result;
        }

        DepthUniforms depthUniforms;
        depthUniforms.cameraPosition = (simd_float4){static_cast<float>(cameraPosition.x),
                                                     static_cast<float>(cameraPosition.y),
                                                     static_cast<float>(cameraPosition.z), 1.0F};
        depthUniforms.cameraForward = (simd_float4){static_cast<float>(cameraForward.x),
                                                   static_cast<float>(cameraForward.y),
                                                   static_cast<float>(cameraForward.z), 0.0F};
        depthUniforms.countAndFlags =
            (simd_uint4){static_cast<std::uint32_t>(retained.size()), 0U, 0U, 0U};

        id<MTLCommandBuffer> command = [queue_ commandBuffer];
        if (!command) {
            result.error = "Metal GPU-sort command buffer creation failed";
            return result;
        }

        {
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            if (!encoder) {
                result.error = "Metal GPU-sort depth encoder creation failed";
                return result;
            }
            [encoder setComputePipelineState:depthPipeline_];
            [encoder setBuffer:splatBuffer offset:0 atIndex:0];
            [encoder setBuffer:orderBuffer offset:0 atIndex:1];
            [encoder setBuffer:keyBuffer offset:0 atIndex:2];
            [encoder setBytes:&depthUniforms length:sizeof(depthUniforms) atIndex:3];
            const NSUInteger width = threadWidth(depthPipeline_, 128U);
            [encoder dispatchThreads:MTLSizeMake(retained.size(), 1U, 1U)
               threadsPerThreadgroup:MTLSizeMake(width, 1U, 1U)];
            [encoder endEncoding];
        }

        for (std::uint32_t k = 2U; k <= static_cast<std::uint32_t>(paddedCount); k <<= 1U) {
            for (std::uint32_t j = k >> 1U; j > 0U; j >>= 1U) {
                BitonicParameters parameters;
                parameters.j = j;
                parameters.k = k;
                parameters.validCount = static_cast<std::uint32_t>(retained.size());
                parameters.paddedCount = static_cast<std::uint32_t>(paddedCount);

                id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                if (!encoder) {
                    result.error = "Metal GPU-sort bitonic encoder creation failed";
                    return result;
                }
                [encoder setComputePipelineState:sortPipeline_];
                [encoder setBuffer:keyBuffer offset:0 atIndex:0];
                [encoder setBytes:&parameters length:sizeof(parameters) atIndex:1];
                const NSUInteger width = threadWidth(sortPipeline_, 128U);
                [encoder dispatchThreads:MTLSizeMake(paddedCount, 1U, 1U)
                   threadsPerThreadgroup:MTLSizeMake(width, 1U, 1U)];
                [encoder endEncoding];
            }
        }

        [command commit];
        [command waitUntilCompleted];
        if (command.status == MTLCommandBufferStatusError) {
            result.error = "Metal GPU-sort command failed: " + errorText(command.error);
            return result;
        }
        if (command.GPUEndTime > command.GPUStartTime)
            result.gpuSeconds = command.GPUEndTime - command.GPUStartTime;

        const auto* keys = static_cast<const GpuDepthKey*>(keyBuffer.contents);
        result.depthKeys.assign(keys, keys + retained.size());
        result.order.reserve(retained.size());

        std::unordered_set<std::uint32_t> expected;
        expected.reserve(retained.size());
        for (const auto index : retained) expected.insert(index);
        std::unordered_set<std::uint32_t> seen;
        seen.reserve(retained.size());

        float previousDepth = std::numeric_limits<float>::infinity();
        for (const auto& key : result.depthKeys) {
            if (!std::isfinite(key.depth) || key.depth > previousDepth + 1.0e-5F ||
                !expected.contains(key.sourceIndex) || !seen.insert(key.sourceIndex).second) {
                result.error = "Metal GPU-sort output failed retained-set/order sanity validation";
                result.order.clear();
                result.depthKeys.clear();
                return result;
            }
            previousDepth = key.depth;
            result.order.push_back(key.sourceIndex);
        }
        if (seen.size() != expected.size()) {
            result.error = "Metal GPU-sort output did not preserve the retained source-index set";
            result.order.clear();
            result.depthKeys.clear();
            return result;
        }

        result.success = true;
        return result;
    }

private:
    struct DepthUniforms {
        simd_float4 cameraPosition{};
        simd_float4 cameraForward{};
        simd_uint4 countAndFlags{};
    };
    struct BitonicParameters {
        std::uint32_t j{};
        std::uint32_t k{};
        std::uint32_t validCount{};
        std::uint32_t paddedCount{};
    };

    static_assert(sizeof(GpuDepthKey) == 8U);
    static_assert(sizeof(DepthUniforms) == 48U);
    static_assert(sizeof(BitonicParameters) == 16U);

    [[nodiscard]] static std::size_t nextPowerOfTwo(std::size_t value) {
        std::size_t result = 1U;
        while (result < value) {
            if (result > std::numeric_limits<std::size_t>::max() / 2U)
                return std::numeric_limits<std::size_t>::max();
            result <<= 1U;
        }
        return result;
    }

    [[nodiscard]] static NSUInteger threadWidth(id<MTLComputePipelineState> pipeline,
                                                NSUInteger preferred) {
        return std::max<NSUInteger>(1U,
            std::min<NSUInteger>(preferred, pipeline.maxTotalThreadsPerThreadgroup));
    }

    [[nodiscard]] static std::string errorText(NSError* error) {
        if (error == nil || error.localizedDescription == nil) return "unknown Metal error";
        return std::string(error.localizedDescription.UTF8String);
    }

    __strong id<MTLDevice> device_{nil};
    __strong id<MTLCommandQueue> queue_{nil};
    __strong id<MTLComputePipelineState> depthPipeline_{nil};
    __strong id<MTLComputePipelineState> sortPipeline_{nil};
    bool available_{};
    std::string error_;
};

} // namespace vulkax::viewer

#endif
