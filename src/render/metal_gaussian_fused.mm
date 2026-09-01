#include "vulkax/render/gaussian_projection.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace vulkax::render {
namespace {

[[nodiscard]] std::size_t nextPowerOfTwo(std::size_t value) {
    if (value <= 1U) return 1U;
    --value;
    for (std::size_t shift = 1U; shift < sizeof(std::size_t) * 8U; shift <<= 1U)
        value |= value >> shift;
    return value + 1U;
}

struct OrderParameters {
    std::uint32_t projectedCount{};
    std::uint32_t paddedCount{};
    std::uint32_t compareDistance{};
    std::uint32_t sequenceLength{};
};

struct MetadataParameters {
    std::uint32_t projectedCount{};
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::uint32_t reserved{};
};

struct ScheduleParameters {
    std::array<std::uint32_t, 4> counts{};
    std::array<std::uint32_t, 4> capacity{};
};

[[nodiscard]] id<MTLComputePipelineState> makePipeline(
    id<MTLDevice> device,
    id<MTLLibrary> library,
    NSString* name) {
    id<MTLFunction> function = [library newFunctionWithName:name];
    if (!function)
        throw std::runtime_error(std::string("Metal fused Gaussian function is missing: ") + [name UTF8String]);
    NSError* error = nil;
    id<MTLComputePipelineState> pipeline =
        [device newComputePipelineStateWithFunction:function error:&error];
    if (!pipeline)
        throw std::runtime_error(std::string("Metal fused Gaussian pipeline failed: ") +
                                 [[error localizedDescription] UTF8String]);
    return pipeline;
}

void requireCompleted(id<MTLCommandBuffer> commandBuffer, const char* stage) {
    if (commandBuffer.status == MTLCommandBufferStatusError)
        throw std::runtime_error(std::string(stage) + " failed: " +
                                 [[commandBuffer.error localizedDescription] UTF8String]);
}

} // namespace

