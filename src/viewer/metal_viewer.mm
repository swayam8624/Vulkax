#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "vulkax/viewer/asset_import.hpp"
#include "vulkax/viewer/gpu_sort_session.hpp"
#include "vulkax/viewer/metal_gpu_sorter.hpp"
#include "vulkax/viewer/metal_shader_source.hpp"
#include "vulkax/viewer/scene.hpp"
#include "vulkax/viewer/visibility.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <simd/simd.h>

namespace {
using vulkax::math::Vec3;
using vulkax::viewer::ViewerScene;

enum class ViewMode : NSInteger { Hybrid = 0, Splats = 1, Surface = 2, Particles = 3, Compare = 4 };
enum class WorldState : NSInteger { Before = 0, After = 1 };

struct GPUSplat { simd_float4 positionScaleX{}, scaleYZOpacity{}, rotation{}, colorMark{}; };
struct GPUAppearance { simd_float4 packed[12]{}; simd_uint4 meta{}; };
struct GPUSurfaceVertex { simd_float4 position{}, normal{}, color{}; };
struct GPUUniforms {
    simd_float4x4 viewProjection{};
    simd_float4 viewportAndScale{}, renderParams{};
    simd_float4 cameraPositionAndFlags{};
    simd_float4 cameraRightAndFocalX{};
    simd_float4 cameraUpAndFocalY{};
    simd_float4 cameraBackAndMinSigma{};
};

Vec3 cameraPosition(Vec3 target, double yaw, double pitch, double distance) {
    const double cp = std::cos(pitch);
    return {target.x + distance * cp * std::sin(yaw), target.y + distance * std::sin(pitch),
            target.z + distance * cp * std::cos(yaw)};
}

simd_float4x4 perspective(float fovy, float aspect, float nearZ, float farZ) {
    const float ys = 1.0F / std::tan(fovy * 0.5F), xs = ys / std::max(aspect, 1.0e-6F);
    const float zs = farZ / (nearZ - farZ);
    return (simd_float4x4){(simd_float4){xs,0,0,0}, (simd_float4){0,ys,0,0},
                           (simd_float4){0,0,zs,-1}, (simd_float4){0,0,nearZ*zs,0}};
}

simd_float4x4 lookAt(Vec3 eyeValue, Vec3 centerValue) {
    const Vec3 back = vulkax::math::normalized(eyeValue - centerValue);
    Vec3 right = vulkax::math::normalized(vulkax::math::cross(Vec3{0,1,0}, back));
    if (vulkax::math::length(right) < 1.0e-9) right = {1,0,0};
    const Vec3 up = vulkax::math::cross(back, right);
    const simd_float3 eye = {(float)eyeValue.x,(float)eyeValue.y,(float)eyeValue.z};
    const simd_float3 x = {(float)right.x,(float)right.y,(float)right.z};
    const simd_float3 y = {(float)up.x,(float)up.y,(float)up.z};
    const simd_float3 z = {(float)back.x,(float)back.y,(float)back.z};
    return (simd_float4x4){(simd_float4){x.x,y.x,z.x,0}, (simd_float4){x.y,y.y,z.y,0},
                           (simd_float4){x.z,y.z,z.z,0},
                           (simd_float4){-simd_dot(x,eye),-simd_dot(y,eye),-simd_dot(z,eye),1}};
}

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }
std::string errorText(NSError* e) { return (e && e.localizedDescription) ? std::string(e.localizedDescription.UTF8String) : "unknown Metal error"; }

MTLScissorRect scissor(NSUInteger x, NSUInteger y, NSUInteger width, NSUInteger height) {
    MTLScissorRect rect; rect.x=x; rect.y=y; rect.width=width; rect.height=height; return rect;
}
MTLViewport viewport(double x, double y, double width, double height) {
    MTLViewport value; value.originX=x; value.originY=y; value.width=width; value.height=height; value.znear=0.0; value.zfar=1.0; return value;
}

double elapsedMilliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

std::vector<vulkax::viewer::GpuDepthKey> referenceDepthKeys(
    const std::vector<vulkax::viewer::ViewerGaussian>& source,
    const std::vector<std::uint32_t>& order,
    Vec3 eye,
    Vec3 forward) {
    const float ex = static_cast<float>(eye.x);
    const float ey = static_cast<float>(eye.y);
    const float ez = static_cast<float>(eye.z);
    const float fx = static_cast<float>(forward.x);
    const float fy = static_cast<float>(forward.y);
    const float fz = static_cast<float>(forward.z);
    std::vector<vulkax::viewer::GpuDepthKey> result;
    result.reserve(order.size());
    for (const auto index : order) {
        const auto& p = source[index].position;
        const float dx = static_cast<float>(p.x) - ex;
        const float dy = static_cast<float>(p.y) - ey;
        const float dz = static_cast<float>(p.z) - ez;
        result.push_back({dx*fx + dy*fy + dz*fz, index});
    }
    return result;
}
} // namespace

@class VulkaxRenderer;

@interface VulkaxMetalView : MTKView <NSDraggingDestination>
@property(nonatomic, weak) VulkaxRenderer* renderer;
@end

@interface VulkaxRenderer : NSObject <MTKViewDelegate> {
@private
    ViewerScene _scene;
    id<MTLDevice> _device;
    id<MTLCommandQueue> _queue;
    id<MTLRenderPipelineState> _splatPipeline, _surfacePipeline, _linePipeline;
    id<MTLDepthStencilState> _opaqueDepth, _transparentDepth;
    id<MTLBuffer> _beforeBuffer, _afterBuffer, _particleBuffer, _surfaceBuffer, _gridBuffer;
    id<MTLBuffer> _beforeAppearanceBuffer, _afterAppearanceBuffer, _particleAppearanceBuffer;
    id<MTLBuffer> _beforeOrderBuffer, _afterOrderBuffer, _particleOrderBuffer;
    std::vector<std::uint32_t> _beforeOrder, _afterOrder, _particleOrder;
    std::size_t _beforeVisible, _afterVisible, _splatBudget;
    std::size_t _beforeCulled, _afterCulled;
    std::unique_ptr<vulkax::viewer::MetalGpuSorter> _gpuSorter;
    vulkax::viewer::GpuSortSession _gpuSortSession;
    double _gpuSortMilliseconds, _visibilityMilliseconds;
    Vec3 _target;
    double _yaw, _pitch, _distance;
    float _splatScale, _opacity, _exposure;
    ViewMode _mode;
    WorldState _state;
    BOOL _highlight, _showGrid, _autoOrbit, _visibilityDirty, _shEnabled;
    BOOL _captureRequested;
    __strong NSURL* _captureURL;
    std::chrono::steady_clock::time_point _lastFrame, _fpsEpoch;
    unsigned _fpsFrames;
    double _fps;
    __weak NSTextField* _stats;
}
- (instancetype)initWithView:(MTKView*)view scene:(ViewerScene)scene;
- (void)setStatsLabel:(NSTextField*)label;
- (void)resetSortValidation;
- (void)orbitDX:(double)dx dy:(double)dy;
- (void)panDX:(double)dx dy:(double)dy;
- (void)zoomDelta:(double)delta;
- (void)handleKey:(NSString*)key;
- (void)loadPlyURL:(NSURL*)url;
- (void)loadObjURL:(NSURL*)url;
- (void)loadRunURL:(NSURL*)url;
- (IBAction)modeChanged:(NSSegmentedControl*)sender;
- (IBAction)stateChanged:(NSSegmentedControl*)sender;
- (IBAction)highlightChanged:(NSButton*)sender;
- (IBAction)gridChanged:(NSButton*)sender;
- (IBAction)orbitChanged:(NSButton*)sender;
- (IBAction)shChanged:(NSButton*)sender;
- (IBAction)splatScaleChanged:(NSSlider*)sender;
- (IBAction)opacityChanged:(NSSlider*)sender;
- (IBAction)exposureChanged:(NSSlider*)sender;
- (IBAction)budgetChanged:(NSSlider*)sender;
- (IBAction)openPly:(id)sender;
- (IBAction)openObj:(id)sender;
- (IBAction)openRun:(id)sender;
- (IBAction)capturePng:(id)sender;
@end

