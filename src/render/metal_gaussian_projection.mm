#include "vulkax/render/gaussian_projection.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vulkax::render {

std::pair<std::vector<GaussianProjectedSplat>, double> projectGaussianSplatsMetal(
    const std::vector<GaussianProjectionInput>& inputs,
    const GaussianProjectionParameters& parameters) {
    if (inputs.empty()) return {{}, 0.0};
    if (inputs.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Gaussian projection input exceeds Metal uint32 range");

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) throw std::runtime_error("no Metal device for Gaussian projection");

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
)metal";

        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library)
            throw std::runtime_error(std::string("Metal Gaussian projection shader compile failed: ") +
                                     [[error localizedDescription] UTF8String]);
        id<MTLFunction> function = [library newFunctionWithName:@"gaussian_project"];
        if (!function) throw std::runtime_error("Metal Gaussian projection function is missing");
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
        if (!pipeline)
            throw std::runtime_error(std::string("Metal Gaussian projection pipeline failed: ") +
                                     [[error localizedDescription] UTF8String]);

        const NSUInteger inputBytes = inputs.size() * sizeof(GaussianProjectionInput);
        const NSUInteger outputBytes = inputs.size() * sizeof(GaussianProjectedSplat);
        id<MTLBuffer> inputBuffer = [device newBufferWithBytes:inputs.data()
                                                    length:inputBytes
                                                   options:MTLResourceStorageModeShared];
        id<MTLBuffer> outputBuffer = [device newBufferWithLength:outputBytes
                                                         options:MTLResourceStorageModeShared];
        if (!inputBuffer || !outputBuffer)
            throw std::runtime_error("Metal Gaussian projection buffer allocation failed");
        std::memset([outputBuffer contents], 0, outputBytes);

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) throw std::runtime_error("Metal Gaussian projection command queue failed");
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:inputBuffer offset:0 atIndex:0];
        [encoder setBuffer:outputBuffer offset:0 atIndex:1];
        [encoder setBytes:&parameters length:sizeof(parameters) atIndex:2];
        const std::uint32_t count = static_cast<std::uint32_t>(inputs.size());
        [encoder setBytes:&count length:sizeof(count) atIndex:3];
        const NSUInteger threadWidth = std::min<NSUInteger>(64U, pipeline.maxTotalThreadsPerThreadgroup);
        [encoder dispatchThreads:MTLSizeMake(inputs.size(), 1U, 1U)
            threadsPerThreadgroup:MTLSizeMake(threadWidth, 1U, 1U)];
        [encoder endEncoding];

        const auto start = std::chrono::steady_clock::now();
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        const auto stop = std::chrono::steady_clock::now();
        if (commandBuffer.status == MTLCommandBufferStatusError)
            throw std::runtime_error(std::string("Metal Gaussian projection dispatch failed: ") +
                                     [[commandBuffer.error localizedDescription] UTF8String]);
        const double milliseconds =
            std::chrono::duration<double, std::milli>(stop - start).count();

        std::vector<GaussianProjectedSplat> outputs(inputs.size());
        std::memcpy(outputs.data(), [outputBuffer contents], outputBytes);
        return {std::move(outputs), milliseconds};
    }
}

} // namespace vulkax::render
