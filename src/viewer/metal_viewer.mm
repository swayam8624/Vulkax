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

struct GPUSplat { simd_float4 positionScaleX{}, scaleYZOpacity{}, rotation{}, colorMark{}; };
struct GPUSurfaceVertex { simd_float4 position{}, normal{}, color{}; };
struct GPUUniforms { simd_float4x4 viewProjection{}; simd_float4 viewportAndScale{}, renderParams{}; };

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
    id<MTLBuffer> _beforeOrderBuffer, _afterOrderBuffer, _particleOrderBuffer;
    std::vector<std::uint32_t> _beforeOrder, _afterOrder, _particleOrder;
    Vec3 _target;
    double _yaw, _pitch, _distance;
    float _splatScale, _opacity, _exposure;
    ViewMode _mode;
    WorldState _state;
    BOOL _highlight, _showGrid, _autoOrbit, _sortDirty;
    std::chrono::steady_clock::time_point _lastFrame, _fpsEpoch;
    unsigned _fpsFrames;
    double _fps;
    __weak NSTextField* _stats;
}
- (instancetype)initWithView:(MTKView*)view scene:(ViewerScene)scene;
- (void)setStatsLabel:(NSTextField*)label;
- (void)orbitDX:(double)dx dy:(double)dy;
- (void)panDX:(double)dx dy:(double)dy;
- (void)zoomDelta:(double)delta;
- (void)handleKey:(NSString*)key;
- (void)loadPlyURL:(NSURL*)url;
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
    return [url.pathExtension.lowercaseString isEqualToString:@"ply"] ? NSDragOperationCopy : NSDragOperationNone;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSURL* url = [NSURL URLFromPasteboard:sender.draggingPasteboard];
    if (!url || ![url.pathExtension.lowercaseString isEqualToString:@"ply"]) return NO;
    [self.renderer loadPlyURL:url];
    return YES;
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
    _highlight = YES; _showGrid = YES; _autoOrbit = NO; _sortDirty = YES; _fpsFrames = 0; _fps = 0;
    _lastFrame = _fpsEpoch = std::chrono::steady_clock::now();

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
    [self uploadScene]; [self resetCamera];
    return self;
}

- (id<MTLBuffer>)buffer:(const void*)bytes length:(NSUInteger)n { return n ? [_device newBufferWithBytes:bytes length:n options:MTLResourceStorageModeShared] : nil; }

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