@implementation VulkaxMetalView
- (instancetype)initWithFrame:(NSRect)frameRect device:(id<MTLDevice>)device {
    self = [super initWithFrame:frameRect device:device];
    if (self) [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    return self;
}
- (BOOL)acceptsFirstResponder { return YES; }
- (void)mouseDragged:(NSEvent*)e { [self.renderer orbitDX:e.deltaX dy:e.deltaY]; }
- (void)rightMouseDragged:(NSEvent*)e { [self.renderer panDX:e.deltaX dy:e.deltaY]; }
- (void)otherMouseDragged:(NSEvent*)e { [self.renderer panDX:e.deltaX dy:e.deltaY]; }
- (void)scrollWheel:(NSEvent*)e { [self.renderer zoomDelta:e.scrollingDeltaY]; }
- (void)keyDown:(NSEvent*)e {
    NSString* key = e.charactersIgnoringModifiers.lowercaseString;
    if (key.length) [self.renderer handleKey:key]; else [super keyDown:e];
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSURL* url = [NSURL URLFromPasteboard:sender.draggingPasteboard];
    if (!url) return NSDragOperationNone;
    NSNumber* directory = nil;
    [url getResourceValue:&directory forKey:NSURLIsDirectoryKey error:nil];
    NSString* extension=url.pathExtension.lowercaseString;
    if (directory.boolValue || [extension isEqualToString:@"ply"] || [extension isEqualToString:@"obj"]) return NSDragOperationCopy;
    return NSDragOperationNone;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSURL* url = [NSURL URLFromPasteboard:sender.draggingPasteboard];
    if (!url) return NO;
    NSNumber* directory = nil;
    [url getResourceValue:&directory forKey:NSURLIsDirectoryKey error:nil];
    if (directory.boolValue) { [self.renderer loadRunURL:url]; return YES; }
    NSString* extension=url.pathExtension.lowercaseString;
    if ([extension isEqualToString:@"ply"]) { [self.renderer loadPlyURL:url]; return YES; }
    if ([extension isEqualToString:@"obj"]) { [self.renderer loadObjURL:url]; return YES; }
    return NO;
}
@end

@implementation VulkaxRenderer

- (id<MTLRenderPipelineState>)pipeline:(id<MTLLibrary>)lib view:(MTKView*)view
                                vertex:(NSString*)vs fragment:(NSString*)fs blend:(BOOL)blend {
    MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [lib newFunctionWithName:vs]; d.fragmentFunction = [lib newFunctionWithName:fs];
    d.colorAttachments[0].pixelFormat = view.colorPixelFormat; d.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
    if (blend) {
        MTLRenderPipelineColorAttachmentDescriptor* a = d.colorAttachments[0]; a.blendingEnabled = YES;
        a.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha; a.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        a.sourceAlphaBlendFactor = MTLBlendFactorOne; a.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    NSError* error = nil; id<MTLRenderPipelineState> p = [_device newRenderPipelineStateWithDescriptor:d error:&error];
    if (!p) throw std::runtime_error("Metal pipeline creation failed: " + errorText(error));
    return p;
}

- (instancetype)initWithView:(MTKView*)view scene:(ViewerScene)scene {
    self = [super init]; if (!self) return nil;
    _scene = std::move(scene); _device = view.device; _queue = [_device newCommandQueue];
    _splatScale = 1.0F; _opacity = 0.88F; _exposure = 1.0F; _mode = ViewMode::Hybrid; _state = WorldState::After;
    _highlight = YES; _showGrid = YES; _autoOrbit = NO; _visibilityDirty = YES; _shEnabled = YES;
    _captureRequested = NO; _captureURL = nil;
    _splatBudget = 200000U; _beforeVisible = _scene.before.size(); _afterVisible = _scene.after.size(); _beforeCulled = _afterCulled = 0U;
    _gpuSortMilliseconds = 0.0; _visibilityMilliseconds = 0.0;
    _fpsFrames = 0; _fps = 0; _lastFrame = _fpsEpoch = std::chrono::steady_clock::now();

    NSString* source = [NSString stringWithUTF8String:vulkax::viewer::metal::shaderSource];
    NSError* error = nil; id<MTLLibrary> lib = [_device newLibraryWithSource:source options:nil error:&error];
    if (!lib) throw std::runtime_error("Metal shader compilation failed: " + errorText(error));
    _splatPipeline = [self pipeline:lib view:view vertex:@"splatVertex" fragment:@"splatFragment" blend:YES];
    _surfacePipeline = [self pipeline:lib view:view vertex:@"surfaceVertex" fragment:@"surfaceFragment" blend:NO];
    _linePipeline = [self pipeline:lib view:view vertex:@"lineVertex" fragment:@"lineFragment" blend:YES];

    MTLDepthStencilDescriptor* od = [MTLDepthStencilDescriptor new]; od.depthCompareFunction = MTLCompareFunctionLessEqual; od.depthWriteEnabled = YES;
    _opaqueDepth = [_device newDepthStencilStateWithDescriptor:od];
    MTLDepthStencilDescriptor* td = [MTLDepthStencilDescriptor new]; td.depthCompareFunction = MTLCompareFunctionLessEqual; td.depthWriteEnabled = NO;
    _transparentDepth = [_device newDepthStencilStateWithDescriptor:td];

    _gpuSorter = std::make_unique<vulkax::viewer::MetalGpuSorter>(_device);
    [self uploadScene]; [self resetCamera];
    return self;
}

- (id<MTLBuffer>)buffer:(const void*)bytes length:(NSUInteger)n { return n ? [_device newBufferWithBytes:bytes length:n options:MTLResourceStorageModeShared] : nil; }
- (id<MTLBuffer>)orderBufferCapacity:(std::size_t)count { return count ? [_device newBufferWithLength:count*sizeof(std::uint32_t) options:MTLResourceStorageModeShared] : nil; }

- (std::vector<GPUSplat>)gpuGaussians:(const std::vector<vulkax::viewer::ViewerGaussian>&)source {
    std::vector<GPUSplat> out; out.reserve(source.size());
    for (const auto& g : source) {
        GPUSplat s;
        s.positionScaleX = (simd_float4){(float)g.position.x,(float)g.position.y,(float)g.position.z,g.scale[0]};
        s.scaleYZOpacity = (simd_float4){g.scale[1],g.scale[2],g.opacity,0};
        s.rotation = (simd_float4){g.rotation[1],g.rotation[2],g.rotation[3],g.rotation[0]};
        s.colorMark = (simd_float4){g.color[0],g.color[1],g.color[2],0}; out.push_back(s);
    }
    return out;
}

- (std::vector<GPUAppearance>)gpuAppearances:(const std::vector<vulkax::viewer::ViewerGaussian>&)source {
    std::vector<GPUAppearance> out(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        std::array<float, 48> scalars{};
        scalars[0] = source[i].shDC[0]; scalars[1] = source[i].shDC[1]; scalars[2] = source[i].shDC[2];
        for (std::size_t value = 0; value < source[i].shRest.size(); ++value) scalars[3U + value] = source[i].shRest[value];
        std::memcpy(out[i].packed, scalars.data(), sizeof(scalars));
        out[i].meta = (simd_uint4){source[i].shCoefficientCount,0U,0U,0U};
    }
    return out;
}

- (void)resetSortValidation {
    const bool available = _gpuSorter && _gpuSorter->available();
    _gpuSortSession.reset(available, available ? std::string{} : (_gpuSorter ? _gpuSorter->error() : "Metal GPU sorter missing"));
    _gpuSortMilliseconds = 0.0;
}

- (void)uploadScene {
    const auto before = [self gpuGaussians:_scene.before], after = [self gpuGaussians:_scene.after];
    const auto beforeAppearance = [self gpuAppearances:_scene.before], afterAppearance = [self gpuAppearances:_scene.after];
    _beforeBuffer = [self buffer:before.data() length:before.size()*sizeof(GPUSplat)];
    _afterBuffer = [self buffer:after.data() length:after.size()*sizeof(GPUSplat)];
    _beforeAppearanceBuffer = [self buffer:beforeAppearance.data() length:beforeAppearance.size()*sizeof(GPUAppearance)];
    _afterAppearanceBuffer = [self buffer:afterAppearance.data() length:afterAppearance.size()*sizeof(GPUAppearance)];

    const float pr = (float)std::max(0.0015,_scene.bounds.radius*0.014);
    std::vector<GPUSplat> particles; particles.reserve(_scene.particles.size());
    std::vector<GPUAppearance> particleAppearance(_scene.particles.size());
    for (const auto& p : _scene.particles) {
        GPUSplat s;
        s.positionScaleX=(simd_float4){(float)p.position.x,(float)p.position.y,(float)p.position.z,pr};
        s.scaleYZOpacity=(simd_float4){pr,pr,0.97F,0}; s.rotation=(simd_float4){0,0,0,1};
        s.colorMark=(simd_float4){0.42F,0.70F,1.0F,p.inRewriteRegion?1.0F:0.0F}; particles.push_back(s);
    }
    _particleBuffer=[self buffer:particles.data() length:particles.size()*sizeof(GPUSplat)];
    _particleAppearanceBuffer=[self buffer:particleAppearance.data() length:particleAppearance.size()*sizeof(GPUAppearance)];

    std::vector<GPUSurfaceVertex> surface; surface.reserve(_scene.surfaceTriangles.size());
    for(const auto& v:_scene.surfaceTriangles){GPUSurfaceVertex s;
        s.position=(simd_float4){(float)v.position.x,(float)v.position.y,(float)v.position.z,1};
        s.normal=(simd_float4){(float)v.normal.x,(float)v.normal.y,(float)v.normal.z,0};
        s.color=(simd_float4){v.color[0],v.color[1],v.color[2],1}; surface.push_back(s);}
    _surfaceBuffer=[self buffer:surface.data() length:surface.size()*sizeof(GPUSurfaceVertex)];

    _beforeOrderBuffer=[self orderBufferCapacity:_scene.before.size()]; _afterOrderBuffer=[self orderBufferCapacity:_scene.after.size()];
    _particleOrder.resize(_scene.particles.size()); std::iota(_particleOrder.begin(),_particleOrder.end(),0U);
    _particleOrderBuffer=[self buffer:_particleOrder.data() length:_particleOrder.size()*sizeof(std::uint32_t)];
    _beforeOrder.resize(_scene.before.size()); std::iota(_beforeOrder.begin(),_beforeOrder.end(),0U);
    _afterOrder.resize(_scene.after.size()); std::iota(_afterOrder.begin(),_afterOrder.end(),0U);
    if(_beforeOrderBuffer&&!_beforeOrder.empty())std::memcpy(_beforeOrderBuffer.contents,_beforeOrder.data(),_beforeOrder.size()*sizeof(std::uint32_t));
    if(_afterOrderBuffer&&!_afterOrder.empty())std::memcpy(_afterOrderBuffer.contents,_afterOrder.data(),_afterOrder.size()*sizeof(std::uint32_t));
    _beforeVisible=_beforeOrder.size(); _afterVisible=_afterOrder.size();

    std::vector<simd_float4> grid; grid.reserve(84); const double gy=_scene.bounds.minimum.y-_scene.bounds.radius*0.06, r=_scene.bounds.radius*1.45;
    for(int i=-10;i<=10;++i){const double t=(double)i/10.0*r;
        grid.push_back((simd_float4){(float)(_scene.bounds.center.x-r),(float)gy,(float)(_scene.bounds.center.z+t),1});
        grid.push_back((simd_float4){(float)(_scene.bounds.center.x+r),(float)gy,(float)(_scene.bounds.center.z+t),1});
        grid.push_back((simd_float4){(float)(_scene.bounds.center.x+t),(float)gy,(float)(_scene.bounds.center.z-r),1});
        grid.push_back((simd_float4){(float)(_scene.bounds.center.x+t),(float)gy,(float)(_scene.bounds.center.z+r),1});}
    _gridBuffer=[self buffer:grid.data() length:grid.size()*sizeof(simd_float4)];
    [self resetSortValidation];
    _visibilityDirty=YES; [self updateStats];
}

- (void)setStatsLabel:(NSTextField*)label { _stats=label; [self updateStats]; }
- (void)updateStats {
    if(!_stats)return;
    NSString* sortStatus=nil;
    if(_gpuSortSession.gpuTrusted()) sortStatus=@"Metal GPU";
    else if(_gpuSortSession.validating()) sortStatus=[NSString stringWithFormat:@"GPU validate %lu/%lu",(unsigned long)_gpuSortSession.parityPasses(),(unsigned long)_gpuSortSession.requiredParityPasses()];
    else sortStatus=@"CPU fallback";
    NSString* sortNote=_gpuSortSession.note().empty()?@"":[NSString stringWithFormat:@"\nSort note   %@",ns(_gpuSortSession.note())];
    _stats.stringValue=[NSString stringWithFormat:@"Gaussians   %lu → %lu\nVisible     %lu | %lu\nCulled      %lu | %lu\nBudget      %lu\nParticles   %lu\nRewrite     %lu\nSurface     %@\nMax Δ       %.3e\nSH          %@\nSort        %@\nCull/LOD    %.3f ms\nGPU sort    %.3f ms%@\nFPS         %.1f\n\nDrop .ply/.obj or run folder\n1–5 modes · B/A state\nS SH · P capture\nH highlight · G grid\nSpace orbit · R camera",
        (unsigned long)_scene.before.size(),(unsigned long)_scene.after.size(),(unsigned long)_beforeVisible,(unsigned long)_afterVisible,
        (unsigned long)_beforeCulled,(unsigned long)_afterCulled,(unsigned long)_splatBudget,(unsigned long)_scene.particles.size(),
        (unsigned long)_scene.rewriteParticleCount,ns(_scene.surfaceKind),_scene.maxGaussianDisplacement,_shEnabled?@"view-dependent":@"DC only",sortStatus,
        _visibilityMilliseconds,_gpuSortMilliseconds,sortNote,_fps];
}
- (void)resetCamera { _target=_scene.bounds.center; _yaw=0.68; _pitch=0.30; _distance=std::max(0.12,_scene.bounds.radius*3.2); _visibilityDirty=YES; }
- (void)orbitDX:(double)dx dy:(double)dy { _yaw-=dx*0.006; _pitch=std::clamp(_pitch-dy*0.006,-1.45,1.45); _visibilityDirty=YES; }
- (void)panDX:(double)dx dy:(double)dy {
    const Vec3 eye=cameraPosition(_target,_yaw,_pitch,_distance), f=vulkax::math::normalized(_target-eye);
    Vec3 right=vulkax::math::normalized(vulkax::math::cross(f,Vec3{0,1,0})); if(vulkax::math::length(right)<1e-9)right={1,0,0};
    const Vec3 up=vulkax::math::normalized(vulkax::math::cross(right,f)); const double s=_distance*0.0015;
    _target+=(-dx*s)*right+(dy*s)*up; _visibilityDirty=YES;
}
- (void)zoomDelta:(double)d { _distance*=std::exp(-d*0.035); _distance=std::clamp(_distance,std::max(0.002,_scene.bounds.radius*0.04),std::max(2.0,_scene.bounds.radius*50.0)); _visibilityDirty=YES; }
- (void)handleKey:(NSString*)k {
    if([k isEqualToString:@"1"]){_mode=ViewMode::Hybrid;_visibilityDirty=YES;} else if([k isEqualToString:@"2"]){_mode=ViewMode::Splats;_visibilityDirty=YES;}
    else if([k isEqualToString:@"3"]){_mode=ViewMode::Surface;_visibilityDirty=YES;} else if([k isEqualToString:@"4"]){_mode=ViewMode::Particles;_visibilityDirty=YES;} else if([k isEqualToString:@"5"]){_mode=ViewMode::Compare;_visibilityDirty=YES;}
    else if([k isEqualToString:@"b"]){_state=WorldState::Before;} else if([k isEqualToString:@"a"]){_state=WorldState::After;}
    else if([k isEqualToString:@"h"])_highlight=!_highlight; else if([k isEqualToString:@"g"])_showGrid=!_showGrid;
    else if([k isEqualToString:@"s"]){_shEnabled=!_shEnabled;[self updateStats];}
    else if([k isEqualToString:@"p"])[self capturePng:nil];
    else if([k isEqualToString:@"r"])[self resetCamera]; else if([k isEqualToString:@" "])_autoOrbit=!_autoOrbit;
}
- (IBAction)modeChanged:(NSSegmentedControl*)s{_mode=(ViewMode)s.selectedSegment;_visibilityDirty=YES;}
- (IBAction)stateChanged:(NSSegmentedControl*)s{_state=s.selectedSegment?WorldState::After:WorldState::Before;}
- (IBAction)highlightChanged:(NSButton*)s{_highlight=s.state==NSControlStateValueOn;}
- (IBAction)gridChanged:(NSButton*)s{_showGrid=s.state==NSControlStateValueOn;}
- (IBAction)orbitChanged:(NSButton*)s{_autoOrbit=s.state==NSControlStateValueOn;}
- (IBAction)shChanged:(NSButton*)s{_shEnabled=s.state==NSControlStateValueOn;[self updateStats];}
- (IBAction)splatScaleChanged:(NSSlider*)s{_splatScale=(float)s.doubleValue;}
- (IBAction)opacityChanged:(NSSlider*)s{_opacity=(float)s.doubleValue;}
- (IBAction)exposureChanged:(NSSlider*)s{_exposure=(float)s.doubleValue;}
- (IBAction)budgetChanged:(NSSlider*)s{_splatBudget=(std::size_t)std::max(1000.0,s.doubleValue);_visibilityDirty=YES;}

- (void)loadPlyURL:(NSURL*)url {
    try { _scene=vulkax::viewer::loadStandaloneGaussianScene(std::filesystem::path(url.path.UTF8String)); [self uploadScene]; [self resetCamera]; }
    catch(const std::exception& e){NSAlert* a=[NSAlert new];a.messageText=@"Could not open Gaussian PLY";a.informativeText=ns(e.what());[a runModal];}
}
- (void)loadObjURL:(NSURL*)url {
    try {
        vulkax::viewer::ObjImportSettings settings;
        settings.targetSplats=std::min<std::size_t>(160000U,std::max<std::size_t>(20000U,_splatBudget));
        settings.maxSplats=std::max<std::size_t>(settings.targetSplats,_splatBudget);
        _scene=vulkax::viewer::loadObjAsGaussianScene(std::filesystem::path(url.path.UTF8String),settings);
        [self uploadScene];[self resetCamera];
    } catch(const std::exception& e){NSAlert* a=[NSAlert new];a.messageText=@"Could not import OBJ";a.informativeText=ns(e.what());[a runModal];}
}
- (void)loadRunURL:(NSURL*)url {
    try { _scene=vulkax::viewer::loadCapturedWorldScene(std::filesystem::path(url.path.UTF8String)); [self uploadScene]; [self resetCamera]; }
    catch(const std::exception& e){NSAlert* a=[NSAlert new];a.messageText=@"Could not open Vulkax run";a.informativeText=ns(e.what());[a runModal];}
}
- (IBAction)openPly:(id)sender {
    (void)sender; NSOpenPanel* p=[NSOpenPanel openPanel]; p.canChooseDirectories=NO; p.allowsMultipleSelection=NO; p.allowedFileTypes=@[@"ply"];
    if([p runModal]==NSModalResponseOK&&p.URL)[self loadPlyURL:p.URL];
}
- (IBAction)openObj:(id)sender {
    (void)sender; NSOpenPanel* p=[NSOpenPanel openPanel]; p.canChooseDirectories=NO; p.allowsMultipleSelection=NO; p.allowedFileTypes=@[@"obj"];
    if([p runModal]==NSModalResponseOK&&p.URL)[self loadObjURL:p.URL];
}
- (IBAction)openRun:(id)sender {
    (void)sender; NSOpenPanel* p=[NSOpenPanel openPanel]; p.canChooseDirectories=YES; p.canChooseFiles=NO; p.allowsMultipleSelection=NO;
    if([p runModal]==NSModalResponseOK&&p.URL)[self loadRunURL:p.URL];
}
- (IBAction)capturePng:(id)sender {
    (void)sender; NSSavePanel* p=[NSSavePanel savePanel]; p.nameFieldStringValue=@"vulkax_capture.png";
    if([p runModal]==NSModalResponseOK&&p.URL){_captureURL=p.URL;_captureRequested=YES;}
}

- (void)updateVisibilityForView:(MTKView*)view {
    if(!_visibilityDirty)return;
    const Vec3 eye=cameraPosition(_target,_yaw,_pitch,_distance);
    const Vec3 forward=vulkax::math::normalized(_target-eye);
    const double effectiveWidth = _mode==ViewMode::Compare ? std::max(view.drawableSize.width*0.5,1.0) : std::max(view.drawableSize.width,1.0);
    vulkax::viewer::ViewerCamera camera; camera.position=eye; camera.target=_target; camera.aspect=effectiveWidth/std::max(view.drawableSize.height,1.0);
    camera.verticalFovRadians=M_PI/4.0; camera.nearPlane=std::max(0.0005,_scene.bounds.radius*0.008); camera.farPlane=std::max(10.0,_scene.bounds.radius*60.0+_distance);

    vulkax::viewer::VisibilitySettings retainedSettings; retainedSettings.maxSplats=_splatBudget; retainedSettings.minimumOpacity=0.002; retainedSettings.sortBackToFront=false;
    const auto visibilityStart=std::chrono::steady_clock::now();
    auto beforeRetained=vulkax::viewer::selectVisibleGaussians(_scene.before,camera,retainedSettings);
    auto afterRetained=vulkax::viewer::selectVisibleGaussians(_scene.after,camera,retainedSettings);
    _visibilityMilliseconds=elapsedMilliseconds(visibilityStart);
    _beforeCulled=beforeRetained.opacityRejected+beforeRetained.frustumRejected+beforeRetained.budgetRejected;
    _afterCulled=afterRetained.opacityRejected+afterRetained.frustumRejected+afterRetained.budgetRejected;

    const auto installOrders=[&](std::vector<std::uint32_t> beforeOrder,std::vector<std::uint32_t> afterOrder){
        _beforeOrder=std::move(beforeOrder);_afterOrder=std::move(afterOrder);_beforeVisible=_beforeOrder.size();_afterVisible=_afterOrder.size();
        if(_beforeOrderBuffer&&!_beforeOrder.empty())std::memcpy(_beforeOrderBuffer.contents,_beforeOrder.data(),_beforeOrder.size()*sizeof(std::uint32_t));
        if(_afterOrderBuffer&&!_afterOrder.empty())std::memcpy(_afterOrderBuffer.contents,_afterOrder.data(),_afterOrder.size()*sizeof(std::uint32_t));
    };

    const auto cpuFallback=[&](){
        vulkax::viewer::VisibilitySettings sortedSettings=retainedSettings;sortedSettings.sortBackToFront=true;
        const auto cpuStart=std::chrono::steady_clock::now();
        auto beforeCpu=vulkax::viewer::selectVisibleGaussians(_scene.before,camera,sortedSettings);
        auto afterCpu=vulkax::viewer::selectVisibleGaussians(_scene.after,camera,sortedSettings);
        _visibilityMilliseconds+=elapsedMilliseconds(cpuStart);
        _gpuSortMilliseconds=0.0;
        _beforeCulled=beforeCpu.opacityRejected+beforeCpu.frustumRejected+beforeCpu.budgetRejected;
        _afterCulled=afterCpu.opacityRejected+afterCpu.frustumRejected+afterCpu.budgetRejected;
        installOrders(std::move(beforeCpu.order),std::move(afterCpu.order));
    };

    if(_gpuSortSession.cpuFallback()||!_gpuSorter||!_gpuSorter->available()){
        cpuFallback();
    }else{
        const auto beforeGpu=_gpuSorter->sort(_beforeBuffer,beforeRetained.order,eye,forward);
        const auto afterGpu=_gpuSorter->sort(_afterBuffer,afterRetained.order,eye,forward);
        _gpuSortMilliseconds=(beforeGpu.gpuSeconds+afterGpu.gpuSeconds)*1000.0;
        if(!beforeGpu.success||!afterGpu.success){
            const std::string reason=!beforeGpu.success?beforeGpu.error:afterGpu.error;
            _gpuSortSession.recordRuntimeFailure("Metal GPU sort failed: "+reason);
            cpuFallback();
        }else if(_gpuSortSession.validating()){
            vulkax::viewer::VisibilitySettings sortedSettings=retainedSettings;sortedSettings.sortBackToFront=true;
            const auto referenceStart=std::chrono::steady_clock::now();
            auto beforeCpu=vulkax::viewer::selectVisibleGaussians(_scene.before,camera,sortedSettings);
            auto afterCpu=vulkax::viewer::selectVisibleGaussians(_scene.after,camera,sortedSettings);
            _visibilityMilliseconds+=elapsedMilliseconds(referenceStart);
            const auto beforeReference=referenceDepthKeys(_scene.before,beforeCpu.order,eye,forward);
            const auto afterReference=referenceDepthKeys(_scene.after,afterCpu.order,eye,forward);
            const auto beforeValidation=vulkax::viewer::validateGpuDepthKeys(beforeGpu.depthKeys,beforeReference,2.0e-4);
            const auto afterValidation=vulkax::viewer::validateGpuDepthKeys(afterGpu.depthKeys,afterReference,2.0e-4);
            const bool exactOrder=beforeGpu.order==beforeCpu.order&&afterGpu.order==afterCpu.order;
            const bool parity=beforeValidation.valid()&&afterValidation.valid()&&exactOrder;
            _gpuSortSession.recordParity(parity,parity?std::string{}:"Metal GPU sort parity mismatch; using deterministic CPU ordering");
            if(_gpuSortSession.gpuTrusted())installOrders(beforeGpu.order,afterGpu.order);
            else installOrders(std::move(beforeCpu.order),std::move(afterCpu.order));
        }else{
            installOrders(beforeGpu.order,afterGpu.order);
        }
    }

    _visibilityDirty=NO; [self updateStats];
}

- (GPUUniforms)uniformsWidth:(double)width height:(double)height particles:(BOOL)particles {
    const Vec3 eye=cameraPosition(_target,_yaw,_pitch,_distance);
    const Vec3 back=vulkax::math::normalized(eye-_target);
    Vec3 right=vulkax::math::normalized(vulkax::math::cross(Vec3{0,1,0},back)); if(vulkax::math::length(right)<1e-9)right={1,0,0};
    const Vec3 up=vulkax::math::normalized(vulkax::math::cross(back,right));
    const float aspect=(float)(std::max(width,1.0)/std::max(height,1.0));
    const float nearZ=(float)std::max(0.0005,_scene.bounds.radius*0.008), farZ=(float)std::max(10.0,_scene.bounds.radius*60.0+_distance);
    const simd_float4x4 projection=perspective(45.0F*(float)M_PI/180.0F,aspect,nearZ,farZ);
    GPUUniforms u;
    u.viewProjection=simd_mul(projection,lookAt(eye,_target));
    u.viewportAndScale=(simd_float4){(float)width,(float)height,particles?0.72F:_splatScale,particles?34.0F:128.0F};
    u.renderParams=(simd_float4){_opacity,_exposure,_highlight?1.0F:0.0F,particles?1.0F:0.0F};
    u.cameraPositionAndFlags=(simd_float4){(float)eye.x,(float)eye.y,(float)eye.z,_shEnabled?1.0F:0.0F};
    u.cameraRightAndFocalX=(simd_float4){(float)right.x,(float)right.y,(float)right.z,projection.columns[0].x};
    u.cameraUpAndFocalY=(simd_float4){(float)up.x,(float)up.y,(float)up.z,projection.columns[1].y};
    u.cameraBackAndMinSigma=(simd_float4){(float)back.x,(float)back.y,(float)back.z,0.75F};
    return u;
}

- (void)setRegion:(id<MTLRenderCommandEncoder>)encoder x:(NSUInteger)x width:(NSUInteger)width height:(NSUInteger)height {
    [encoder setViewport:viewport((double)x,0.0,(double)width,(double)height)];
    [encoder setScissorRect:scissor(x,0,width,height)];
}

- (void)drawSplats:(id<MTLRenderCommandEncoder>)e width:(double)width height:(double)height
            buffer:(id<MTLBuffer>)b appearance:(id<MTLBuffer>)appearance order:(id<MTLBuffer>)o
             count:(NSUInteger)n particles:(BOOL)p {
    if(!b||!appearance||!o||!n)return; GPUUniforms u=[self uniformsWidth:width height:height particles:p];
    [e setRenderPipelineState:_splatPipeline]; [e setDepthStencilState:_transparentDepth];
    [e setVertexBuffer:b offset:0 atIndex:0];[e setVertexBuffer:o offset:0 atIndex:1];[e setVertexBytes:&u length:sizeof(u) atIndex:2];[e setVertexBuffer:appearance offset:0 atIndex:3];[e setFragmentBytes:&u length:sizeof(u) atIndex:2];
    [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n];
}
- (void)drawGrid:(id<MTLRenderCommandEncoder>)e width:(double)width height:(double)height {
    if(!_showGrid||!_gridBuffer)return; GPUUniforms u=[self uniformsWidth:width height:height particles:NO];[e setRenderPipelineState:_linePipeline];[e setDepthStencilState:_transparentDepth];[e setVertexBuffer:_gridBuffer offset:0 atIndex:0];[e setVertexBytes:&u length:sizeof(u) atIndex:2];[e drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:84];
}
- (void)drawSurface:(id<MTLRenderCommandEncoder>)e width:(double)width height:(double)height {
    if(!_surfaceBuffer||_scene.surfaceTriangles.empty())return; GPUUniforms u=[self uniformsWidth:width height:height particles:NO];[e setRenderPipelineState:_surfacePipeline];[e setDepthStencilState:_opaqueDepth];[e setVertexBuffer:_surfaceBuffer offset:0 atIndex:0];[e setVertexBytes:&u length:sizeof(u) atIndex:2];[e setFragmentBytes:&u length:sizeof(u) atIndex:2];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:_scene.surfaceTriangles.size()];
}

- (void)writeCaptureBuffer:(id<MTLBuffer>)buffer width:(NSUInteger)width height:(NSUInteger)height bytesPerRow:(NSUInteger)bytesPerRow url:(NSURL*)url {
    if(!buffer||!url||width==0||height==0)return;
    NSBitmapImageRep* rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)width pixelsHigh:(NSInteger)height bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:(NSInteger)(width*4U) bitsPerPixel:32];
    if(!rep)return;
    auto* source=(const unsigned char*)buffer.contents; auto* destination=rep.bitmapData;
    for(NSUInteger y=0;y<height;++y){const unsigned char* src=source+y*bytesPerRow;unsigned char* dst=destination+y*width*4U;for(NSUInteger x=0;x<width;++x){dst[4*x+0]=src[4*x+2];dst[4*x+1]=src[4*x+1];dst[4*x+2]=src[4*x+0];dst[4*x+3]=src[4*x+3];}}
    NSData* png=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if(!png||![png writeToURL:url atomically:YES]){NSAlert* alert=[NSAlert new];alert.messageText=@"Could not save Vulkax capture";alert.informativeText=url.path;[alert runModal];}
}

