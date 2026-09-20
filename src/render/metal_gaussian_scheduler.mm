#include "vulkax/render/gaussian_scheduler.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace vulkax::render {
namespace {

[[nodiscard]] std::size_t paddedOrderCount(std::size_t count) {
    std::size_t padded = 1U;
    while (padded < count) {
        if (padded > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) / 2U)
            throw std::invalid_argument("Metal Gaussian depth order exceeds uint32 power-of-two capacity");
        padded *= 2U;
    }
    return padded;
}

} // namespace

GaussianNativeScheduleResult scheduleGaussianProjectionMetal(
    const GaussianNativeProjectionResult& projection) {
    const std::size_t tileCount =
        static_cast<std::size_t>(projection.tileColumns) * projection.tileRows;
    if (projection.projected.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        projection.splatReferences > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        tileCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Metal Gaussian scheduler exceeds uint32 capacity");
    const std::size_t paddedCount = paddedOrderCount(projection.projected.size());

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) throw std::runtime_error("no Metal device for Gaussian scheduler");

        NSString* source = @R"metal(
#include <metal_stdlib>
using namespace metal;

struct GaussianOutput {
    float4 centerMajor;
    float4 minorDepth;
    float4 colorCull;
    float4 tileBounds;
};

inline bool comes_before(
    uint lhs,
    uint rhs,
    device const GaussianOutput* projected) {
    const uint sentinel = 0xffffffffu;
    if (lhs == rhs) return false;
    if (lhs == sentinel) return false;
    if (rhs == sentinel) return true;
    const float lhsDepth = projected[lhs].minorDepth.z;
    const float rhsDepth = projected[rhs].minorDepth.z;
    if (lhsDepth > rhsDepth) return true;
    if (lhsDepth < rhsDepth) return false;
    return lhs < rhs;
}

kernel void gaussian_depth_order(
    device const GaussianOutput* projected [[buffer(0)]],
    device uint* orderIndices [[buffer(1)]],
    constant uint4& parameters [[buffer(2)]],
    uint index [[thread_position_in_grid]]) {
    const uint paddedCount = parameters.y;
    if (index >= paddedCount) return;
    const uint partner = index ^ parameters.z;
    if (partner <= index || partner >= paddedCount) return;

    const uint lhs = orderIndices[index];
    const uint rhs = orderIndices[partner];
    const bool ascendingDesiredOrder = (index & parameters.w) == 0u;
    const bool swap = ascendingDesiredOrder
        ? comes_before(rhs, lhs, projected)
        : comes_before(lhs, rhs, projected);
    if (swap) {
        orderIndices[index] = rhs;
        orderIndices[partner] = lhs;
    }
}

kernel void gaussian_schedule_csr(
    device const GaussianOutput* projected [[buffer(0)]],
    device const uint* orderIndices [[buffer(1)]],
    device uint* offsets [[buffer(2)]],
    device uint* cursors [[buffer(3)]],
    device uint* references [[buffer(4)]],
    device uint4* metadata [[buffer(5)]],
    constant uint4& parameters [[buffer(6)]],
    uint threadIndex [[thread_position_in_grid]]) {
    if (threadIndex != 0u) return;

    const uint projectedCount = parameters.x;
    const uint columns = parameters.y;
    const uint rows = parameters.z;
    const uint capacity = parameters.w;
    const uint tileCount = columns * rows;
    metadata[0] = uint4(0u);

    for (uint tile = 0u; tile <= tileCount; ++tile) offsets[tile] = 0u;
    for (uint tile = 0u; tile < tileCount; ++tile) cursors[tile] = 0u;

    uint total = 0u;
    for (uint rank = 0u; rank < projectedCount; ++rank) {
        const uint splat = orderIndices[rank];
        if (splat >= projectedCount) {
            metadata[0].z = 5u;
            return;
        }
        const float4 bounds = projected[splat].tileBounds;
        const uint firstColumn = uint(bounds.x + 0.5f);
        const uint lastColumn = uint(bounds.y + 0.5f);
        const uint firstRow = uint(bounds.z + 0.5f);
        const uint lastRow = uint(bounds.w + 0.5f);
        if (firstColumn > lastColumn || firstRow > lastRow ||
            lastColumn >= columns || lastRow >= rows) {
            metadata[0].z = 1u;
            return;
        }
        for (uint row = firstRow; row <= lastRow; ++row) {
            for (uint column = firstColumn; column <= lastColumn; ++column) {
                if (total >= capacity) {
                    metadata[0].z = 2u;
                    return;
                }
                const uint tile = row * columns + column;
                offsets[tile + 1u] += 1u;
                total += 1u;
            }
        }
    }

    uint running = 0u;
    uint maximum = 0u;
    for (uint tile = 0u; tile < tileCount; ++tile) {
        const uint count = offsets[tile + 1u];
        maximum = max(maximum, count);
        offsets[tile] = running;
        cursors[tile] = running;
        running += count;
    }
    offsets[tileCount] = running;
    if (running != total) {
        metadata[0].z = 3u;
        return;
    }

    for (uint rank = 0u; rank < projectedCount; ++rank) {
        const uint splat = orderIndices[rank];
        const float4 bounds = projected[splat].tileBounds;
        const uint firstColumn = uint(bounds.x + 0.5f);
        const uint lastColumn = uint(bounds.y + 0.5f);
        const uint firstRow = uint(bounds.z + 0.5f);
        const uint lastRow = uint(bounds.w + 0.5f);
        for (uint row = firstRow; row <= lastRow; ++row) {
            for (uint column = firstColumn; column <= lastColumn; ++column) {
                const uint tile = row * columns + column;
                const uint slot = cursors[tile]++;
                if (slot >= capacity) {
                    metadata[0].z = 4u;
                    return;
                }
                references[slot] = splat;
            }
        }
    }

    metadata[0] = uint4(total, maximum, 0u, 0u);
}
)metal";

        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library)
            throw std::runtime_error(std::string("Metal Gaussian scheduler shader compile failed: ") +
                                     [[error localizedDescription] UTF8String]);
        id<MTLFunction> orderFunction = [library newFunctionWithName:@"gaussian_depth_order"];
        id<MTLFunction> scheduleFunction = [library newFunctionWithName:@"gaussian_schedule_csr"];
        if (!orderFunction || !scheduleFunction)
            throw std::runtime_error("Metal Gaussian scheduler functions are missing");
        id<MTLComputePipelineState> orderPipeline =
            [device newComputePipelineStateWithFunction:orderFunction error:&error];
        if (!orderPipeline)
            throw std::runtime_error(std::string("Metal Gaussian order pipeline failed: ") +
                                     [[error localizedDescription] UTF8String]);
        id<MTLComputePipelineState> schedulePipeline =
            [device newComputePipelineStateWithFunction:scheduleFunction error:&error];
        if (!schedulePipeline)
            throw std::runtime_error(std::string("Metal Gaussian CSR pipeline failed: ") +
                                     [[error localizedDescription] UTF8String]);

        const NSUInteger projectedBytes = std::max<NSUInteger>(
            projection.projected.size() * sizeof(GaussianProjectedSplat), 4U);
        const NSUInteger orderBytes = paddedCount * sizeof(std::uint32_t);
        const NSUInteger offsetsBytes = (tileCount + 1U) * sizeof(std::uint32_t);
        const NSUInteger cursorsBytes = std::max<std::size_t>(tileCount, 1U) * sizeof(std::uint32_t);
        const NSUInteger referencesBytes = std::max<std::size_t>(projection.splatReferences, 1U) *
                                           sizeof(std::uint32_t);

        id<MTLBuffer> projectedBuffer = [device newBufferWithLength:projectedBytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> orderBuffer = [device newBufferWithLength:orderBytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> offsetsBuffer = [device newBufferWithLength:offsetsBytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> cursorsBuffer = [device newBufferWithLength:cursorsBytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> referencesBuffer = [device newBufferWithLength:referencesBytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> metadataBuffer = [device newBufferWithLength:4U * sizeof(std::uint32_t) options:MTLResourceStorageModeShared];
        if (!projectedBuffer || !orderBuffer || !offsetsBuffer || !cursorsBuffer ||
            !referencesBuffer || !metadataBuffer)
            throw std::runtime_error("Metal Gaussian scheduler buffer allocation failed");

        if (!projection.projected.empty()) {
            std::memcpy([projectedBuffer contents], projection.projected.data(),
                        projection.projected.size() * sizeof(GaussianProjectedSplat));
        }
        auto* hostOrder = static_cast<std::uint32_t*>([orderBuffer contents]);
        for (std::size_t index = 0U; index < paddedCount; ++index) {
            hostOrder[index] = index < projection.projected.size()
                ? static_cast<std::uint32_t>(index)
                : std::numeric_limits<std::uint32_t>::max();
        }
        std::memset([offsetsBuffer contents], 0, offsetsBytes);
        std::memset([cursorsBuffer contents], 0, cursorsBytes);
        std::memset([referencesBuffer contents], 0, referencesBytes);
        std::memset([metadataBuffer contents], 0, 4U * sizeof(std::uint32_t));

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) throw std::runtime_error("Metal Gaussian scheduler command queue failed");
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];

        [encoder setComputePipelineState:orderPipeline];
        [encoder setBuffer:projectedBuffer offset:0 atIndex:0];
        [encoder setBuffer:orderBuffer offset:0 atIndex:1];
        const NSUInteger orderThreads = std::max<NSUInteger>(
            1U, std::min<NSUInteger>(64U, orderPipeline.maxTotalThreadsPerThreadgroup));
        for (std::uint32_t sequenceLength = 2U;
             sequenceLength <= static_cast<std::uint32_t>(paddedCount);
             sequenceLength <<= 1U) {
            for (std::uint32_t compareDistance = sequenceLength >> 1U;
                 compareDistance > 0U;
                 compareDistance >>= 1U) {
                const std::array<std::uint32_t, 4> parameters{
                    static_cast<std::uint32_t>(projection.projected.size()),
                    static_cast<std::uint32_t>(paddedCount),
                    compareDistance,
                    sequenceLength,
                };
                [encoder setBytes:parameters.data() length:sizeof(parameters) atIndex:2];
                [encoder dispatchThreads:MTLSizeMake(paddedCount, 1U, 1U)
                    threadsPerThreadgroup:MTLSizeMake(orderThreads, 1U, 1U)];
                [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
            }
            if (sequenceLength > static_cast<std::uint32_t>(paddedCount) / 2U) break;
        }

        [encoder setComputePipelineState:schedulePipeline];
        [encoder setBuffer:projectedBuffer offset:0 atIndex:0];
        [encoder setBuffer:orderBuffer offset:0 atIndex:1];
        [encoder setBuffer:offsetsBuffer offset:0 atIndex:2];
        [encoder setBuffer:cursorsBuffer offset:0 atIndex:3];
        [encoder setBuffer:referencesBuffer offset:0 atIndex:4];
        [encoder setBuffer:metadataBuffer offset:0 atIndex:5];
        const std::array<std::uint32_t, 4> scheduleParameters{
            static_cast<std::uint32_t>(projection.projected.size()),
            projection.tileColumns,
            projection.tileRows,
            static_cast<std::uint32_t>(projection.splatReferences),
        };
        [encoder setBytes:scheduleParameters.data() length:sizeof(scheduleParameters) atIndex:6];
        [encoder dispatchThreads:MTLSizeMake(1U, 1U, 1U)
            threadsPerThreadgroup:MTLSizeMake(1U, 1U, 1U)];
        [encoder endEncoding];

        const auto start = std::chrono::steady_clock::now();
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        const auto stop = std::chrono::steady_clock::now();
        if (commandBuffer.status == MTLCommandBufferStatusError)
            throw std::runtime_error(std::string("Metal Gaussian scheduler dispatch failed: ") +
                                     [[commandBuffer.error localizedDescription] UTF8String]);

        const auto* metadata = static_cast<const std::uint32_t*>([metadataBuffer contents]);
        if (metadata[2] != 0U)
            throw std::runtime_error("Metal Gaussian scheduler kernel reported status " +
                                     std::to_string(metadata[2]));
        if (metadata[0] > projection.splatReferences)
            throw std::runtime_error("Metal Gaussian scheduler returned too many references");

        GaussianNativeScheduleResult result;
        result.paddedOrderCount = paddedCount;
        result.schedulingMilliseconds =
            std::chrono::duration<double, std::milli>(stop - start).count();
        result.inputBytes = projection.projected.size() * sizeof(GaussianProjectedSplat);
        result.outputBytes = projection.projected.size() * sizeof(std::uint32_t) +
                             (tileCount + 1U) * sizeof(std::uint32_t) +
                             metadata[0] * sizeof(std::uint32_t) +
                             4U * sizeof(std::uint32_t);
        result.workspaceBytes =
            (paddedCount - projection.projected.size()) * sizeof(std::uint32_t) +
            std::max<std::size_t>(tileCount, 1U) * sizeof(std::uint32_t);
        result.depthOrder.resize(projection.projected.size());
        if (!result.depthOrder.empty())
            std::memcpy(result.depthOrder.data(), [orderBuffer contents],
                        result.depthOrder.size() * sizeof(std::uint32_t));

        result.schedule.tileSize = projection.tileSize;
        result.schedule.columns = projection.tileColumns;
        result.schedule.rows = projection.tileRows;
        result.schedule.splatReferences = metadata[0];
        result.schedule.maximumSplatsPerTile = metadata[1];
        result.schedule.tileOffsets.resize(tileCount + 1U);
        std::memcpy(result.schedule.tileOffsets.data(), [offsetsBuffer contents], offsetsBytes);
        result.schedule.projectedSplatIndices.resize(result.schedule.splatReferences);
        if (result.schedule.splatReferences > 0U) {
            std::memcpy(result.schedule.projectedSplatIndices.data(), [referencesBuffer contents],
                        result.schedule.splatReferences * sizeof(std::uint32_t));
        }
        return result;
    }
}

} // namespace vulkax::render