- (void)uploadScene {
    const auto before = [self gpuGaussians:_scene.before], after = [self gpuGaussians:_scene.after];
    _beforeBuffer = [self buffer:before.data() length:before.size()*sizeof(GPUSplat)];
    _afterBuffer = [self buffer:after.data() length:after.size()*sizeof(GPUSplat)];

    const float pr = (float)std::max(0.0015,_scene.bounds.radius*0.014);
    std::vector<GPUSplat> particles; particles.reserve(_scene.particles.size());
    for (const auto& p : _scene.particles) {
        GPUSplat s;
        s.positionScaleX=(simd_float4){(float)p.position.x,(float)p.position.y,(float)p.position.z,pr};
        s.scaleYZOpacity=(simd_float4){pr,pr,0.97F,0}; s.rotation=(simd_float4){0,0,0,1};
        s.colorMark=(simd_float4){0.42F,0.70F,1.0F,p.inRewriteRegion?1.0F:0.0F}; particles.push_back(s);
    }
    _particleBuffer=[self buffer:particles.data() length:particles.size()*sizeof(GPUSplat)];

    std::vector<GPUSurfaceVertex> surface; surface.reserve(_scene.surfaceTriangles.size());
    for(const auto& v:_scene.surfaceTriangles){GPUSurfaceVertex s;
        s.position=(simd_float4){(float)v.position.x,(float)v.position.y,(float)v.position.z,1};
        s.normal=(simd_float4){(float)v.normal.x,(float)v.normal.y,(float)v.normal.z,0};
        s.color=(simd_float4){v.color[0],v.color[1],v.color[2],1}; surface.push_back(s);}
    _surfaceBuffer=[self buffer:surface.data() length:surface.size()*sizeof(GPUSurfaceVertex)];

    _beforeOrder.resize(_scene.before.size()); _afterOrder.resize(_scene.after.size()); _particleOrder.resize(_scene.particles.size());
    std::iota(_beforeOrder.begin(),_beforeOrder.end(),0U); std::iota(_afterOrder.begin(),_afterOrder.end(),0U); std::iota(_particleOrder.begin(),_particleOrder.end(),0U);
    _beforeOrderBuffer=[self buffer:_beforeOrder.data() length:_beforeOrder.size()*sizeof(std::uint32_t)];
    _afterOrderBuffer=[self buffer:_afterOrder.data() length:_afterOrder.size()*sizeof(std::uint32_t)];
    _particleOrderBuffer=[self buffer:_particleOrder.data() length:_particleOrder.size()*sizeof(std::uint32_t)];

    std::vector<simd_float4> grid; grid.reserve(84); const double gy=_scene.bounds.minimum.y-_scene.bounds.radius*0.06, r=_scene.bounds.radius*1.45;
    for(int i=-10;i<=10;++i){const double t=(double)i/10.0*r;
        grid.push_back((simd_float4){(float)(_scene.bounds.center.x-r),(float)gy,(float)(_scene.bounds.center.z+t),1});
        grid.push_back((simd_float4){(float)(_scene.bounds.center.x+r),(float)gy,(float)(_scene.bounds.center.z+t),1});
        grid.push_back((simd_float4){(float)(_scene.bounds.center.x+t),(float)gy,(float)(_scene.bounds.center.z-r),1});
        grid.push_back((simd_float4){(float)(_scene.bounds.center.x+t),(float)gy,(float)(_scene.bounds.center.z+r),1});}
    _gridBuffer=[self buffer:grid.data() length:grid.size()*sizeof(simd_float4)]; _sortDirty=YES; [self updateStats];
}

- (void)setStatsLabel:(NSTextField*)label { _stats=label; [self updateStats]; }
- (void)updateStats {
    if(!_stats)return;
    _stats.stringValue=[NSString stringWithFormat:@"Gaussians   %lu → %lu\nParticles   %lu\nRewrite     %lu\nSurface     %@\nMax Δ       %.3e\nFPS         %.1f\n\nDrag .ply onto viewport\n1–4 modes · B/A state\nH highlight · G grid\nSpace orbit · R camera",
        (unsigned long)_scene.before.size(),(unsigned long)_scene.after.size(),(unsigned long)_scene.particles.size(),(unsigned long)_scene.rewriteParticleCount,ns(_scene.surfaceKind),_scene.maxGaussianDisplacement,_fps];
}
- (void)resetCamera { _target=_scene.bounds.center; _yaw=0.68; _pitch=0.30; _distance=std::max(0.12,_scene.bounds.radius*3.2); _sortDirty=YES; }
- (void)orbitDX:(double)dx dy:(double)dy { _yaw-=dx*0.006; _pitch=std::clamp(_pitch-dy*0.006,-1.45,1.45); _sortDirty=YES; }
- (void)panDX:(double)dx dy:(double)dy {
    const Vec3 eye=cameraPosition(_target,_yaw,_pitch,_distance), f=vulkax::math::normalized(_target-eye);
    Vec3 right=vulkax::math::normalized(vulkax::math::cross(f,Vec3{0,1,0})); if(vulkax::math::length(right)<1e-9)right={1,0,0};
    const Vec3 up=vulkax::math::normalized(vulkax::math::cross(right,f)); const double s=_distance*0.0015;
    _target+=(-dx*s)*right+(dy*s)*up; _sortDirty=YES;
}
- (void)zoomDelta:(double)d { _distance*=std::exp(-d*0.035); _distance=std::clamp(_distance,std::max(0.002,_scene.bounds.radius*0.04),std::max(2.0,_scene.bounds.radius*50.0)); _sortDirty=YES; }
- (void)handleKey:(NSString*)k {
    if([k isEqualToString:@"1"])_mode=ViewMode::Hybrid; else if([k isEqualToString:@"2"])_mode=ViewMode::Splats;
    else if([k isEqualToString:@"3"])_mode=ViewMode::Surface; else if([k isEqualToString:@"4"])_mode=ViewMode::Particles;
    else if([k isEqualToString:@"b"]){_state=WorldState::Before;_sortDirty=YES;} else if([k isEqualToString:@"a"]){_state=WorldState::After;_sortDirty=YES;}
    else if([k isEqualToString:@"h"])_highlight=!_highlight; else if([k isEqualToString:@"g"])_showGrid=!_showGrid;
    else if([k isEqualToString:@"r"])[self resetCamera]; else if([k isEqualToString:@" "])_autoOrbit=!_autoOrbit;
}
- (IBAction)modeChanged:(NSSegmentedControl*)s{_mode=(ViewMode)s.selectedSegment;}
- (IBAction)stateChanged:(NSSegmentedControl*)s{_state=s.selectedSegment?WorldState::After:WorldState::Before;_sortDirty=YES;}
- (IBAction)highlightChanged:(NSButton*)s{_highlight=s.state==NSControlStateValueOn;}
- (IBAction)gridChanged:(NSButton*)s{_showGrid=s.state==NSControlStateValueOn;}
- (IBAction)orbitChanged:(NSButton*)s{_autoOrbit=s.state==NSControlStateValueOn;}
- (IBAction)splatScaleChanged:(NSSlider*)s{_splatScale=(float)s.doubleValue;}
- (IBAction)opacityChanged:(NSSlider*)s{_opacity=(float)s.doubleValue;}
- (IBAction)exposureChanged:(NSSlider*)s{_exposure=(float)s.doubleValue;}