- (void)drawInMTKView:(MTKView*)view {
    const auto now=std::chrono::steady_clock::now(); const double dt=std::chrono::duration<double>(now-_lastFrame).count(); _lastFrame=now;
    if(_autoOrbit){_yaw+=dt*0.28;_visibilityDirty=YES;} [self updateVisibilityForView:view];
    if(++_fpsFrames>=20){const double elapsed=std::chrono::duration<double>(now-_fpsEpoch).count(); if(elapsed>0){_fps=_fpsFrames/elapsed;_fpsFrames=0;_fpsEpoch=now;[self updateStats];}}
    id<CAMetalDrawable> drawable=view.currentDrawable; MTLRenderPassDescriptor* pass=view.currentRenderPassDescriptor; if(!drawable||!pass)return;
    pass.colorAttachments[0].clearColor=MTLClearColorMake(0.018,0.028,0.060,1); pass.depthAttachment.clearDepth=1.0;
    id<MTLCommandBuffer> cmd=[_queue commandBuffer]; id<MTLRenderCommandEncoder> e=[cmd renderCommandEncoderWithDescriptor:pass];
    const NSUInteger width=(NSUInteger)std::max(view.drawableSize.width,1.0), height=(NSUInteger)std::max(view.drawableSize.height,1.0);

    if(_mode==ViewMode::Compare){
        const NSUInteger leftWidth=std::max<NSUInteger>(1U,width/2U), rightWidth=std::max<NSUInteger>(1U,width-leftWidth);
        [self setRegion:e x:0U width:leftWidth height:height]; [self drawGrid:e width:leftWidth height:height]; [self drawSurface:e width:leftWidth height:height];
        [self drawSplats:e width:leftWidth height:height buffer:_beforeBuffer appearance:_beforeAppearanceBuffer order:_beforeOrderBuffer count:_beforeVisible particles:NO];
        [self setRegion:e x:leftWidth width:rightWidth height:height]; [self drawGrid:e width:rightWidth height:height]; [self drawSurface:e width:rightWidth height:height];
        [self drawSplats:e width:rightWidth height:height buffer:_afterBuffer appearance:_afterAppearanceBuffer order:_afterOrderBuffer count:_afterVisible particles:NO];
    } else {
        [self setRegion:e x:0U width:width height:height]; [self drawGrid:e width:width height:height];
        if(_mode==ViewMode::Hybrid||_mode==ViewMode::Surface)[self drawSurface:e width:width height:height];
        if(_mode==ViewMode::Hybrid||_mode==ViewMode::Splats){const BOOL before=_state==WorldState::Before;[self drawSplats:e width:width height:height buffer:before?_beforeBuffer:_afterBuffer appearance:before?_beforeAppearanceBuffer:_afterAppearanceBuffer order:before?_beforeOrderBuffer:_afterOrderBuffer count:before?_beforeVisible:_afterVisible particles:NO];}
        if(_mode==ViewMode::Hybrid||_mode==ViewMode::Particles)[self drawSplats:e width:width height:height buffer:_particleBuffer appearance:_particleAppearanceBuffer order:_particleOrderBuffer count:_scene.particles.size() particles:YES];
    }
    [e endEncoding];

    id<MTLBuffer> captureBuffer=nil; NSUInteger captureBytesPerRow=0U; NSURL* captureURL=nil;
    if(_captureRequested&&_captureURL){captureURL=_captureURL;captureBytesPerRow=((width*4U+255U)/256U)*256U;captureBuffer=[_device newBufferWithLength:captureBytesPerRow*height options:MTLResourceStorageModeShared];id<MTLBlitCommandEncoder> blit=[cmd blitCommandEncoder];MTLOrigin origin={0,0,0};MTLSize size={width,height,1};[blit copyFromTexture:drawable.texture sourceSlice:0 sourceLevel:0 sourceOrigin:origin sourceSize:size toBuffer:captureBuffer destinationOffset:0 destinationBytesPerRow:captureBytesPerRow destinationBytesPerImage:captureBytesPerRow*height];[blit endEncoding];}

    [cmd presentDrawable:drawable];[cmd commit];
    if(captureBuffer&&captureURL){[cmd waitUntilCompleted];[self writeCaptureBuffer:captureBuffer width:width height:height bytesPerRow:captureBytesPerRow url:captureURL];_captureRequested=NO;_captureURL=nil;}
}
- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size{(void)view;(void)size;_visibilityDirty=YES;}
@end

