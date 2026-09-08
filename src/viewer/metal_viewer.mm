#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "vulkax/viewer/metal_shader_source.hpp"
#include "vulkax/viewer/scene.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <simd/simd.h>

namespace {

using vulkax::math::Vec3;
using vulkax::viewer::ViewerScene;

enum class ViewMode : NSInteger { Hybrid = 0, Splats = 1, Surface = 2, Particles = 3 };
enum class WorldState : NSInteger { Before = 0, After = 1 };

struct GPUSplat {
    simd_float4 positionScaleX{};
    simd_float4 scaleYZOpacity{};
    simd_float4 rotation{};
    simd_float4 colorMark{};
};

struct GPUSurfaceVertex {
    simd_float4 position{};
    simd_float4 normal{};
    simd_float4 color{};
};

struct GPUUniforms {
    simd_float4x4 viewProjection{};
    simd_float4 viewportAndScale{};
    simd_float4 renderParams{};
};

[[nodiscard]] Vec3 cameraPosition(Vec3 target, double yaw, double pitch, double distance) {
    const double cp = std::cos(pitch);
    return {target.x + distance * cp * std::sin(yaw),
            target.y + distance * std::sin(pitch),
            target.z + distance * cp * std::cos(yaw)};
}

[[nodiscard]] simd_float4x4 perspective(float fovy, float aspect, float nearZ, float farZ) {
    const float ys = 1.0F / std::tan(fovy * 0.5F);
    const float xs = ys / std::max(aspect, 1.0e-6F);
    const float zs = farZ / (nearZ - farZ);
    return (simd_float4x4){
        (simd_float4){xs, 0, 0, 0},
        (simd_float4){0, ys, 0, 0},
        (simd_float4){0, 0, zs, -1},
        (simd_float4){0, 0, nearZ * zs, 0},
    };
}

[[nodiscard]] simd_float4x4 lookAt(Vec3 eyeValue, Vec3 centerValue) {
    const Vec3 backward = vulkax::math::normalized(eyeValue - centerValue);
    Vec3 right = vulkax::math::normalized(vulkax::math::cross(Vec3{0, 1, 0}, backward));
    if (vulkax::math::length(right) < 1.0e-9) right = {1, 0, 0};
    const Vec3 up = vulkax::math::cross(backward, right);
    const simd_float3 eye{static_cast<float>(eyeValue.x), static_cast<float>(eyeValue.y), static_cast<float>(eyeValue.z)};
    const simd_float3 x{static_cast<float>(right.x), static_cast<float>(right.y), static_cast<float>(right.z)};
    const simd_float3 y{static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z)};
    const simd_float3 z{static_cast<float>(backward.x), static_cast<float>(backward.y), static_cast<float>(backward.z)};
    return (simd_float4x4){
        (simd_float4){x.x, y.x, z.x, 0},
        (simd_float4){x.y, y.y, z.y, 0},
        (simd_float4){x.z, y.z, z.z, 0},
        (simd_float4){-simd_dot(x, eye), -simd_dot(y, eye), -simd_dot(z, eye), 1},
    };
}

[[nodiscard]] NSString* stringFromStd(const std::string& value) {
    return [NSString stringWithUTF8String:value.c_str()];
}

[[nodiscard]] std::string errorText(NSError* error) {
    if (error == nil || error.localizedDescription == nil) return "unknown Metal error";
    return std::string(error.localizedDescription.UTF8String);
}

} // namespace

@class VulkaxRenderer;

@interface VulkaxMetalView : MTKView
@property(nonatomic, weak) VulkaxRenderer* vulkaxRenderer;
@end