- (void)loadPlyURL:(NSURL*)url {
    try { _scene=vulkax::viewer::loadStandaloneGaussianScene(std::filesystem::path(url.path.UTF8String)); [self uploadScene]; [self resetCamera]; }
    catch(const std::exception& e){NSAlert* a=[NSAlert new];a.messageText=@"Could not open Gaussian PLY";a.informativeText=ns(e.what());[a runModal];}
}
- (IBAction)openPly:(id)sender {
    (void)sender; NSOpenPanel* p=[NSOpenPanel openPanel]; p.canChooseDirectories=NO; p.allowsMultipleSelection=NO; p.allowedFileTypes=@[@"ply"];
    if([p runModal]==NSModalResponseOK&&p.URL)[self loadPlyURL:p.URL];
}

- (void)sortSource:(const std::vector<vulkax::viewer::ViewerGaussian>&)source order:(std::vector<std::uint32_t>&)order buffer:(id<MTLBuffer>)buffer eye:(Vec3)eye forward:(Vec3)f {
    if(!buffer||source.size()!=order.size())return; std::sort(order.begin(),order.end(),[&](std::uint32_t a,std::uint32_t b){return vulkax::math::dot(source[a].position-eye,f)>vulkax::math::dot(source[b].position-eye,f);});
    std::memcpy(buffer.contents,order.data(),order.size()*sizeof(std::uint32_t));
}
- (void)updateSort {
    if(!_sortDirty)return; const Vec3 eye=cameraPosition(_target,_yaw,_pitch,_distance), f=vulkax::math::normalized(_target-eye);
    [self sortSource:_scene.before order:_beforeOrder buffer:_beforeOrderBuffer eye:eye forward:f]; [self sortSource:_scene.after order:_afterOrder buffer:_afterOrderBuffer eye:eye forward:f]; _sortDirty=NO;
}
- (GPUUniforms)uniforms:(MTKView*)view particles:(BOOL)particles {
    const Vec3 eye=cameraPosition(_target,_yaw,_pitch,_distance); const float aspect=(float)(view.drawableSize.width/std::max(view.drawableSize.height,1.0));
    const float nearZ=(float)std::max(0.0005,_scene.bounds.radius*0.008), farZ=(float)std::max(10.0,_scene.bounds.radius*60.0+_distance);
    GPUUniforms u; u.viewProjection=simd_mul(perspective(45.0F*(float)M_PI/180.0F,aspect,nearZ,farZ),lookAt(eye,_target));
    u.viewportAndScale=(simd_float4){(float)view.drawableSize.width,(float)view.drawableSize.height,particles?0.72F:_splatScale,particles?34.0F:128.0F};
    u.renderParams=(simd_float4){_opacity,_exposure,_highlight?1.0F:0.0F,particles?1.0F:0.0F}; return u;
}
- (void)drawSplats:(id<MTLRenderCommandEncoder>)e view:(MTKView*)v buffer:(id<MTLBuffer>)b order:(id<MTLBuffer>)o count:(NSUInteger)n particles:(BOOL)p {
    if(!b||!o||!n)return; GPUUniforms u=[self uniforms:v particles:p]; [e setRenderPipelineState:_splatPipeline]; [e setDepthStencilState:_transparentDepth];
    [e setVertexBuffer:b offset:0 atIndex:0];[e setVertexBuffer:o offset:0 atIndex:1];[e setVertexBytes:&u length:sizeof(u) atIndex:2];[e setFragmentBytes:&u length:sizeof(u) atIndex:2];
    [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n];
}
- (void)drawInMTKView:(MTKView*)view {
    const auto now=std::chrono::steady_clock::now(); const double dt=std::chrono::duration<double>(now-_lastFrame).count(); _lastFrame=now;
    if(_autoOrbit){_yaw+=dt*0.28;_sortDirty=YES;} [self updateSort];
    if(++_fpsFrames>=20){const double elapsed=std::chrono::duration<double>(now-_fpsEpoch).count(); if(elapsed>0){_fps=_fpsFrames/elapsed;_fpsFrames=0;_fpsEpoch=now;[self updateStats];}}
    id<CAMetalDrawable> drawable=view.currentDrawable; MTLRenderPassDescriptor* pass=view.currentRenderPassDescriptor; if(!drawable||!pass)return;
    pass.colorAttachments[0].clearColor=MTLClearColorMake(0.018,0.028,0.060,1); pass.depthAttachment.clearDepth=1.0;
    id<MTLCommandBuffer> cmd=[_queue commandBuffer]; id<MTLRenderCommandEncoder> e=[cmd renderCommandEncoderWithDescriptor:pass];
    if(_showGrid&&_gridBuffer){GPUUniforms u=[self uniforms:view particles:NO];[e setRenderPipelineState:_linePipeline];[e setDepthStencilState:_transparentDepth];[e setVertexBuffer:_gridBuffer offset:0 atIndex:0];[e setVertexBytes:&u length:sizeof(u) atIndex:2];[e drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:84];}
    if((_mode==ViewMode::Hybrid||_mode==ViewMode::Surface)&&_surfaceBuffer&&!_scene.surfaceTriangles.empty()){GPUUniforms u=[self uniforms:view particles:NO];[e setRenderPipelineState:_surfacePipeline];[e setDepthStencilState:_opaqueDepth];[e setVertexBuffer:_surfaceBuffer offset:0 atIndex:0];[e setVertexBytes:&u length:sizeof(u) atIndex:2];[e setFragmentBytes:&u length:sizeof(u) atIndex:2];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:_scene.surfaceTriangles.size()];}
    if(_mode==ViewMode::Hybrid||_mode==ViewMode::Splats){const BOOL before=_state==WorldState::Before;[self drawSplats:e view:view buffer:before?_beforeBuffer:_afterBuffer order:before?_beforeOrderBuffer:_afterOrderBuffer count:before?_scene.before.size():_scene.after.size() particles:NO];}
    if(_mode==ViewMode::Hybrid||_mode==ViewMode::Particles)[self drawSplats:e view:view buffer:_particleBuffer order:_particleOrderBuffer count:_scene.particles.size() particles:YES];
    [e endEncoding];[cmd presentDrawable:drawable];[cmd commit];
}
- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size{(void)view;(void)size;_sortDirty=YES;}
@end