static NSTextField* label(NSString* text,CGFloat size,NSFontWeight weight){NSTextField* f=[NSTextField labelWithString:text];f.font=[NSFont systemFontOfSize:size weight:weight];f.textColor=[NSColor colorWithCalibratedWhite:0.92 alpha:1];return f;}
static NSView* sliderRow(NSString* title,double lo,double hi,double value,id target,SEL action){NSStackView* s=[NSStackView stackViewWithViews:@[]];s.orientation=NSUserInterfaceLayoutOrientationVertical;s.spacing=4;NSTextField* c=label(title,11,NSFontWeightMedium);c.textColor=[NSColor colorWithCalibratedRed:.64 green:.73 blue:.86 alpha:1];NSSlider* slider=[NSSlider sliderWithValue:value minValue:lo maxValue:hi target:target action:action];slider.continuous=YES;[s addArrangedSubview:c];[s addArrangedSubview:slider];return s;}
static NSVisualEffectView* inspector(VulkaxRenderer* r,NSTextField** statsOut){NSVisualEffectView* p=[NSVisualEffectView new];p.material=NSVisualEffectMaterialSidebar;p.blendingMode=NSVisualEffectBlendingModeBehindWindow;p.state=NSVisualEffectStateActive;NSStackView* s=[NSStackView stackViewWithViews:@[]];s.translatesAutoresizingMaskIntoConstraints=NO;s.orientation=NSUserInterfaceLayoutOrientationVertical;s.alignment=NSLayoutAttributeLeading;s.spacing=10;s.edgeInsets=NSEdgeInsetsMake(20,18,18,18);[p addSubview:s];[NSLayoutConstraint activateConstraints:@[[s.leadingAnchor constraintEqualToAnchor:p.leadingAnchor],[s.trailingAnchor constraintEqualToAnchor:p.trailingAnchor],[s.topAnchor constraintEqualToAnchor:p.topAnchor]]];NSTextField* brand=label(@"VULKAX NATIVE VIEWER",11,NSFontWeightSemibold);brand.textColor=[NSColor colorWithCalibratedRed:.52 green:.70 blue:1 alpha:1];[s addArrangedSubview:brand];[s addArrangedSubview:label(@"Interactive Gaussian World",20,NSFontWeightBold)];NSSegmentedControl* m=[NSSegmentedControl segmentedControlWithLabels:@[@"Hybrid",@"Splats",@"Surface",@"Particles",@"Compare"] trackingMode:NSSegmentSwitchTrackingSelectOne target:r action:@selector(modeChanged:)];m.selectedSegment=0;[s addArrangedSubview:m];NSSegmentedControl* st=[NSSegmentedControl segmentedControlWithLabels:@[@"Before",@"Verified After"] trackingMode:NSSegmentSwitchTrackingSelectOne target:r action:@selector(stateChanged:)];st.selectedSegment=1;[s addArrangedSubview:st];NSButton* h=[NSButton checkboxWithTitle:@"Rewrite highlight" target:r action:@selector(highlightChanged:)];h.state=NSControlStateValueOn;NSButton* g=[NSButton checkboxWithTitle:@"Ground grid" target:r action:@selector(gridChanged:)];g.state=NSControlStateValueOn;NSButton* o=[NSButton checkboxWithTitle:@"Auto orbit" target:r action:@selector(orbitChanged:)];NSButton* sh=[NSButton checkboxWithTitle:@"View-dependent SH" target:r action:@selector(shChanged:)];sh.state=NSControlStateValueOn;[s addArrangedSubview:h];[s addArrangedSubview:g];[s addArrangedSubview:o];[s addArrangedSubview:sh];[s addArrangedSubview:sliderRow(@"Splat scale",.2,4,1,r,@selector(splatScaleChanged:))];[s addArrangedSubview:sliderRow(@"Opacity",.05,1,.88,r,@selector(opacityChanged:))];[s addArrangedSubview:sliderRow(@"Exposure",.35,2.4,1,r,@selector(exposureChanged:))];[s addArrangedSubview:sliderRow(@"Splat budget",10000,500000,200000,r,@selector(budgetChanged:))];[s addArrangedSubview:[NSButton buttonWithTitle:@"Open Vulkax Run…" target:r action:@selector(openRun:)]];[s addArrangedSubview:[NSButton buttonWithTitle:@"Open Gaussian PLY…" target:r action:@selector(openPly:)]];[s addArrangedSubview:[NSButton buttonWithTitle:@"Open OBJ Mesh…" target:r action:@selector(openObj:)]];[s addArrangedSubview:[NSButton buttonWithTitle:@"Capture PNG…" target:r action:@selector(capturePng:)]];NSTextField* stats=label(@"",11,NSFontWeightRegular);stats.textColor=[NSColor colorWithCalibratedRed:.68 green:.77 blue:.90 alpha:1];stats.maximumNumberOfLines=0;[s addArrangedSubview:stats];*statsOut=stats;return p;}