@interface VulkaxRenderer : NSObject <MTKViewDelegate> {
@private
    ViewerScene _scene;
    id<MTLDevice> _device;
    id<MTLCommandQueue> _queue;
    id<MTLRenderPipelineState> _splatPipeline;
    id<MTLRenderPipelineState> _surfacePipeline;
    id<MTLRenderPipelineState> _linePipeline;
    id<MTLDepthStencilState> _opaqueDepth;
    id<MTLDepthStencilState> _transparentDepth;
    id<MTLBuffer> _beforeBuffer;
    id<MTLBuffer> _afterBuffer;
    id<MTLBuffer> _particleBuffer;
    id<MTLBuffer> _surfaceBuffer;
    id<MTLBuffer> _gridBuffer;
    id<MTLBuffer> _beforeOrderBuffer;
    id<MTLBuffer> _afterOrderBuffer;
    id<MTLBuffer> _particleOrderBuffer;
    std::vector<std::uint32_t> _beforeOrder;
    std::vector<std::uint32_t> _afterOrder;
    std::vector<std::uint32_t> _particleOrder;
    Vec3 _target;
    double _yaw;
    double _pitch;
    double _distance;
    float _splatScale;
    float _opacity;
    float _exposure;
    ViewMode _mode;
    WorldState _state;
    BOOL _highlight;
    BOOL _showGrid;
    BOOL _autoOrbit;
    BOOL _sortDirty;
    std::chrono::steady_clock::time_point _lastFrame;
    __weak NSTextField* _statsLabel;
}
- (instancetype)initWithView:(MTKView*)view scene:(ViewerScene)scene;
- (void)setStatsLabel:(NSTextField*)label;
- (void)orbitDX:(double)dx dy:(double)dy;
- (void)panDX:(double)dx dy:(double)dy;
- (void)zoomDelta:(double)delta;
- (void)resetCamera;
- (void)handleKey:(NSString*)key;
- (IBAction)modeChanged:(NSSegmentedControl*)sender;
- (IBAction)stateChanged:(NSSegmentedControl*)sender;
- (IBAction)highlightChanged:(NSButton*)sender;
- (IBAction)gridChanged:(NSButton*)sender;
- (IBAction)orbitChanged:(NSButton*)sender;
- (IBAction)splatScaleChanged:(NSSlider*)sender;
- (IBAction)opacityChanged:(NSSlider*)sender;
- (IBAction)exposureChanged:(NSSlider*)sender;
- (IBAction)openPly:(id)sender;
@end

@implementation VulkaxMetalView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)mouseDragged:(NSEvent*)event { [self.vulkaxRenderer orbitDX:event.deltaX dy:event.deltaY]; }
- (void)rightMouseDragged:(NSEvent*)event { [self.vulkaxRenderer panDX:event.deltaX dy:event.deltaY]; }
- (void)otherMouseDragged:(NSEvent*)event { [self.vulkaxRenderer panDX:event.deltaX dy:event.deltaY]; }
- (void)scrollWheel:(NSEvent*)event { [self.vulkaxRenderer zoomDelta:event.scrollingDeltaY]; }
- (void)keyDown:(NSEvent*)event {
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if (key.length > 0) [self.vulkaxRenderer handleKey:key];
    else [super keyDown:event];
}
@end

@implementation VulkaxRenderer