static NSTextField* label(NSString* text,CGFloat size,NSFontWeight weight){NSTextField* f=[NSTextField labelWithString:text];f.font=[NSFont systemFontOfSize:size weight:weight];f.textColor=[NSColor colorWithCalibratedWhite:0.92 alpha:1];return f;}
static NSView* sliderRow(NSString* title,double lo,double hi,double value,id target,SEL action){NSStackView* s=[NSStackView stackViewWithViews:@[]];s.orientation=NSUserInterfaceLayoutOrientationVertical;s.spacing=4;NSTextField* c=label(title,11,NSFontWeightMedium);c.textColor=[NSColor colorWithCalibratedRed:.64 green:.73 blue:.86 alpha:1];NSSlider* slider=[NSSlider sliderWithValue:value minValue:lo maxValue:hi target:target action:action];slider.continuous=YES;[s addArrangedSubview:c];[s addArrangedSubview:slider];return s;}
static NSVisualEffectView* inspector(VulkaxRenderer* r,NSTextField** statsOut){NSVisualEffectView* p=[NSVisualEffectView new];p.material=NSVisualEffectMaterialSidebar;p.blendingMode=NSVisualEffectBlendingModeBehindWindow;p.state=NSVisualEffectStateActive;NSStackView* s=[NSStackView stackViewWithViews:@[]];s.translatesAutoresizingMaskIntoConstraints=NO;s.orientation=NSUserInterfaceLayoutOrientationVertical;s.alignment=NSLayoutAttributeLeading;s.spacing=12;s.edgeInsets=NSEdgeInsetsMake(20,18,18,18);[p addSubview:s];[NSLayoutConstraint activateConstraints:@[[s.leadingAnchor constraintEqualToAnchor:p.leadingAnchor],[s.trailingAnchor constraintEqualToAnchor:p.trailingAnchor],[s.topAnchor constraintEqualToAnchor:p.topAnchor]]];NSTextField* brand=label(@"VULKAX NATIVE VIEWER",11,NSFontWeightSemibold);brand.textColor=[NSColor colorWithCalibratedRed:.52 green:.70 blue:1 alpha:1];[s addArrangedSubview:brand];[s addArrangedSubview:label(@"Interactive Gaussian World",20,NSFontWeightBold)];NSSegmentedControl* m=[NSSegmentedControl segmentedControlWithLabels:@[@"Hybrid",@"Splats",@"Surface",@"Particles"] trackingMode:NSSegmentSwitchTrackingSelectOne target:r action:@selector(modeChanged:)];m.selectedSegment=0;[s addArrangedSubview:m];NSSegmentedControl* st=[NSSegmentedControl segmentedControlWithLabels:@[@"Before",@"Verified After"] trackingMode:NSSegmentSwitchTrackingSelectOne target:r action:@selector(stateChanged:)];st.selectedSegment=1;[s addArrangedSubview:st];NSButton* h=[NSButton checkboxWithTitle:@"Rewrite highlight" target:r action:@selector(highlightChanged:)];h.state=NSControlStateValueOn;NSButton* g=[NSButton checkboxWithTitle:@"Ground grid" target:r action:@selector(gridChanged:)];g.state=NSControlStateValueOn;NSButton* o=[NSButton checkboxWithTitle:@"Auto orbit" target:r action:@selector(orbitChanged:)];[s addArrangedSubview:h];[s addArrangedSubview:g];[s addArrangedSubview:o];[s addArrangedSubview:sliderRow(@"Splat scale",.2,4,1,r,@selector(splatScaleChanged:))];[s addArrangedSubview:sliderRow(@"Opacity",.05,1,.88,r,@selector(opacityChanged:))];[s addArrangedSubview:sliderRow(@"Exposure",.35,2.4,1,r,@selector(exposureChanged:))];[s addArrangedSubview:[NSButton buttonWithTitle:@"Open Gaussian PLY…" target:r action:@selector(openPly:)]];NSTextField* stats=label(@"",11,NSFontWeightRegular);stats.textColor=[NSColor colorWithCalibratedRed:.68 green:.77 blue:.90 alpha:1];stats.maximumNumberOfLines=0;[s addArrangedSubview:stats];*statsOut=stats;return p;}

