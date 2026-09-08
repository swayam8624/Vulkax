#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "vulkax/viewer/metal_sort_shader_source.hpp"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <simd/simd.h>

namespace {

struct DepthKey {
    float depth{};
    std::uint32_t sourceIndex{};
};

struct BitonicParameters {
    std::uint32_t j{};
    std::uint32_t k{};
    std::uint32_t validCount{};
    std::uint32_t paddedCount{};
};

static_assert(sizeof(DepthKey) == 8U);
static_assert(sizeof(BitonicParameters) == 16U);

[[nodiscard]] std::string errorText(NSError* error) {
    if (error == nil || error.localizedDescription == nil) return "unknown Metal error";
    return std::string(error.localizedDescription.UTF8String);
}

[[nodiscard]] std::size_t nextPowerOfTwo(std::size_t value) {
    if (value <= 1U) return 1U;
    std::size_t result = 1U;
    while (result < value) {
        if (result > std::numeric_limits<std::size_t>::max() / 2U)
            throw std::overflow_error("bitonic test size overflow");
        result <<= 1U;
    }
    return result;
}

[[nodiscard]] bool comesBefore(const DepthKey& lhs, const DepthKey& rhs) {
    if (lhs.depth > rhs.depth) return true;
    if (lhs.depth < rhs.depth) return false;
    return lhs.sourceIndex < rhs.sourceIndex;
}

[[nodiscard]] std::vector<DepthKey> gpuSort(id<MTLDevice> device,
                                            id<MTLCommandQueue> queue,
                                            id<MTLComputePipelineState> pipeline,
                                            const std::vector<DepthKey>& input) {
    if (input.empty()) return {};
    const std::size_t paddedCount = nextPowerOfTwo(input.size());
    if (paddedCount > std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("bitonic test exceeds uint32 range");

    const DepthKey sentinel{-FLT_MAX, std::numeric_limits<std::uint32_t>::max()};
    std::vector<DepthKey> padded(paddedCount, sentinel);
    std::copy(input.begin(), input.end(), padded.begin());

    id<MTLBuffer> buffer = [device newBufferWithBytes:padded.data()
                                               length:padded.size() * sizeof(DepthKey)
                                              options:MTLResourceStorageModeShared];
    if (!buffer) throw std::runtime_error("failed to allocate Metal bitonic-sort buffer");

    id<MTLCommandBuffer> command = [queue commandBuffer];
    if (!command) throw std::runtime_error("failed to create Metal bitonic-sort command buffer");

    const NSUInteger threadWidth = std::max<NSUInteger>(
        1U, std::min<NSUInteger>(128U, pipeline.maxTotalThreadsPerThreadgroup));

    for (std::uint32_t k = 2U; k <= static_cast<std::uint32_t>(paddedCount); k <<= 1U) {
        for (std::uint32_t j = k >> 1U; j > 0U; j >>= 1U) {
            BitonicParameters parameters;
            parameters.j = j;
            parameters.k = k;
            parameters.validCount = static_cast<std::uint32_t>(input.size());
            parameters.paddedCount = static_cast<std::uint32_t>(paddedCount);

            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            if (!encoder) throw std::runtime_error("failed to create Metal bitonic-sort encoder");
            [encoder setComputePipelineState:pipeline];
            [encoder setBuffer:buffer offset:0 atIndex:0];
            [encoder setBytes:&parameters length:sizeof(parameters) atIndex:1];
            [encoder dispatchThreads:MTLSizeMake(paddedCount, 1U, 1U)
               threadsPerThreadgroup:MTLSizeMake(threadWidth, 1U, 1U)];
            [encoder endEncoding];
        }
    }

    [command commit];
    [command waitUntilCompleted];
    if (command.status == MTLCommandBufferStatusError)
        throw std::runtime_error("Metal bitonic sort failed: " + errorText(command.error));

    const auto* output = static_cast<const DepthKey*>(buffer.contents);
    return std::vector<DepthKey>(output, output + input.size());
}

void verifyCase(id<MTLDevice> device,
                id<MTLCommandQueue> queue,
                id<MTLComputePipelineState> pipeline,
                const std::vector<DepthKey>& input) {
    auto expected = input;
    std::sort(expected.begin(), expected.end(), comesBefore);
    const auto actual = gpuSort(device, queue, pipeline, input);
    assert(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (actual[i].depth != expected[i].depth || actual[i].sourceIndex != expected[i].sourceIndex) {
            std::cerr << "Metal bitonic mismatch at " << i
                      << ": got (" << actual[i].depth << ',' << actual[i].sourceIndex
                      << ") expected (" << expected[i].depth << ',' << expected[i].sourceIndex << ")\n";
        }
        assert(actual[i].depth == expected[i].depth);
        assert(actual[i].sourceIndex == expected[i].sourceIndex);
    }
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
        NSString* source = [NSString stringWithUTF8String:vulkax::viewer::metal::sortShaderSource];
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&libraryError];
        if (!library) {
            std::cerr << "Metal sort shader failed to compile: " << errorText(libraryError) << '\n';
            return 1;
        }
        id<MTLFunction> function = [library newFunctionWithName:@"bitonicSortStep"];
        if (!function) {
            std::cerr << "bitonicSortStep kernel missing\n";
            return 1;
        }
        NSError* pipelineError = nil;
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:function error:&pipelineError];
        if (!pipeline) {
            std::cerr << "Could not create Metal bitonic pipeline: " << errorText(pipelineError) << '\n';
            return 1;
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) {
            std::cerr << "Could not create Metal command queue\n";
            return 1;
        }

        verifyCase(device, queue, pipeline, {{3.0F, 0U}});
        verifyCase(device, queue, pipeline,
                   {{2.0F, 0U}, {8.0F, 1U}, {5.0F, 2U}});
        verifyCase(device, queue, pipeline,
                   {{5.0F, 8U}, {5.0F, 2U}, {5.0F, 5U}, {9.0F, 4U}, {1.0F, 1U}});

        std::vector<DepthKey> medium;
        medium.reserve(257U);
        for (std::uint32_t i = 0; i < 257U; ++i) {
            const float depth = std::sin(static_cast<float>(i) * 0.731F) * 23.0F
                              + std::cos(static_cast<float>(i) * 0.113F) * 7.0F;
            medium.push_back({depth, i});
        }
        verifyCase(device, queue, pipeline, medium);

        std::vector<DepthKey> larger;
        larger.reserve(1025U);
        for (std::uint32_t i = 0; i < 1025U; ++i) {
            const float bucket = static_cast<float>((i * 37U) % 97U);
            larger.push_back({bucket * 0.125F - 4.0F, i});
        }
        verifyCase(device, queue, pipeline, larger);

        return 0;
    }
}