- (id<MTLRenderPipelineState>)pipelineWithLibrary:(id<MTLLibrary>)library
                                           view:(MTKView*)view
                                         vertex:(NSString*)vertexName
                                       fragment:(NSString*)fragmentName
                                       blending:(BOOL)blending {
    MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = [library newFunctionWithName:vertexName];
    descriptor.fragmentFunction = [library newFunctionWithName:fragmentName];
    descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat;
    descriptor.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
    if (blending) {
        MTLRenderPipelineColorAttachmentDescriptor* attachment = descriptor.colorAttachments[0];
        attachment.blendingEnabled = YES;
        attachment.rgbBlendOperation = MTLBlendOperationAdd;
        attachment.alphaBlendOperation = MTLBlendOperationAdd;
        attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline = [_device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (!pipeline) throw std::runtime_error("Metal pipeline creation failed: " + errorText(error));
    return pipeline;
}

- (instancetype)initWithView:(MTKView*)view scene:(ViewerScene)scene {
    self = [super init];
    if (!self) return nil;
    _scene = std::move(scene);
    _device = view.device;
    _queue = [_device newCommandQueue];
    _splatScale = 1.0F;
    _opacity = 0.88F;
    _exposure = 1.0F;
    _mode = ViewMode::Hybrid;
    _state = WorldState::After;
    _highlight = YES;
    _showGrid = YES;
    _autoOrbit = NO;
    _sortDirty = YES;
    _lastFrame = std::chrono::steady_clock::now();

    NSString* source = [NSString stringWithUTF8String:vulkax::viewer::metal::shaderSource];
    NSError* error = nil;
    id<MTLLibrary> library = [_device newLibraryWithSource:source options:nil error:&error];
    if (!library) throw std::runtime_error("Metal shader compilation failed: " + errorText(error));
    _splatPipeline = [self pipelineWithLibrary:library view:view vertex:@"splatVertex" fragment:@"splatFragment" blending:YES];
    _surfacePipeline = [self pipelineWithLibrary:library view:view vertex:@"surfaceVertex" fragment:@"surfaceFragment" blending:NO];
    _linePipeline = [self pipelineWithLibrary:library view:view vertex:@"lineVertex" fragment:@"lineFragment" blending:YES];

    MTLDepthStencilDescriptor* opaque = [MTLDepthStencilDescriptor new];
    opaque.depthCompareFunction = MTLCompareFunctionLessEqual;
    opaque.depthWriteEnabled = YES;
    _opaqueDepth = [_device newDepthStencilStateWithDescriptor:opaque];
    MTLDepthStencilDescriptor* transparent = [MTLDepthStencilDescriptor new];
    transparent.depthCompareFunction = MTLCompareFunctionLessEqual;
    transparent.depthWriteEnabled = NO;
    _transparentDepth = [_device newDepthStencilStateWithDescriptor:transparent];

    [self uploadScene];
    [self resetCamera];
    return self;
}

- (id<MTLBuffer>)bufferFromBytes:(const void*)bytes length:(NSUInteger)length {
    if (length == 0) return nil;
    return [_device newBufferWithBytes:bytes length:length options:MTLResourceStorageModeShared];
}

- (std::vector<GPUSplat>)gpuGaussians:(const std::vector<vulkax::viewer::ViewerGaussian>&)source {
    std::vector<GPUSplat> out;
    out.reserve(source.size());
    for (const auto& g : source) {
        GPUSplat s;
        s.positionScaleX = {(float)g.position.x, (float)g.position.y, (float)g.position.z, g.scale[0]};
        s.scaleYZOpacity = {g.scale[1], g.scale[2], g.opacity, 0};
        s.rotation = {g.rotation[1], g.rotation[2], g.rotation[3], g.rotation[0]};
        s.colorMark = {g.color[0], g.color[1], g.color[2], 0};
        out.push_back(s);
    }
    return out;
}

- (void)uploadScene {
    const auto before = [self gpuGaussians:_scene.before];
    const auto after = [self gpuGaussians:_scene.after];
    _beforeBuffer = [self bufferFromBytes:before.data() length:before.size()*sizeof(GPUSplat)];
    _afterBuffer = [self bufferFromBytes:after.data() length:after.size()*sizeof(GPUSplat)];

    const float radius = static_cast<float>(std::max(0.0015, _scene.bounds.radius*0.014));
    std::vector<GPUSplat> particles;
    particles.reserve(_scene.particles.size());
    for (const auto& p : _scene.particles) {
        GPUSplat s;
        s.positionScaleX = {(float)p.position.x,(float)p.position.y,(float)p.position.z,radius};
        s.scaleYZOpacity = {radius,radius,0.97F,0};
        s.rotation = {0,0,0,1};
        s.colorMark = {0.42F,0.70F,1.0F,p.inRewriteRegion?1.0F:0.0F};
        particles.push_back(s);
    }
    _particleBuffer = [self bufferFromBytes:particles.data() length:particles.size()*sizeof(GPUSplat)];

    std::vector<GPUSurfaceVertex> surface;
    surface.reserve(_scene.surfaceTriangles.size());
    for (const auto& v : _scene.surfaceTriangles) {
        surface.push_back({
            {(float)v.position.x,(float)v.position.y,(float)v.position.z,1},
            {(float)v.normal.x,(float)v.normal.y,(float)v.normal.z,0},
            {v.color[0],v.color[1],v.color[2],1}
        });
    }
    _surfaceBuffer = [self bufferFromBytes:surface.data() length:surface.size()*sizeof(GPUSurfaceVertex)];

    _beforeOrder.resize(_scene.before.size());
    _afterOrder.resize(_scene.after.size());
    _particleOrder.resize(_scene.particles.size());
    std::iota(_beforeOrder.begin(),_beforeOrder.end(),0U);
    std::iota(_afterOrder.begin(),_afterOrder.end(),0U);
    std::iota(_particleOrder.begin(),_particleOrder.end(),0U);
    _beforeOrderBuffer = [self bufferFromBytes:_beforeOrder.data() length:_beforeOrder.size()*sizeof(std::uint32_t)];
    _afterOrderBuffer = [self bufferFromBytes:_afterOrder.data() length:_afterOrder.size()*sizeof(std::uint32_t)];
    _particleOrderBuffer = [self bufferFromBytes:_particleOrder.data() length:_particleOrder.size()*sizeof(std::uint32_t)];

    std::vector<simd_float4> grid;
    grid.reserve(84);
    const double y = _scene.bounds.minimum.y - _scene.bounds.radius*0.06;
    const double r = _scene.bounds.radius*1.45;
    for (int i=-10;i<=10;++i) {
        const double t = static_cast<double>(i)/10.0*r;
        grid.push_back({(float)(_scene.bounds.center.x-r),(float)y,(float)(_scene.bounds.center.z+t),1});
        grid.push_back({(float)(_scene.bounds.center.x+r),(float)y,(float)(_scene.bounds.center.z+t),1});
        grid.push_back({(float)(_scene.bounds.center.x+t),(float)y,(float)(_scene.bounds.center.z-r),1});
        grid.push_back({(float)(_scene.bounds.center.x+t),(float)y,(float)(_scene.bounds.center.z+r),1});
    }
    _gridBuffer = [self bufferFromBytes:grid.data() length:grid.size()*sizeof(simd_float4)];
    _sortDirty = YES;
    [self updateStats];
}

- (void)setStatsLabel:(NSTextField*)label { _statsLabel = label; [self updateStats]; }

- (void)updateStats {
    if (!_statsLabel) return;
    _statsLabel.stringValue = [NSString stringWithFormat:@"Gaussians   %lu → %lu\nParticles   %lu\nRewrite     %lu\nSurface     %@\nMax Δ       %.3e\n\n1–4 modes · B/A state\nH highlight · G grid\nSpace orbit · R camera",
        (unsigned long)_scene.before.size(),(unsigned long)_scene.after.size(),(unsigned long)_scene.particles.size(),
        (unsigned long)_scene.rewriteParticleCount,stringFromStd(_scene.surfaceKind),_scene.maxGaussianDisplacement];
}

- (void)resetCamera {
    _target = _scene.bounds.center;
    _yaw = 0.68;
    _pitch = 0.30;
    _distance = std::max(0.12,_scene.bounds.radius*3.2);
    _sortDirty = YES;
}

- (void)orbitDX:(double)dx dy:(double)dy {
    _yaw -= dx*0.006;
    _pitch = std::clamp(_pitch-dy*0.006,-1.45,1.45);
    _sortDirty = YES;
}

- (void)panDX:(double)dx dy:(double)dy {
    const Vec3 eye = cameraPosition(_target,_yaw,_pitch,_distance);
    const Vec3 forward = vulkax::math::normalized(_target-eye);
    Vec3 right = vulkax::math::normalized(vulkax::math::cross(forward,Vec3{0,1,0}));
    if (vulkax::math::length(right)<1.0e-9) right={1,0,0};
    const Vec3 up = vulkax::math::normalized(vulkax::math::cross(right,forward));
    const double scale = _distance*0.0015;
    _target += (-dx*scale)*right + (dy*scale)*up;
    _sortDirty = YES;
}

- (void)zoomDelta:(double)delta {
    _distance *= std::exp(-delta*0.035);
    _distance = std::clamp(_distance,std::max(0.002,_scene.bounds.radius*0.04),std::max(2.0,_scene.bounds.radius*50.0));
    _sortDirty = YES;
}

- (void)handleKey:(NSString*)key {
    if ([key isEqualToString:@"1"]) _mode=ViewMode::Hybrid;
    else if ([key isEqualToString:@"2"]) _mode=ViewMode::Splats;
    else if ([key isEqualToString:@"3"]) _mode=ViewMode::Surface;
    else if ([key isEqualToString:@"4"]) _mode=ViewMode::Particles;
    else if ([key isEqualToString:@"b"]) {_state=WorldState::Before;_sortDirty=YES;}
    else if ([key isEqualToString:@"a"]) {_state=WorldState::After;_sortDirty=YES;}
    else if ([key isEqualToString:@"h"]) _highlight=!_highlight;
    else if ([key isEqualToString:@"g"]) _showGrid=!_showGrid;
    else if ([key isEqualToString:@"r"]) [self resetCamera];
    else if ([key isEqualToString:@" "]) _autoOrbit=!_autoOrbit;
}

- (IBAction)modeChanged:(NSSegmentedControl*)s { _mode=static_cast<ViewMode>(s.selectedSegment); }
- (IBAction)stateChanged:(NSSegmentedControl*)s { _state=s.selectedSegment==0?WorldState::Before:WorldState::After;_sortDirty=YES; }
- (IBAction)highlightChanged:(NSButton*)s { _highlight=s.state==NSControlStateValueOn; }
- (IBAction)gridChanged:(NSButton*)s { _showGrid=s.state==NSControlStateValueOn; }
- (IBAction)orbitChanged:(NSButton*)s { _autoOrbit=s.state==NSControlStateValueOn; }
- (IBAction)splatScaleChanged:(NSSlider*)s { _splatScale=(float)s.doubleValue; }
- (IBAction)opacityChanged:(NSSlider*)s { _opacity=(float)s.doubleValue; }
- (IBAction)exposureChanged:(NSSlider*)s { _exposure=(float)s.doubleValue; }

- (IBAction)openPly:(id)sender {
    (void)sender;
    NSOpenPanel* panel=[NSOpenPanel openPanel];
    panel.canChooseDirectories=NO;
    panel.allowsMultipleSelection=NO;
    panel.allowedFileTypes=@[@"ply"];
    if ([panel runModal]!=NSModalResponseOK || panel.URL==nil) return;
    try {
        _scene=vulkax::viewer::loadStandaloneGaussianScene(std::filesystem::path(panel.URL.path.UTF8String));
        [self uploadScene];
        [self resetCamera];
    } catch (const std::exception& e) {
        NSAlert* alert=[NSAlert new];
        alert.messageText=@"Could not open Gaussian PLY";
        alert.informativeText=stringFromStd(e.what());
        [alert runModal];
    }
}

- (void)sortSource:(const std::vector<vulkax::viewer::ViewerGaussian>&)source
             order:(std::vector<std::uint32_t>&)order
            buffer:(id<MTLBuffer>)buffer eye:(Vec3)eye forward:(Vec3)forward {
    if (!buffer || source.size()!=order.size()) return;
    std::sort(order.begin(),order.end(),[&](std::uint32_t a,std::uint32_t b){
        return vulkax::math::dot(source[a].position-eye,forward)>vulkax::math::dot(source[b].position-eye,forward);
    });
    std::memcpy(buffer.contents,order.data(),order.size()*sizeof(std::uint32_t));
}

- (void)updateSort {
    if (!_sortDirty) return;
    const Vec3 eye=cameraPosition(_target,_yaw,_pitch,_distance);
    const Vec3 forward=vulkax::math::normalized(_target-eye);
    [self sortSource:_scene.before order:_beforeOrder buffer:_beforeOrderBuffer eye:eye forward:forward];
    [self sortSource:_scene.after order:_afterOrder buffer:_afterOrderBuffer eye:eye forward:forward];
    _sortDirty=NO;
}

- (GPUUniforms)uniformsForView:(MTKView*)view particles:(BOOL)particles {
    const Vec3 eye=cameraPosition(_target,_yaw,_pitch,_distance);
    const float aspect=(float)(view.drawableSize.width/std::max(view.drawableSize.height,1.0));
    const float nearZ=(float)std::max(0.0005,_scene.bounds.radius*0.008);
    const float farZ=(float)std::max(10.0,_scene.bounds.radius*60.0+_distance);
    GPUUniforms u;
    u.viewProjection=simd_mul(perspective(45.0F*(float)M_PI/180.0F,aspect,nearZ,farZ),lookAt(eye,_target));
    u.viewportAndScale={(float)view.drawableSize.width,(float)view.drawableSize.height,particles?0.72F:_splatScale,particles?34.0F:128.0F};
    u.renderParams={_opacity,_exposure,_highlight?1.0F:0.0F,particles?1.0F:0.0F};
    return u;
}

- (void)drawSplats:(id<MTLRenderCommandEncoder>)encoder view:(MTKView*)view
            buffer:(id<MTLBuffer>)buffer order:(id<MTLBuffer>)order count:(NSUInteger)count particles:(BOOL)particles {
    if (!buffer || !order || count==0) return;
    GPUUniforms u=[self uniformsForView:view particles:particles];
    [encoder setRenderPipelineState:_splatPipeline];
    [encoder setDepthStencilState:_transparentDepth];
    [encoder setVertexBuffer:buffer offset:0 atIndex:0];
    [encoder setVertexBuffer:order offset:0 atIndex:1];
    [encoder setVertexBytes:&u length:sizeof(u) atIndex:2];
    [encoder setFragmentBytes:&u length:sizeof(u) atIndex:2];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:count];
}

- (void)drawInMTKView:(MTKView*)view {
    const auto now=std::chrono::steady_clock::now();
    const double dt=std::chrono::duration<double>(now-_lastFrame).count();
    _lastFrame=now;
    if (_autoOrbit) {_yaw+=dt*0.28;_sortDirty=YES;}
    [self updateSort];

    id<CAMetalDrawable> drawable=view.currentDrawable;
    MTLRenderPassDescriptor* pass=view.currentRenderPassDescriptor;
    if (!drawable || !pass) return;
    pass.colorAttachments[0].clearColor=MTLClearColorMake(0.018,0.028,0.060,1.0);
    pass.colorAttachments[0].loadAction=MTLLoadActionClear;
    pass.colorAttachments[0].storeAction=MTLStoreActionStore;
    pass.depthAttachment.clearDepth=1.0;
    pass.depthAttachment.loadAction=MTLLoadActionClear;
    pass.depthAttachment.storeAction=MTLStoreActionDontCare;

    id<MTLCommandBuffer> command=[_queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder=[command renderCommandEncoderWithDescriptor:pass];

    if (_showGrid && _gridBuffer) {
        GPUUniforms u=[self uniformsForView:view particles:NO];
        [encoder setRenderPipelineState:_linePipeline];
        [encoder setDepthStencilState:_transparentDepth];
        [encoder setVertexBuffer:_gridBuffer offset:0 atIndex:0];
        [encoder setVertexBytes:&u length:sizeof(u) atIndex:2];
        [encoder drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:84];
    }
    if ((_mode==ViewMode::Hybrid || _mode==ViewMode::Surface) && _surfaceBuffer && !_scene.surfaceTriangles.empty()) {
        GPUUniforms u=[self uniformsForView:view particles:NO];
        [encoder setRenderPipelineState:_surfacePipeline];
        [encoder setDepthStencilState:_opaqueDepth];
        [encoder setVertexBuffer:_surfaceBuffer offset:0 atIndex:0];
        [encoder setVertexBytes:&u length:sizeof(u) atIndex:2];
        [encoder setFragmentBytes:&u length:sizeof(u) atIndex:2];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:_scene.surfaceTriangles.size()];
    }
    if (_mode==ViewMode::Hybrid || _mode==ViewMode::Splats) {
        const BOOL before=_state==WorldState::Before;
        [self drawSplats:encoder view:view buffer:before?_beforeBuffer:_afterBuffer order:before?_beforeOrderBuffer:_afterOrderBuffer
                    count:before?_scene.before.size():_scene.after.size() particles:NO];
    }
    if (_mode==ViewMode::Hybrid || _mode==ViewMode::Particles)
        [self drawSplats:encoder view:view buffer:_particleBuffer order:_particleOrderBuffer count:_scene.particles.size() particles:YES];

    [encoder endEncoding];
    [command presentDrawable:drawable];
    [command commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size { (void)view;(void)size;_sortDirty=YES; }
@end

static NSTextField* makeLabel(NSString* text, CGFloat size, NSFontWeight weight) {
    NSTextField* field=[NSTextField labelWithString:text];
    field.font=[NSFont systemFontOfSize:size weight:weight];
    field.textColor=[NSColor colorWithCalibratedWhite:0.92 alpha:1.0];
    return field;
}

static NSView* makeSlider(NSString* title,double min,double max,double value,id target,SEL action) {
    NSStackView* box=[NSStackView stackViewWithViews:@[]];
    box.orientation=NSUserInterfaceLayoutOrientationVertical;
    box.spacing=4;
    NSTextField* caption=makeLabel(title,11,NSFontWeightMedium);
    caption.textColor=[NSColor colorWithCalibratedRed:0.64 green:0.73 blue:0.86 alpha:1.0];
    NSSlider* slider=[NSSlider sliderWithValue:value minValue:min maxValue:max target:target action:action];
    slider.continuous=YES;
    [box addArrangedSubview:caption];
    [box addArrangedSubview:slider];
    return box;
}

static NSVisualEffectView* makeInspector(VulkaxRenderer* renderer,NSTextField** statsOut) {
    NSVisualEffectView* panel=[NSVisualEffectView new];
    panel.material=NSVisualEffectMaterialSidebar;
    panel.blendingMode=NSVisualEffectBlendingModeBehindWindow;
    panel.state=NSVisualEffectStateActive;
    NSStackView* stack=[NSStackView stackViewWithViews:@[]];
    stack.translatesAutoresizingMaskIntoConstraints=NO;
    stack.orientation=NSUserInterfaceLayoutOrientationVertical;
    stack.alignment=NSLayoutAttributeLeading;
    stack.spacing=12;
    stack.edgeInsets=NSEdgeInsetsMake(20,18,18,18);
    [panel addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor],[stack.topAnchor constraintEqualToAnchor:panel.topAnchor]]];

    NSTextField* brand=makeLabel(@"VULKAX NATIVE VIEWER",11,NSFontWeightSemibold);
    brand.textColor=[NSColor colorWithCalibratedRed:0.52 green:0.70 blue:1.0 alpha:1.0];
    [stack addArrangedSubview:brand];
    [stack addArrangedSubview:makeLabel(@"Interactive Gaussian World",20,NSFontWeightBold)];

    NSSegmentedControl* mode=[NSSegmentedControl segmentedControlWithLabels:@[@"Hybrid",@"Splats",@"Surface",@"Particles"] trackingMode:NSSegmentSwitchTrackingSelectOne target:renderer action:@selector(modeChanged:)];
    mode.selectedSegment=0; [stack addArrangedSubview:mode];
    NSSegmentedControl* state=[NSSegmentedControl segmentedControlWithLabels:@[@"Before",@"Verified After"] trackingMode:NSSegmentSwitchTrackingSelectOne target:renderer action:@selector(stateChanged:)];
    state.selectedSegment=1; [stack addArrangedSubview:state];

    NSButton* highlight=[NSButton checkboxWithTitle:@"Rewrite highlight" target:renderer action:@selector(highlightChanged:)]; highlight.state=NSControlStateValueOn;
    NSButton* grid=[NSButton checkboxWithTitle:@"Ground grid" target:renderer action:@selector(gridChanged:)]; grid.state=NSControlStateValueOn;
    NSButton* orbit=[NSButton checkboxWithTitle:@"Auto orbit" target:renderer action:@selector(orbitChanged:)];
    [stack addArrangedSubview:highlight];[stack addArrangedSubview:grid];[stack addArrangedSubview:orbit];
    [stack addArrangedSubview:makeSlider(@"Splat scale",0.2,4.0,1.0,renderer,@selector(splatScaleChanged:))];
    [stack addArrangedSubview:makeSlider(@"Opacity",0.05,1.0,0.88,renderer,@selector(opacityChanged:))];
    [stack addArrangedSubview:makeSlider(@"Exposure",0.35,2.4,1.0,renderer,@selector(exposureChanged:))];
    [stack addArrangedSubview:[NSButton buttonWithTitle:@"Open Gaussian PLY…" target:renderer action:@selector(openPly:)]];
    NSTextField* stats=makeLabel(@"",11,NSFontWeightRegular); stats.textColor=[NSColor colorWithCalibratedRed:0.68 green:0.77 blue:0.90 alpha:1.0]; stats.maximumNumberOfLines=0;
    [stack addArrangedSubview:stats]; *statsOut=stats;
    return panel;
}

@interface VulkaxAppDelegate : NSObject <NSApplicationDelegate,NSWindowDelegate> {
@private ViewerScene _initialScene;
}
@property(nonatomic,strong) NSWindow* window;
@property(nonatomic,strong) VulkaxRenderer* renderer;
- (instancetype)initWithScene:(ViewerScene)scene;
@end

@implementation VulkaxAppDelegate
- (instancetype)initWithScene:(ViewerScene)scene { self=[super init]; if(self) _initialScene=std::move(scene); return self; }
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if(!device){[NSApp terminate:nil];return;}
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1320,820) styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable) backing:NSBackingStoreBuffered defer:NO];
    self.window.title=@"Vulkax — Native Gaussian World Viewer"; self.window.minSize=NSMakeSize(920,620); self.window.delegate=self; [self.window center];
    NSView* root=[NSView new]; self.window.contentView=root;
    VulkaxMetalView* view=[[VulkaxMetalView alloc] initWithFrame:NSZeroRect device:device];
    view.translatesAutoresizingMaskIntoConstraints=NO; view.colorPixelFormat=MTLPixelFormatBGRA8Unorm_sRGB; view.depthStencilPixelFormat=MTLPixelFormatDepth32Float; view.preferredFramesPerSecond=60; view.paused=NO; view.enableSetNeedsDisplay=NO;
    try { self.renderer=[[VulkaxRenderer alloc] initWithView:view scene:std::move(_initialScene)]; }
    catch(const std::exception& e){NSAlert* a=[NSAlert new];a.messageText=@"Vulkax renderer initialization failed";a.informativeText=stringFromStd(e.what());[a runModal];[NSApp terminate:nil];return;}
    view.delegate=self.renderer; view.vulkaxRenderer=self.renderer;
    NSTextField* stats=nil; NSVisualEffectView* inspector=makeInspector(self.renderer,&stats); inspector.translatesAutoresizingMaskIntoConstraints=NO; [self.renderer setStatsLabel:stats];
    [root addSubview:view];[root addSubview:inspector];
    [NSLayoutConstraint activateConstraints:@[[view.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],[view.topAnchor constraintEqualToAnchor:root.topAnchor],[view.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],[view.trailingAnchor constraintEqualToAnchor:inspector.leadingAnchor],[inspector.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],[inspector.topAnchor constraintEqualToAnchor:root.topAnchor],[inspector.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],[inspector.widthAnchor constraintEqualToConstant:300]]];
    [self.window makeKeyAndOrderFront:nil];[self.window makeFirstResponder:view];[NSApp activateIgnoringOtherApps:YES];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {(void)sender;return YES;}