@interface VulkaxAppDelegate : NSObject <NSApplicationDelegate> { @private ViewerScene _initialScene; }
@property(nonatomic,strong) NSWindow* window;
@property(nonatomic,strong) VulkaxRenderer* renderer;
- (instancetype)initWithScene:(ViewerScene)scene;
@end
@implementation VulkaxAppDelegate
- (instancetype)initWithScene:(ViewerScene)scene{self=[super init];if(self)_initialScene=std::move(scene);return self;}
- (void)applicationDidFinishLaunching:(NSNotification*)note{(void)note;id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!device){[NSApp terminate:nil];return;}self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1320,820) styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable) backing:NSBackingStoreBuffered defer:NO];self.window.title=@"Vulkax — Native Gaussian World Viewer";self.window.minSize=NSMakeSize(920,620);[self.window center];NSView* root=[NSView new];self.window.contentView=root;VulkaxMetalView* view=[[VulkaxMetalView alloc] initWithFrame:NSZeroRect device:device];view.translatesAutoresizingMaskIntoConstraints=NO;view.colorPixelFormat=MTLPixelFormatBGRA8Unorm_sRGB;view.depthStencilPixelFormat=MTLPixelFormatDepth32Float;view.preferredFramesPerSecond=60;view.paused=NO;view.enableSetNeedsDisplay=NO;try{self.renderer=[[VulkaxRenderer alloc] initWithView:view scene:std::move(_initialScene)];}catch(const std::exception& e){NSAlert* a=[NSAlert new];a.messageText=@"Vulkax renderer initialization failed";a.informativeText=ns(e.what());[a runModal];[NSApp terminate:nil];return;}view.delegate=self.renderer;view.renderer=self.renderer;NSTextField* stats=nil;NSVisualEffectView* panel=inspector(self.renderer,&stats);panel.translatesAutoresizingMaskIntoConstraints=NO;[self.renderer setStatsLabel:stats];[root addSubview:view];[root addSubview:panel];[NSLayoutConstraint activateConstraints:@[[view.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],[view.topAnchor constraintEqualToAnchor:root.topAnchor],[view.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],[view.trailingAnchor constraintEqualToAnchor:panel.leadingAnchor],[panel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],[panel.topAnchor constraintEqualToAnchor:root.topAnchor],[panel.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],[panel.widthAnchor constraintEqualToConstant:300]]];[self.window makeKeyAndOrderFront:nil];[self.window makeFirstResponder:view];[NSApp activateIgnoringOtherApps:YES];}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender{(void)sender;return YES;}
@end