GaussianFusedProjectionScheduleResult projectScheduleGaussianSplatsMetal(
    const std::vector<GaussianProjectionInput>& inputs,
    const GaussianProjectionParameters& parameters,
    std::uint32_t columns,
    std::uint32_t rows) {
    GaussianFusedProjectionScheduleResult result;
    result.stats.inputSplats = inputs.size();
    result.inputBytes = inputs.size() * sizeof(GaussianProjectionInput);
    result.outputBytes = inputs.size() * sizeof(GaussianProjectedSplat);
    result.intermediateReadbackBytes = 8U * sizeof(std::uint32_t);

    const std::size_t tileCount = static_cast<std::size_t>(columns) * rows;
    if (columns == 0U || rows == 0U || tileCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Metal fused Gaussian tile grid exceeds uint32 capacity");
    if (inputs.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Metal fused Gaussian input exceeds uint32 capacity");
    if (inputs.empty()) {
        result.tileOffsets.assign(tileCount + 1U, 0U);
        result.paddedOrderCount = 1U;
        return result;
    }

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) throw std::runtime_error("no Metal device for fused Gaussian execution");

        NSString* source = @R"metal(
#include <metal_stdlib>
using namespace metal;

struct GaussianInput {
    float4 positionOpacity;
    float4 scale;
    float4 rotation;
    float4 color;
};

struct GaussianOutput {
    float4 centerMajor;
    float4 minorDepth;
    float4 colorCull;
    float4 tileBounds;
};

struct ProjectionParameters {
    float4 cameraPosition;
    float4 right;
    float4 up;
    float4 forward;
    float4 imageFocalNear;
    float4 sigmaTile;
};

struct MetadataParameters {
    uint projectedCount;
    uint columns;
    uint rows;
    uint reserved;
};

struct OrderParameters {
    uint projectedCount;
    uint paddedCount;
    uint compareDistance;
    uint sequenceLength;
};

struct ScheduleParameters {
    uint4 counts;
    uint4 capacity;
};

inline void writeCull(device GaussianOutput& output, float reason) {
    output.centerMajor = float4(0.0f);
    output.minorDepth = float4(0.0f);
    output.colorCull = float4(0.0f, 0.0f, 0.0f, reason);
    output.tileBounds = float4(0.0f);
}

inline float covarianceQuadratic(
    float3 a,
    float c00, float c01, float c02,
    float c11, float c12, float c22) {
    return a.x * (c00 * a.x + c01 * a.y + c02 * a.z) +
           a.y * (c01 * a.x + c11 * a.y + c12 * a.z) +
           a.z * (c02 * a.x + c12 * a.y + c22 * a.z);
}

kernel void gaussian_project(
    device const GaussianInput* inputs [[buffer(0)]],
    device GaussianOutput* outputs [[buffer(1)]],
    constant ProjectionParameters& parameters [[buffer(2)]],
    constant uint& count [[buffer(3)]],
    uint index [[thread_position_in_grid]]) {
    if (index >= count) return;

    GaussianInput inputValue = inputs[index];
    float opacity = inputValue.positionOpacity.w;
    if (!isfinite(opacity) || opacity < (1.0f / 255.0f)) {
        writeCull(outputs[index], 1.0f);
        return;
    }

    float3 delta = inputValue.positionOpacity.xyz - parameters.cameraPosition.xyz;
    float cameraX = dot(delta, parameters.right.xyz);
    float cameraY = dot(delta, parameters.up.xyz);
    float cameraZ = dot(delta, parameters.forward.xyz);
    if (!isfinite(cameraZ) || cameraZ <= parameters.imageFocalNear.w) {
        writeCull(outputs[index], 2.0f);
        return;
    }

    float w = inputValue.rotation.x;
    float x = inputValue.rotation.y;
    float y = inputValue.rotation.z;
    float z = inputValue.rotation.w;
    float r00 = 1.0f - 2.0f * (y * y + z * z);
    float r01 = 2.0f * (x * y - z * w);
    float r02 = 2.0f * (x * z + y * w);
    float r10 = 2.0f * (x * y + z * w);
    float r11 = 1.0f - 2.0f * (x * x + z * z);
    float r12 = 2.0f * (y * z - x * w);
    float r20 = 2.0f * (x * z - y * w);
    float r21 = 2.0f * (y * z + x * w);
    float r22 = 1.0f - 2.0f * (x * x + y * y);

    float3 axis0 = float3(r00, r10, r20);
    float3 axis1 = float3(r01, r11, r21);
    float3 axis2 = float3(r02, r12, r22);
    float3 axisCamera0 = float3(
        dot(parameters.right.xyz, axis0),
        dot(parameters.up.xyz, axis0),
        dot(parameters.forward.xyz, axis0));
    float3 axisCamera1 = float3(
        dot(parameters.right.xyz, axis1),
        dot(parameters.up.xyz, axis1),
        dot(parameters.forward.xyz, axis1));
    float3 axisCamera2 = float3(
        dot(parameters.right.xyz, axis2),
        dot(parameters.up.xyz, axis2),
        dot(parameters.forward.xyz, axis2));

    float3 variance = inputValue.scale.xyz * inputValue.scale.xyz;
    float c00 = variance.x * axisCamera0.x * axisCamera0.x +
                variance.y * axisCamera1.x * axisCamera1.x +
                variance.z * axisCamera2.x * axisCamera2.x;
    float c01 = variance.x * axisCamera0.x * axisCamera0.y +
                variance.y * axisCamera1.x * axisCamera1.y +
                variance.z * axisCamera2.x * axisCamera2.y;
    float c02 = variance.x * axisCamera0.x * axisCamera0.z +
                variance.y * axisCamera1.x * axisCamera1.z +
                variance.z * axisCamera2.x * axisCamera2.z;
    float c11 = variance.x * axisCamera0.y * axisCamera0.y +
                variance.y * axisCamera1.y * axisCamera1.y +
                variance.z * axisCamera2.y * axisCamera2.y;
    float c12 = variance.x * axisCamera0.y * axisCamera0.z +
                variance.y * axisCamera1.y * axisCamera1.z +
                variance.z * axisCamera2.y * axisCamera2.z;
    float c22 = variance.x * axisCamera0.z * axisCamera0.z +
                variance.y * axisCamera1.z * axisCamera1.z +
                variance.z * axisCamera2.z * axisCamera2.z;

    float width = parameters.imageFocalNear.x;
    float height = parameters.imageFocalNear.y;
    float focal = parameters.imageFocalNear.z;
    float centerX = 0.5f * width + focal * cameraX / cameraZ;
    float centerY = 0.5f * height - focal * cameraY / cameraZ;
    float inverseZ = 1.0f / cameraZ;
    float inverseZ2 = inverseZ * inverseZ;
    float3 jacobianX = float3(focal * inverseZ, 0.0f, -focal * cameraX * inverseZ2);
    float3 jacobianY = float3(0.0f, -focal * inverseZ, focal * cameraY * inverseZ2);
    float minimumVariance = parameters.sigmaTile.y * parameters.sigmaTile.y;
    float covarianceXX = covarianceQuadratic(jacobianX, c00, c01, c02, c11, c12, c22) + minimumVariance;
    float covarianceXY =
        jacobianX.x * (c00 * jacobianY.x + c01 * jacobianY.y + c02 * jacobianY.z) +
        jacobianX.y * (c01 * jacobianY.x + c11 * jacobianY.y + c12 * jacobianY.z) +
        jacobianX.z * (c02 * jacobianY.x + c12 * jacobianY.y + c22 * jacobianY.z);
    float covarianceYY = covarianceQuadratic(jacobianY, c00, c01, c02, c11, c12, c22) + minimumVariance;

    float trace = covarianceXX + covarianceYY;
    float deltaCovariance = covarianceXX - covarianceYY;
    float discriminant = sqrt(max(0.0f, deltaCovariance * deltaCovariance +
                                         4.0f * covarianceXY * covarianceXY));
    float lambdaMajor = max(0.5f * (trace + discriminant), minimumVariance);
    float lambdaMinor = max(0.5f * (trace - discriminant), minimumVariance);
    float sigmaMajor = clamp(sqrt(lambdaMajor), parameters.sigmaTile.y, parameters.sigmaTile.z);
    float sigmaMinor = clamp(sqrt(lambdaMinor), parameters.sigmaTile.y, parameters.sigmaTile.z);
    float angle = 0.5f * atan2(2.0f * covarianceXY, covarianceXX - covarianceYY);
    float cosine = cos(angle);
    float sine = sin(angle);
    float2 majorPixels = float2(cosine, sine) * sigmaMajor * parameters.sigmaTile.x;
    float2 minorPixels = float2(-sine, cosine) * sigmaMinor * parameters.sigmaTile.x;
    float extentX = abs(majorPixels.x) + abs(minorPixels.x);
    float extentY = abs(majorPixels.y) + abs(minorPixels.y);
    if (centerX + extentX < 0.0f || centerX - extentX >= width ||
        centerY + extentY < 0.0f || centerY - extentY >= height) {
        writeCull(outputs[index], 3.0f);
        return;
    }

    float2 centerNdc = float2(2.0f * centerX / width - 1.0f,
                              1.0f - 2.0f * centerY / height);
    float2 majorNdc = float2(2.0f * majorPixels.x / width,
                             -2.0f * majorPixels.y / height);
    float2 minorNdc = float2(2.0f * minorPixels.x / width,
                             -2.0f * minorPixels.y / height);
    float pixelMinimumX = clamp(centerX - extentX, 0.0f, width - 1.0f);
    float pixelMaximumX = clamp(centerX + extentX, 0.0f, width - 1.0f);
    float pixelMinimumY = clamp(centerY - extentY, 0.0f, height - 1.0f);
    float pixelMaximumY = clamp(centerY + extentY, 0.0f, height - 1.0f);
    float tileSize = parameters.sigmaTile.w;

    outputs[index].centerMajor = float4(centerNdc, majorNdc);
    outputs[index].minorDepth = float4(minorNdc, cameraZ, opacity);
    outputs[index].colorCull = float4(inputValue.color.rgb, 0.0f);
    outputs[index].tileBounds = float4(
        floor(pixelMinimumX / tileSize),
        floor(pixelMaximumX / tileSize),
        floor(pixelMinimumY / tileSize),
        floor(pixelMaximumY / tileSize));
}

inline bool decodeTile(float encoded, uint limit, thread uint& value) {
    if (!isfinite(encoded)) return false;
    float rounded = round(encoded);
    if (abs(encoded - rounded) > 1.0e-4f || rounded < 0.0f || rounded >= float(limit)) return false;
    value = uint(rounded);
    return true;
}

kernel void gaussian_projection_metadata(
    device const GaussianOutput* projected [[buffer(0)]],
    device uint4* metadata [[buffer(1)]],
    constant MetadataParameters& parameters [[buffer(2)]],
    uint index [[thread_position_in_grid]]) {
    if (index != 0u) return;
    uint visible = 0u;
    uint opacity = 0u;
    uint behind = 0u;
    uint outside = 0u;
    uint references = 0u;
    uint errorCode = 0u;

    for (uint i = 0u; i < parameters.projectedCount; ++i) {
        float encodedReason = projected[i].colorCull.w;
        if (!isfinite(encodedReason)) { errorCode = 1u; break; }
        float roundedReason = round(encodedReason);
        if (abs(encodedReason - roundedReason) > 1.0e-4f || roundedReason < 0.0f || roundedReason > 3.0f) {
            errorCode = 2u; break;
        }
        uint reason = uint(roundedReason);
        if (reason == 1u) { opacity += 1u; continue; }
        if (reason == 2u) { behind += 1u; continue; }
        if (reason == 3u) { outside += 1u; continue; }

        float depth = projected[i].minorDepth.z;
        if (!isfinite(depth) || !(depth > 0.0f)) { errorCode = 3u; break; }
        uint firstColumn = 0u, lastColumn = 0u, firstRow = 0u, lastRow = 0u;
        float4 bounds = projected[i].tileBounds;
        if (!decodeTile(bounds.x, parameters.columns, firstColumn) ||
            !decodeTile(bounds.y, parameters.columns, lastColumn) ||
            !decodeTile(bounds.z, parameters.rows, firstRow) ||
            !decodeTile(bounds.w, parameters.rows, lastRow) ||
            firstColumn > lastColumn || firstRow > lastRow) {
            errorCode = 4u; break;
        }
        ulong contribution = ulong(lastColumn - firstColumn + 1u) *
                             ulong(lastRow - firstRow + 1u);
        if (contribution > 0xfffffffful || ulong(references) + contribution > 0xfffffffful) {
            errorCode = 5u; break;
        }
        references += uint(contribution);
        visible += 1u;
    }
    metadata[0] = uint4(visible, opacity, behind, outside);
    metadata[1] = uint4(references, errorCode, 0u, 0u);
}

inline uint cullReason(device const GaussianOutput* projected, uint index) {
    float encoded = projected[index].colorCull.w;
    if (!isfinite(encoded)) return 4u;
    float rounded = round(encoded);
    if (abs(encoded - rounded) > 1.0e-4f || rounded < 0.0f || rounded > 3.0f) return 4u;
    return uint(rounded);
}

inline bool comesBefore(device const GaussianOutput* projected, uint lhs, uint rhs) {
    const uint sentinel = 0xffffffffu;
    if (lhs == rhs) return false;
    if (lhs == sentinel) return false;
    if (rhs == sentinel) return true;
    uint lhsCull = cullReason(projected, lhs);
    uint rhsCull = cullReason(projected, rhs);
    bool lhsVisible = lhsCull == 0u;
    bool rhsVisible = rhsCull == 0u;
    if (lhsVisible != rhsVisible) return lhsVisible;
    if (!lhsVisible) return lhs < rhs;
    float lhsDepth = projected[lhs].minorDepth.z;
    float rhsDepth = projected[rhs].minorDepth.z;
    if (lhsDepth > rhsDepth) return true;
    if (lhsDepth < rhsDepth) return false;
    return lhs < rhs;
}

kernel void gaussian_order_init(
    device uint* orderIndices [[buffer(0)]],
    constant OrderParameters& parameters [[buffer(1)]],
    uint index [[thread_position_in_grid]]) {
    if (index >= parameters.paddedCount) return;
    orderIndices[index] = index < parameters.projectedCount ? index : 0xffffffffu;
}

kernel void gaussian_depth_order(
    device const GaussianOutput* projected [[buffer(0)]],
    device uint* orderIndices [[buffer(1)]],
    constant OrderParameters& parameters [[buffer(2)]],
    uint index [[thread_position_in_grid]]) {
    if (index >= parameters.paddedCount) return;
    uint partner = index ^ parameters.compareDistance;
    if (partner <= index || partner >= parameters.paddedCount) return;
    uint lhs = orderIndices[index];
    uint rhs = orderIndices[partner];
    bool ascendingDesiredOrder = (index & parameters.sequenceLength) == 0u;
    bool shouldSwap = ascendingDesiredOrder ? comesBefore(projected, rhs, lhs)
                                            : comesBefore(projected, lhs, rhs);
    if (shouldSwap) {
        orderIndices[index] = rhs;
        orderIndices[partner] = lhs;
    }
}

kernel void gaussian_fused_schedule_csr(
    device const GaussianOutput* projected [[buffer(0)]],
    device const uint* orderIndices [[buffer(1)]],
    device uint* offsets [[buffer(2)]],
    device uint* cursors [[buffer(3)]],
    device uint* references [[buffer(4)]],
    device uint4* metadata [[buffer(5)]],
    constant ScheduleParameters& parameters [[buffer(6)]],
    uint index [[thread_position_in_grid]]) {
    if (index != 0u) return;
    uint rawCount = parameters.counts.x;
    uint visibleCount = parameters.counts.y;
    uint columns = parameters.counts.z;
    uint rows = parameters.counts.w;
    uint referenceCapacity = parameters.capacity.x;
    uint tileCount = columns * rows;

    metadata[0] = uint4(0u);
    for (uint tile = 0u; tile <= tileCount; ++tile) offsets[tile] = 0u;
    for (uint tile = 0u; tile < tileCount; ++tile) cursors[tile] = 0u;

    uint total = 0u;
    for (uint rank = 0u; rank < visibleCount; ++rank) {
        uint rawIndex = orderIndices[rank];
        if (rawIndex >= rawCount) { metadata[0].z = 1u; return; }
        if (abs(projected[rawIndex].colorCull.w) > 1.0e-4f) { metadata[0].z = 2u; return; }
        uint firstColumn = 0u, lastColumn = 0u, firstRow = 0u, lastRow = 0u;
        float4 bounds = projected[rawIndex].tileBounds;
        if (!decodeTile(bounds.x, columns, firstColumn) ||
            !decodeTile(bounds.y, columns, lastColumn) ||
            !decodeTile(bounds.z, rows, firstRow) ||
            !decodeTile(bounds.w, rows, lastRow) ||
            firstColumn > lastColumn || firstRow > lastRow) {
            metadata[0].z = 3u; return;
        }
        for (uint row = firstRow; row <= lastRow; ++row) {
            for (uint column = firstColumn; column <= lastColumn; ++column) {
                if (total >= referenceCapacity) { metadata[0].z = 4u; return; }
                offsets[row * columns + column + 1u] += 1u;
                total += 1u;
            }
        }
    }

    uint running = 0u;
    uint maximum = 0u;
    for (uint tile = 0u; tile < tileCount; ++tile) {
        uint count = offsets[tile + 1u];
        maximum = max(maximum, count);
        offsets[tile] = running;
        cursors[tile] = running;
        running += count;
    }
    offsets[tileCount] = running;
    if (running != total) { metadata[0].z = 5u; return; }

    for (uint rank = 0u; rank < visibleCount; ++rank) {
        uint rawIndex = orderIndices[rank];
        float4 bounds = projected[rawIndex].tileBounds;
        uint firstColumn = uint(round(bounds.x));
        uint lastColumn = uint(round(bounds.y));
        uint firstRow = uint(round(bounds.z));
        uint lastRow = uint(round(bounds.w));
        for (uint row = firstRow; row <= lastRow; ++row) {
            for (uint column = firstColumn; column <= lastColumn; ++column) {
                uint tile = row * columns + column;
                uint slot = cursors[tile]++;
                if (slot >= referenceCapacity) { metadata[0].z = 6u; return; }
                references[slot] = rank;
            }
        }
    }
    if (visibleCount < rawCount) {
        uint firstCulled = orderIndices[visibleCount];
        if (firstCulled < rawCount && abs(projected[firstCulled].colorCull.w) <= 1.0e-4f) {
            metadata[0].z = 7u; return;
        }
    }
    metadata[0] = uint4(total, maximum, 0u, visibleCount);
}
)metal";

        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library)
            throw std::runtime_error(std::string("Metal fused Gaussian shader compile failed: ") +
                                     [[error localizedDescription] UTF8String]);

        id<MTLComputePipelineState> projectionPipeline = makePipeline(device, library, @"gaussian_project");
        id<MTLComputePipelineState> metadataPipeline = makePipeline(device, library, @"gaussian_projection_metadata");
        id<MTLComputePipelineState> initPipeline = makePipeline(device, library, @"gaussian_order_init");
        id<MTLComputePipelineState> orderPipeline = makePipeline(device, library, @"gaussian_depth_order");
        id<MTLComputePipelineState> schedulePipeline = makePipeline(device, library, @"gaussian_fused_schedule_csr");

        const NSUInteger inputBytes = inputs.size() * sizeof(GaussianProjectionInput);
        const NSUInteger projectedBytes = inputs.size() * sizeof(GaussianProjectedSplat);
        id<MTLBuffer> inputBuffer = [device newBufferWithBytes:inputs.data()
                                                    length:inputBytes
                                                   options:MTLResourceStorageModeShared];
        id<MTLBuffer> projectedBuffer = [device newBufferWithLength:projectedBytes
                                                            options:MTLResourceStorageModeShared];
        id<MTLBuffer> projectionMetadataBuffer =
            [device newBufferWithLength:8U * sizeof(std::uint32_t)
                                options:MTLResourceStorageModeShared];
        if (!inputBuffer || !projectedBuffer || !projectionMetadataBuffer)
            throw std::runtime_error("Metal fused Gaussian projection buffer allocation failed");
        std::memset([projectedBuffer contents], 0, projectedBytes);
        std::memset([projectionMetadataBuffer contents], 0, 8U * sizeof(std::uint32_t));

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) throw std::runtime_error("Metal fused Gaussian command queue failed");

        id<MTLCommandBuffer> projectionCommands = [queue commandBuffer];
        {
            id<MTLComputeCommandEncoder> encoder = [projectionCommands computeCommandEncoder];
            [encoder setComputePipelineState:projectionPipeline];
            [encoder setBuffer:inputBuffer offset:0 atIndex:0];
            [encoder setBuffer:projectedBuffer offset:0 atIndex:1];
            [encoder setBytes:&parameters length:sizeof(parameters) atIndex:2];
            const std::uint32_t count = static_cast<std::uint32_t>(inputs.size());
            [encoder setBytes:&count length:sizeof(count) atIndex:3];
            const NSUInteger width = std::min<NSUInteger>(64U, projectionPipeline.maxTotalThreadsPerThreadgroup);
            [encoder dispatchThreads:MTLSizeMake(inputs.size(), 1U, 1U)
                threadsPerThreadgroup:MTLSizeMake(width, 1U, 1U)];
            [encoder endEncoding];
        }
        {
            id<MTLComputeCommandEncoder> encoder = [projectionCommands computeCommandEncoder];
            [encoder setComputePipelineState:metadataPipeline];
            [encoder setBuffer:projectedBuffer offset:0 atIndex:0];
            [encoder setBuffer:projectionMetadataBuffer offset:0 atIndex:1];
            const MetadataParameters metadataParameters{
                static_cast<std::uint32_t>(inputs.size()), columns, rows, 0U};
            [encoder setBytes:&metadataParameters length:sizeof(metadataParameters) atIndex:2];
            [encoder dispatchThreads:MTLSizeMake(1U, 1U, 1U)
                threadsPerThreadgroup:MTLSizeMake(1U, 1U, 1U)];
            [encoder endEncoding];
        }

        const auto projectionStart = std::chrono::steady_clock::now();
        [projectionCommands commit];
        [projectionCommands waitUntilCompleted];
        const auto projectionStop = std::chrono::steady_clock::now();
        requireCompleted(projectionCommands, "Metal fused Gaussian projection/metadata");
        result.projectionMilliseconds =
            std::chrono::duration<double, std::milli>(projectionStop - projectionStart).count();

        const auto* projectionMetadata =
            static_cast<const std::uint32_t*>([projectionMetadataBuffer contents]);
        const std::uint32_t visibleCount = projectionMetadata[0];
        result.stats.visibleSplats = visibleCount;
        result.stats.culledOpacity = projectionMetadata[1];
        result.stats.culledBehindCamera = projectionMetadata[2];
        result.stats.culledOutsideImage = projectionMetadata[3];
        const std::uint32_t referenceCount = projectionMetadata[4];
        const std::uint32_t metadataError = projectionMetadata[5];
        if (metadataError != 0U)
            throw std::runtime_error("Metal fused Gaussian projection metadata validation failed with code " +
                                     std::to_string(metadataError));
        const std::size_t classified = static_cast<std::size_t>(visibleCount) +
            result.stats.culledOpacity + result.stats.culledBehindCamera + result.stats.culledOutsideImage;
        if (classified != inputs.size())
            throw std::runtime_error("Metal fused Gaussian projection metadata count is inconsistent");

        const std::size_t paddedCount = nextPowerOfTwo(inputs.size());
        if (paddedCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            throw std::invalid_argument("Metal fused Gaussian padded order exceeds uint32 capacity");
        result.paddedOrderCount = paddedCount;
        result.splatReferences = referenceCount;

        const NSUInteger orderBytes = paddedCount * sizeof(std::uint32_t);
        const NSUInteger offsetsBytes = (tileCount + 1U) * sizeof(std::uint32_t);
        const NSUInteger cursorsBytes = std::max<std::size_t>(tileCount, 1U) * sizeof(std::uint32_t);
        const NSUInteger referencesBytes = std::max<std::size_t>(referenceCount, 1U) * sizeof(std::uint32_t);
        const NSUInteger scheduleMetadataBytes = 4U * sizeof(std::uint32_t);
        id<MTLBuffer> orderBuffer = [device newBufferWithLength:orderBytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> offsetsBuffer = [device newBufferWithLength:offsetsBytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> cursorsBuffer = [device newBufferWithLength:cursorsBytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> referencesBuffer = [device newBufferWithLength:referencesBytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> scheduleMetadataBuffer =
            [device newBufferWithLength:scheduleMetadataBytes options:MTLResourceStorageModeShared];
        if (!orderBuffer || !offsetsBuffer || !cursorsBuffer || !referencesBuffer || !scheduleMetadataBuffer)
            throw std::runtime_error("Metal fused Gaussian scheduler buffer allocation failed");
        std::memset([orderBuffer contents], 0xff, orderBytes);
        std::memset([offsetsBuffer contents], 0, offsetsBytes);
        std::memset([cursorsBuffer contents], 0, cursorsBytes);
        std::memset([referencesBuffer contents], 0, referencesBytes);
        std::memset([scheduleMetadataBuffer contents], 0, scheduleMetadataBytes);

        id<MTLCommandBuffer> scheduleCommands = [queue commandBuffer];
        {
            id<MTLComputeCommandEncoder> encoder = [scheduleCommands computeCommandEncoder];
            [encoder setComputePipelineState:initPipeline];
            [encoder setBuffer:orderBuffer offset:0 atIndex:0];
            const OrderParameters initParameters{
                static_cast<std::uint32_t>(inputs.size()),
                static_cast<std::uint32_t>(paddedCount), 0U, 0U};
            [encoder setBytes:&initParameters length:sizeof(initParameters) atIndex:1];
            const NSUInteger width = std::min<NSUInteger>(64U, initPipeline.maxTotalThreadsPerThreadgroup);
            [encoder dispatchThreads:MTLSizeMake(paddedCount, 1U, 1U)
                threadsPerThreadgroup:MTLSizeMake(width, 1U, 1U)];
            [encoder endEncoding];
        }

        for (std::uint32_t sequenceLength = 2U;
             sequenceLength <= static_cast<std::uint32_t>(paddedCount);
             sequenceLength <<= 1U) {
            for (std::uint32_t compareDistance = sequenceLength >> 1U;
                 compareDistance > 0U;
                 compareDistance >>= 1U) {
                id<MTLComputeCommandEncoder> encoder = [scheduleCommands computeCommandEncoder];
                [encoder setComputePipelineState:orderPipeline];
                [encoder setBuffer:projectedBuffer offset:0 atIndex:0];
                [encoder setBuffer:orderBuffer offset:0 atIndex:1];
                const OrderParameters orderParameters{
                    static_cast<std::uint32_t>(inputs.size()),
                    static_cast<std::uint32_t>(paddedCount),
                    compareDistance,
                    sequenceLength};
                [encoder setBytes:&orderParameters length:sizeof(orderParameters) atIndex:2];
                const NSUInteger width = std::min<NSUInteger>(64U, orderPipeline.maxTotalThreadsPerThreadgroup);
                [encoder dispatchThreads:MTLSizeMake(paddedCount, 1U, 1U)
                    threadsPerThreadgroup:MTLSizeMake(width, 1U, 1U)];
                [encoder endEncoding];
            }
            if (sequenceLength > static_cast<std::uint32_t>(paddedCount) / 2U) break;
        }

        {
            id<MTLComputeCommandEncoder> encoder = [scheduleCommands computeCommandEncoder];
            [encoder setComputePipelineState:schedulePipeline];
            [encoder setBuffer:projectedBuffer offset:0 atIndex:0];
            [encoder setBuffer:orderBuffer offset:0 atIndex:1];
            [encoder setBuffer:offsetsBuffer offset:0 atIndex:2];
            [encoder setBuffer:cursorsBuffer offset:0 atIndex:3];
            [encoder setBuffer:referencesBuffer offset:0 atIndex:4];
            [encoder setBuffer:scheduleMetadataBuffer offset:0 atIndex:5];
            ScheduleParameters scheduleParameters;
            scheduleParameters.counts = {
                static_cast<std::uint32_t>(inputs.size()), visibleCount, columns, rows};
            scheduleParameters.capacity = {referenceCount, 0U, 0U, 0U};
            [encoder setBytes:&scheduleParameters length:sizeof(scheduleParameters) atIndex:6];
            [encoder dispatchThreads:MTLSizeMake(1U, 1U, 1U)
                threadsPerThreadgroup:MTLSizeMake(1U, 1U, 1U)];
            [encoder endEncoding];
        }

        const auto scheduleStart = std::chrono::steady_clock::now();
        [scheduleCommands commit];
        [scheduleCommands waitUntilCompleted];
        const auto scheduleStop = std::chrono::steady_clock::now();
        requireCompleted(scheduleCommands, "Metal fused Gaussian ordering/CSR");
        result.schedulingMilliseconds =
            std::chrono::duration<double, std::milli>(scheduleStop - scheduleStart).count();

        const auto* scheduleMetadata =
            static_cast<const std::uint32_t*>([scheduleMetadataBuffer contents]);
        if (scheduleMetadata[2] != 0U)
            throw std::runtime_error("Metal fused Gaussian CSR validation failed with code " +
                                     std::to_string(scheduleMetadata[2]));
        if (scheduleMetadata[0] != referenceCount || scheduleMetadata[3] != visibleCount)
            throw std::runtime_error("Metal fused Gaussian CSR metadata is inconsistent");
        result.maximumSplatsPerTile = scheduleMetadata[1];

        const auto* rawProjected =
            static_cast<const GaussianProjectedSplat*>([projectedBuffer contents]);
        const auto* order = static_cast<const std::uint32_t*>([orderBuffer contents]);
        result.projected.reserve(visibleCount);
        for (std::uint32_t rank = 0U; rank < visibleCount; ++rank) {
            const std::uint32_t rawIndex = order[rank];
            if (rawIndex >= inputs.size())
                throw std::runtime_error("Metal fused Gaussian order references invalid raw record");
            if (std::abs(rawProjected[rawIndex].colorCull[3]) > 1.0e-4F)
                throw std::runtime_error("Metal fused Gaussian visible prefix contains a culled record");
            result.projected.push_back(rawProjected[rawIndex]);
        }

        result.tileOffsets.resize(tileCount + 1U);
        std::memcpy(result.tileOffsets.data(), [offsetsBuffer contents], offsetsBytes);
        result.projectedSplatIndices.resize(referenceCount);
        if (referenceCount > 0U)
            std::memcpy(result.projectedSplatIndices.data(), [referencesBuffer contents],
                        referenceCount * sizeof(std::uint32_t));

        result.schedulerOutputBytes =
            orderBytes + offsetsBytes + static_cast<std::size_t>(referenceCount) * sizeof(std::uint32_t) +
            scheduleMetadataBytes;
        result.schedulerWorkspaceBytes = orderBytes + cursorsBytes;
        return result;
    }
}

} // namespace vulkax::render
