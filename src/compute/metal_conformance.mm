#include "vulkax/compute/conformance.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace vulkax::compute {

ConformanceResult runMetalConformance(std::size_t elementCount) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            throw std::runtime_error("no Metal device available");
        }
        NSString* source = @R"metal(
            #include <metal_stdlib>
            using namespace metal;
            struct Parameters { float scale; float bias; uint count; };
            kernel void conformance(device float* values [[buffer(0)]],
                                    constant Parameters& parameters [[buffer(1)]],
                                    uint gid [[thread_position_in_grid]]) {
                if (gid < parameters.count) {
                    values[gid] = parameters.scale * values[gid] + parameters.bias;
                }
            }
        )metal";
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (library == nil) {
            throw std::runtime_error("Metal library compilation failed: " +
                                     std::string([[error localizedDescription] UTF8String]));
        }
        id<MTLFunction> function = [library newFunctionWithName:@"conformance"];
        if (function == nil) {
            throw std::runtime_error("Metal conformance function not found");
        }
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
        if (pipeline == nil) {
            throw std::runtime_error("Metal compute pipeline creation failed: " +
                                     std::string([[error localizedDescription] UTF8String]));
        }

        std::vector<float> input(elementCount);
        std::vector<float> expected(elementCount);
        constexpr float scale = 1.75F;
        constexpr float bias = -0.375F;
        for (std::size_t index = 0; index < elementCount; ++index) {
            const float x = static_cast<float>(index % 257u) * 0.03125F - 3.0F;
            input[index] = x;
            expected[index] = scale * x + bias;
        }
        const NSUInteger byteCount = static_cast<NSUInteger>(elementCount * sizeof(float));
        id<MTLBuffer> buffer = [device newBufferWithBytes:input.data()
                                                   length:byteCount
                                                  options:MTLResourceStorageModeShared];
        if (buffer == nil) {
            throw std::runtime_error("Metal buffer allocation failed");
        }
        struct Parameters {
            float scale;
            float bias;
            std::uint32_t count;
        } parameters{scale, bias, static_cast<std::uint32_t>(elementCount)};

        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:buffer offset:0 atIndex:0];
        [encoder setBytes:&parameters length:sizeof(parameters) atIndex:1];
        const NSUInteger width = std::min<NSUInteger>(64, pipeline.maxTotalThreadsPerThreadgroup);
        [encoder dispatchThreads:MTLSizeMake(static_cast<NSUInteger>(elementCount), 1, 1)
           threadsPerThreadgroup:MTLSizeMake(width, 1, 1)];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if (commandBuffer.status == MTLCommandBufferStatusError) {
            throw std::runtime_error("Metal conformance dispatch failed: " +
                                     std::string([[commandBuffer.error localizedDescription] UTF8String]));
        }

        const auto* values = static_cast<const float*>(buffer.contents);
        double maxAbsolute = 0.0;
        double maxRelative = 0.0;
        for (std::size_t index = 0; index < elementCount; ++index) {
            const double actual = static_cast<double>(values[index]);
            const double reference = static_cast<double>(expected[index]);
            const double absolute = std::abs(actual - reference);
            const double relative = absolute / std::max(1.0e-12, std::abs(reference));
            maxAbsolute = std::max(maxAbsolute, absolute);
            maxRelative = std::max(maxRelative, relative);
        }
        return {backend::BackendKind::Metal, std::string([[device name] UTF8String]), elementCount,
                maxAbsolute, maxRelative, maxAbsolute <= 1.0e-5};
    }
}

} // namespace vulkax::compute