static void menu(){NSMenu* bar=[NSMenu new];NSMenuItem* root=[NSMenuItem new];[bar addItem:root];NSApp.mainMenu=bar;NSMenu* m=[NSMenu new];[m addItemWithTitle:@"About Vulkax Viewer" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];[m addItem:[NSMenuItem separatorItem]];[m addItemWithTitle:@"Quit Vulkax Viewer" action:@selector(terminate:) keyEquivalent:@"q"];root.submenu=m;}
struct Args{std::filesystem::path run{"build/captured-world-run"},particles{},ply{};};
static Args args(int argc,const char* argv[]){Args a;for(int i=1;i<argc;++i){std::string v=argv[i];auto next=[&](const char* f){if(i+1>=argc)throw std::runtime_error(std::string(f)+" requires a path");return std::string(argv[++i]);};if(v=="--run")a.run=next("--run");else if(v=="--particles")a.particles=next("--particles");else if(v=="--ply")a.ply=next("--ply");else if(v=="--help"||v=="-h"){std::cout<<"Usage: vulkax_viewer [--run captured-world-run] [--particles particles.csv] [--ply gaussians.ply]\n";std::exit(0);}else if(!v.starts_with('-'))a.run=v;else throw std::runtime_error("unknown argument: "+v);}return a;}
int main(int argc,const char* argv[]){@autoreleasepool{try{const auto a=args(argc,argv);ViewerScene scene=a.ply.empty()?vulkax::viewer::loadCapturedWorldScene(a.run,a.particles):vulkax::viewer::loadStandaloneGaussianScene(a.ply);[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];menu();VulkaxAppDelegate* d=[[VulkaxAppDelegate alloc] initWithScene:std::move(scene)];NSApp.delegate=d;[NSApp run];return 0;}catch(const std::exception& e){std::cerr<<"vulkax_viewer: "<<e.what()<<'\n';return 1;}}}