@end

static void installMenu() {
    NSMenu* bar=[NSMenu new];NSMenuItem* root=[NSMenuItem new];[bar addItem:root];NSApp.mainMenu=bar;
    NSMenu* menu=[NSMenu new];[menu addItemWithTitle:@"About Vulkax Viewer" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];[menu addItem:[NSMenuItem separatorItem]];[menu addItemWithTitle:@"Quit Vulkax Viewer" action:@selector(terminate:) keyEquivalent:@"q"];root.submenu=menu;
}

struct Arguments {std::filesystem::path run{"build/captured-world-run"};std::filesystem::path particles{};std::filesystem::path ply{};};
static Arguments parseArgs(int argc,const char* argv[]){Arguments a;for(int i=1;i<argc;++i){std::string v=argv[i];auto next=[&](const char* f){if(i+1>=argc)throw std::runtime_error(std::string(f)+" requires a path");return std::string(argv[++i]);};if(v=="--run")a.run=next("--run");else if(v=="--particles")a.particles=next("--particles");else if(v=="--ply")a.ply=next("--ply");else if(v=="--help"||v=="-h"){std::cout<<"Usage: vulkax_viewer [--run captured-world-run] [--particles particles.csv] [--ply gaussians.ply]\n";std::exit(0);}else if(!v.starts_with('-'))a.run=v;else throw std::runtime_error("unknown argument: "+v);}return a;}

int main(int argc,const char* argv[]){@autoreleasepool{try{const auto args=parseArgs(argc,argv);ViewerScene scene=args.ply.empty()?vulkax::viewer::loadCapturedWorldScene(args.run,args.particles):vulkax::viewer::loadStandaloneGaussianScene(args.ply);[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];installMenu();VulkaxAppDelegate* delegate=[[VulkaxAppDelegate alloc] initWithScene:std::move(scene)];NSApp.delegate=delegate;[NSApp run];return 0;}catch(const std::exception& e){std::cerr<<"vulkax_viewer: "<<e.what()<<'\n';return 1;}}}
