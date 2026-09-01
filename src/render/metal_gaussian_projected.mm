#include "vulkax/render/gaussian_projection.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace vulkax::render {

ImageRGBA8 renderGaussianProjectedMetal(
    const std::vector<GaussianProjectedSplat>& projected,
    const GaussianRenderSettings& settings) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) throw std::runtime_error("no Metal device for projected Gaussian raster");

        NSString* source = @R"metal(
#include <metal_stdlib>
using namespace metal;

struct GaussianOutput {
    float4 centerMajor;
    float4 minorDepth;
    float4 colorCull;
    float4 tileBounds;
};

struct GaussianVarying {
    float4 position [[position]];
    float2 local;
    float4 colorOpacity;
};

constant float2 corners[6] = {
    float2(-1.0f, -1.0f), float2(1.0f, -1.0f), float2(1.0f, 1.0f),
    float2(-1.0f, -1.0f), float2(1.0f, 1.0f), float2(-1.0f, 1.0f)
};

vertex GaussianVarying gaussian_projected_vertex(
    uint vertexId [[vertex_id]],
    device const GaussianOutput* projected [[buffer(0)]],
    constant float& sigmaCutoff [[buffer(1)]]) {
    const uint splatIndex = vertexId / 6u;
    const uint cornerIndex = vertexId % 6u;
    const GaussianOutput splat = projected[splatIndex];
    const float2 local = corners[cornerIndex];
    const float2 clip = splat.centerMajor.xy +
                        local.x * splat.centerMajor.zw +
                        local.y * splat.minorDepth.xy;
    GaussianVarying out;
    // Metal's render-target convention already matches the legacy Metal path;
    // unlike Vulkan, no explicit Y inversion is applied here.
    out.position = float4(clip, 0.0f, 1.0f);
    out.local = local * sigmaCutoff;
    out.colorOpacity = float4(splat.colorCull.rgb,
                              clamp(splat.minorDepth.w, 0.0f, 0.999f));
    return out;
}

fragment float4 gaussian_projected_fragment(GaussianVarying in [[stage_in]]) {
    float alpha = in.colorOpacity.a * exp(-0.5f * dot(in.local, in.local));
    if (alpha < (1.0f / 255.0f)) discard_fragment();
    alpha = min(alpha, 0.999f);
    return float4(in.colorOpacity.rgb, alpha);
}
)metal";

        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library)
            throw std::runtime_error(std::string("Metal projected Gaussian shader compile failed: ") +
                                     [[error localizedDescription] UTF8String]);
        id<MTLFunction> vertexFunction =
            [library newFunctionWithName:@"gaussian_projected_vertex"];
        id<MTLFunction> fragmentFunction =
            [library newFunctionWithName:@"gaussian_projected_fragment"];
        if (!vertexFunction || !fragmentFunction)
            throw std::runtime_error("Metal projected Gaussian shader functions are missing");

        MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDescriptor.vertexFunction = vertexFunction;
        pipelineDescriptor.fragmentFunction = fragmentFunction;
        pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        pipelineDescriptor.colorAttachments[0].blendingEnabled = YES;
        pipelineDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pipelineDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pipelineDescriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        pipelineDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        pipelineDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pipelineDescriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        id<MTLRenderPipelineState> pipeline =
            [device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
        if (!pipeline)
            throw std::runtime_error(std::string("Metal projected Gaussian pipeline failed: ") +
                                     [[error localizedDescription] UTF8String]);

        const NSUInteger projectedBytes = std::max<NSUInteger>(
            1U, static_cast<NSUInteger>(projected.size() * sizeof(GaussianProjectedSplat)));
        id<MTLBuffer> projectedBuffer =
            [device newBufferWithBytes:projected.empty() ? nullptr : projected.data()
                                length:projectedBytes
                               options:MTLResourceStorageModeShared];
        if (!projectedBuffer)
            throw std::runtime_error("Metal projected Gaussian buffer allocation failed");

        MTLTextureDescriptor* textureDescriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:settings.image.width
                                                              height:settings.image.height
                                                           mipmapped:NO];
        textureDescriptor.usage = MTLTextureUsageRenderTarget;
        textureDescriptor.storageMode =
            device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
        id<MTLTexture> texture = [device newTextureWithDescriptor:textureDescriptor];
        if (!texture) throw std::runtime_error("Metal projected Gaussian texture allocation failed");

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(
            settings.image.clearColor.r, settings.image.clearColor.g,
            settings.image.clearColor.b, settings.image.clearColor.a);

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) throw std::runtime_error("Metal projected Gaussian command queue failed");
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder =
            [commandBuffer renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipeline];
        if (!projected.empty()) {
            [encoder setVertexBuffer:projectedBuffer offset:0 atIndex:0];
            const float sigmaCutoff = static_cast<float>(settings.sigmaCutoff);
            [encoder setVertexBytes:&sigmaCutoff length:sizeof(sigmaCutoff) atIndex:1];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:projected.size() * 6U];
        }
        [encoder endEncoding];
        if (!device.hasUnifiedMemory) {
            id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
            [blit synchronizeResource:texture];
            [blit endEncoding];
        }
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if (commandBuffer.status == MTLCommandBufferStatusError)
            throw std::runtime_error(std::string("Metal projected Gaussian render failed: ") +
                                     [[commandBuffer.error localizedDescription] UTF8String]);

        ImageRGBA8 output{
            settings.image.width,
            settings.image.height,
            std::vector<std::uint8_t>(
                static_cast<std::size_t>(settings.image.width) *
                static_cast<std::size_t>(settings.image.height) * 4U)};
        [texture getBytes:output.pixels.data()
              bytesPerRow:settings.image.width * 4U
               fromRegion:MTLRegionMake2D(0, 0, settings.image.width, settings.image.height)
              mipmapLevel:0];
        return output;
    }
}

} // namespace vulkax::render