@interface VulkaxAppDelegate : NSObject <NSApplicationDelegate> { @private ViewerScene _initialScene; }
@property(nonatomic,strong) NSWindow* window;
@property(nonatomic,strong) VulkaxRenderer* renderer;
- (instancetype)initWithScene:(ViewerScene)scene;
@end
@implementation VulkaxAppDelegate
- (instancetype)initWithScene:(ViewerScene)scene{self=[super init];if(self)_initialScene=std::move(scene);return self;}
- (void)applicationDidFinishLaunching:(NSNotification*)note{(void)note;id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!device){[NSApp terminate:nil];return;}self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1380,850) styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable) backing:NSBackingStoreBuffered defer:NO];self.window.title=@"Vulkax — Native Gaussian World Viewer";self.window.minSize=NSMakeSize(980,650);[self.window center];NSView* root=[NSView new];self.window.contentView=root;VulkaxMetalView* view=[[VulkaxMetalView alloc] initWithFrame:NSZeroRect device:device];view.translatesAutoresizingMaskIntoConstraints=NO;view.colorPixelFormat=MTLPixelFormatBGRA8Unorm_sRGB;view.depthStencilPixelFormat=MTLPixelFormatDepth32Float;view.framebufferOnly=NO;view.preferredFramesPerSecond=60;view.paused=NO;view.enableSetNeedsDisplay=NO;try{self.renderer=[[VulkaxRenderer alloc] initWithView:view scene:std::move(_initialScene)];}catch(const std::exception& e){NSAlert* a=[NSAlert new];a.messageText=@"Vulkax renderer initialization failed";a.informativeText=ns(e.what());[a runModal];[NSApp terminate:nil];return;}view.delegate=self.renderer;view.renderer=self.renderer;NSTextField* stats=nil;NSVisualEffectView* panel=inspector(self.renderer,&stats);panel.translatesAutoresizingMaskIntoConstraints=NO;[self.renderer setStatsLabel:stats];[root addSubview:view];[root addSubview:panel];[NSLayoutConstraint activateConstraints:@[[view.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],[view.topAnchor constraintEqualToAnchor:root.topAnchor],[view.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],[view.trailingAnchor constraintEqualToAnchor:panel.leadingAnchor],[panel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],[panel.topAnchor constraintEqualToAnchor:root.topAnchor],[panel.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],[panel.widthAnchor constraintEqualToConstant:330]]];[self.window makeKeyAndOrderFront:nil];[self.window makeFirstResponder:view];[NSApp activateIgnoringOtherApps:YES];}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender{(void)sender;return YES;}
@end

