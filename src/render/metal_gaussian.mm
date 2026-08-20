#include "vulkax/render/gaussian.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace vulkax::render {

ImageRGBA8 renderGaussianVerticesMetal(const std::vector<GaussianRasterVertex>& vertices,
                                       const RenderSettings& settings) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) throw std::runtime_error("no Metal device");

        NSString* source = @R"metal(
#include <metal_stdlib>
using namespace metal;
struct GaussianVertex {
    float3 clip [[attribute(0)]];
    float2 local [[attribute(1)]];
    float4 colorOpacity [[attribute(2)]];
};
struct GaussianVarying {
    float4 position [[position]];
    float2 local;
    float4 colorOpacity;
};
vertex GaussianVarying gaussian_vertex(GaussianVertex in [[stage_in]]) {
    GaussianVarying out;
    out.position = float4(in.clip, 1.0f);
    out.local = in.local;
    out.colorOpacity = in.colorOpacity;
    return out;
}
fragment float4 gaussian_fragment(GaussianVarying in [[stage_in]]) {
    float alpha = in.colorOpacity.a * exp(-0.5f * dot(in.local, in.local));
    if (alpha < (1.0f / 255.0f)) discard_fragment();
    alpha = min(alpha, 0.999f);
    return float4(in.colorOpacity.rgb, alpha);
}
)metal";

        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library)
            throw std::runtime_error(std::string("Metal Gaussian shader compile failed: ") +
                                     [[error localizedDescription] UTF8String]);
        id<MTLFunction> vertexFunction = [library newFunctionWithName:@"gaussian_vertex"];
        id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"gaussian_fragment"];

        MTLVertexDescriptor* descriptor = [[MTLVertexDescriptor alloc] init];
        descriptor.attributes[0].format = MTLVertexFormatFloat3;
        descriptor.attributes[0].offset = 0;
        descriptor.attributes[0].bufferIndex = 0;
        descriptor.attributes[1].format = MTLVertexFormatFloat2;
        descriptor.attributes[1].offset = 12;
        descriptor.attributes[1].bufferIndex = 0;
        descriptor.attributes[2].format = MTLVertexFormatFloat4;
        descriptor.attributes[2].offset = 20;
        descriptor.attributes[2].bufferIndex = 0;
        descriptor.layouts[0].stride = sizeof(GaussianRasterVertex);
        descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDescriptor.vertexFunction = vertexFunction;
        pipelineDescriptor.fragmentFunction = fragmentFunction;
        pipelineDescriptor.vertexDescriptor = descriptor;
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
            throw std::runtime_error(std::string("Metal Gaussian pipeline failed: ") +
                                     [[error localizedDescription] UTF8String]);

        const NSUInteger vertexBytes = std::max<NSUInteger>(
            1, static_cast<NSUInteger>(vertices.size() * sizeof(GaussianRasterVertex)));
        id<MTLBuffer> vertexBuffer = [device newBufferWithBytes:vertices.empty() ? nullptr : vertices.data()
                                                        length:vertexBytes
                                                       options:MTLResourceStorageModeShared];
        if (!vertexBuffer) throw std::runtime_error("Metal Gaussian vertex-buffer allocation failed");

        MTLTextureDescriptor* textureDescriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:settings.width
                                                              height:settings.height
                                                           mipmapped:NO];
        textureDescriptor.usage = MTLTextureUsageRenderTarget;
        textureDescriptor.storageMode = device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
        id<MTLTexture> texture = [device newTextureWithDescriptor:textureDescriptor];
        if (!texture) throw std::runtime_error("Metal Gaussian render texture allocation failed");

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(
            settings.clearColor.r, settings.clearColor.g, settings.clearColor.b, settings.clearColor.a);

        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipeline];
        if (!vertices.empty()) {
            [encoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:vertices.size()];
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
            throw std::runtime_error(std::string("Metal Gaussian render failed: ") +
                                     [[commandBuffer.error localizedDescription] UTF8String]);

        ImageRGBA8 output{
            settings.width,
            settings.height,
            std::vector<std::uint8_t>(static_cast<std::size_t>(settings.width) *
                                      static_cast<std::size_t>(settings.height) * 4U)};
        [texture getBytes:output.pixels.data()
              bytesPerRow:settings.width * 4U
               fromRegion:MTLRegionMake2D(0, 0, settings.width, settings.height)
              mipmapLevel:0];
        return output;
    }
}

} // namespace vulkax::render
