#include "vulkax/render/headless.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace vulkax::render {
namespace { struct Vertex{float x,y,z,r,g,b,a,size;}; }

ImageRGBA8 renderParticlesMetal(const std::vector<visualization::ParticleInstance>& particles,const RenderSettings& settings){
 @autoreleasepool {
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(device==nil)throw std::runtime_error("no Metal device");
    NSString* source=@R"metal(
#include <metal_stdlib>
using namespace metal;
struct Vin{float3 position [[attribute(0)]];float4 color [[attribute(1)]];float pointSize [[attribute(2)]];};
struct Vout{float4 position [[position]];float4 color;float pointSize [[point_size]];};
vertex Vout particle_vertex(Vin in [[stage_in]],constant float& scale [[buffer(1)]]){Vout o;o.position=float4(in.position.x/scale,-in.position.y/scale,clamp(in.position.z/scale,-1.0f,1.0f),1);o.color=in.color;o.pointSize=max(in.pointSize,1.0f);return o;}
fragment float4 particle_fragment(Vout in [[stage_in]],float2 pc [[point_coord]]){float2 d=pc*2.0f-1.0f;float r2=dot(d,d);if(r2>1.0f)discard_fragment();float l=0.45f+0.55f*sqrt(max(0.0f,1.0f-r2));return float4(in.color.rgb*l,in.color.a);}
)metal";
    NSError* error=nil;id<MTLLibrary> library=[device newLibraryWithSource:source options:nil error:&error];if(!library)throw std::runtime_error(std::string("Metal render shader compile failed: ")+[[error localizedDescription] UTF8String]);id<MTLFunction> vs=[library newFunctionWithName:@"particle_vertex"];id<MTLFunction> fs=[library newFunctionWithName:@"particle_fragment"];
    MTLVertexDescriptor* vd=[[MTLVertexDescriptor alloc]init];vd.attributes[0].format=MTLVertexFormatFloat3;vd.attributes[0].offset=0;vd.attributes[0].bufferIndex=0;vd.attributes[1].format=MTLVertexFormatFloat4;vd.attributes[1].offset=12;vd.attributes[1].bufferIndex=0;vd.attributes[2].format=MTLVertexFormatFloat;vd.attributes[2].offset=28;vd.attributes[2].bufferIndex=0;vd.layouts[0].stride=sizeof(Vertex);vd.layouts[0].stepFunction=MTLVertexStepFunctionPerVertex;
    MTLRenderPipelineDescriptor* pd=[[MTLRenderPipelineDescriptor alloc]init];pd.vertexFunction=vs;pd.fragmentFunction=fs;pd.vertexDescriptor=vd;pd.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;id<MTLRenderPipelineState> pipeline=[device newRenderPipelineStateWithDescriptor:pd error:&error];if(!pipeline)throw std::runtime_error(std::string("Metal render pipeline failed: ")+[[error localizedDescription] UTF8String]);
    std::vector<visualization::ParticleInstance> sorted=particles;std::stable_sort(sorted.begin(),sorted.end(),[](const auto&a,const auto&b){return a.position.z<b.position.z;});std::vector<Vertex> vertices;vertices.reserve(sorted.size());const double minDim=static_cast<double>(std::min(settings.width,settings.height));for(const auto&p:sorted){float size=static_cast<float>(std::max(1.0,2.0*p.radius/settings.worldScale*minDim));vertices.push_back({static_cast<float>(p.position.x),static_cast<float>(p.position.y),static_cast<float>(p.position.z),p.color.r,p.color.g,p.color.b,p.color.a,size});}id<MTLBuffer> vb=[device newBufferWithBytes:vertices.empty()?nullptr:vertices.data() length:std::max<NSUInteger>(1,vertices.size()*sizeof(Vertex)) options:MTLResourceStorageModeShared];
    MTLTextureDescriptor* td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:settings.width height:settings.height mipmapped:NO];td.usage=MTLTextureUsageRenderTarget;td.storageMode=device.hasUnifiedMemory?MTLStorageModeShared:MTLStorageModeManaged;id<MTLTexture> texture=[device newTextureWithDescriptor:td];if(!texture)throw std::runtime_error("Metal render texture allocation failed");MTLRenderPassDescriptor* rp=[MTLRenderPassDescriptor renderPassDescriptor];rp.colorAttachments[0].texture=texture;rp.colorAttachments[0].loadAction=MTLLoadActionClear;rp.colorAttachments[0].storeAction=MTLStoreActionStore;rp.colorAttachments[0].clearColor=MTLClearColorMake(settings.clearColor.r,settings.clearColor.g,settings.clearColor.b,settings.clearColor.a);
    id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> cb=[queue commandBuffer];id<MTLRenderCommandEncoder> enc=[cb renderCommandEncoderWithDescriptor:rp];[enc setRenderPipelineState:pipeline];if(!vertices.empty()){[enc setVertexBuffer:vb offset:0 atIndex:0];float scale=static_cast<float>(settings.worldScale);[enc setVertexBytes:&scale length:sizeof(float) atIndex:1];[enc drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:vertices.size()];}[enc endEncoding];if(!device.hasUnifiedMemory){id<MTLBlitCommandEncoder> blit=[cb blitCommandEncoder];[blit synchronizeResource:texture];[blit endEncoding];}[cb commit];[cb waitUntilCompleted];if(cb.status==MTLCommandBufferStatusError)throw std::runtime_error(std::string("Metal render failed: ")+[[cb.error localizedDescription] UTF8String]);ImageRGBA8 output{settings.width,settings.height,std::vector<std::uint8_t>(static_cast<std::size_t>(settings.width)*settings.height*4u)};[texture getBytes:output.pixels.data() bytesPerRow:settings.width*4u fromRegion:MTLRegionMake2D(0,0,settings.width,settings.height) mipmapLevel:0];return output;
 }
}

} // namespace vulkax::render