static void menu(){NSMenu* bar=[NSMenu new];NSMenuItem* root=[NSMenuItem new];[bar addItem:root];NSApp.mainMenu=bar;NSMenu* m=[NSMenu new];[m addItemWithTitle:@"About Vulkax Viewer" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];[m addItem:[NSMenuItem separatorItem]];[m addItemWithTitle:@"Quit Vulkax Viewer" action:@selector(terminate:) keyEquivalent:@"q"];root.submenu=m;}
struct Args{std::filesystem::path run{"build/captured-world-run"},particles{},ply{},obj{};};
static Args args(int argc,const char* argv[]){Args a;for(int i=1;i<argc;++i){std::string v=argv[i];auto next=[&](const char* f){if(i+1>=argc)throw std::runtime_error(std::string(f)+" requires a path");return std::string(argv[++i]);};if(v=="--run")a.run=next("--run");else if(v=="--particles")a.particles=next("--particles");else if(v=="--ply")a.ply=next("--ply");else if(v=="--obj")a.obj=next("--obj");else if(v=="--help"||v=="-h"){std::cout<<"Usage: vulkax_viewer [--run captured-world-run] [--particles particles.csv] [--ply gaussians.ply] [--obj model.obj]\nControls: 1-5 modes, B/A state, S SH, P capture, H highlight, G grid, Space orbit, R reset.\nGPU ordering is promoted automatically after three CPU-parity passes and falls back safely on any mismatch.\n";std::exit(0);}else if(!v.starts_with('-'))a.run=v;else throw std::runtime_error("unknown argument: "+v);}return a;}
int main(int argc,const char* argv[]){@autoreleasepool{try{const auto a=args(argc,argv);ViewerScene scene;if(!a.obj.empty())scene=vulkax::viewer::loadObjAsGaussianScene(a.obj);else if(!a.ply.empty())scene=vulkax::viewer::loadStandaloneGaussianScene(a.ply);else scene=vulkax::viewer::loadCapturedWorldScene(a.run,a.particles);[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];menu();VulkaxAppDelegate* d=[[VulkaxAppDelegate alloc] initWithScene:std::move(scene)];NSApp.delegate=d;[NSApp run];return 0;}catch(const std::exception& e){std::cerr<<"vulkax_viewer: "<<e.what()<<'\n';return 1;}}}
