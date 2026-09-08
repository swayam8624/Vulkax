#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

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
    simd_float4 positionScaleX{}; // xyz, scale x
    simd_float4 scaleYZOpacity{}; // scale y, scale z, opacity, reserved
    simd_float4 rotation{};       // xyzw
    simd_float4 colorMark{};      // rgb, rewrite marker
};

struct GPUSurfaceVertex {
    simd_float4 position{};
    simd_float4 normal{};
    simd_float4 color{};
};

struct GPUUniforms {
    simd_float4x4 viewProjection{};
    simd_float4 viewportAndScale{}; // width, height, splat multiplier, max point radius px
    simd_float4 renderParams{};     // opacity, exposure, rewrite highlight, particle mode
};

[[nodiscard]] Vec3 cameraPosition(Vec3 target, double yaw, double pitch, double distance) {
    const double cp = std::cos(pitch);
    return {target.x + distance * cp * std::sin(yaw),
            target.y + distance * std::sin(pitch),
            target.z + distance * cp * std::cos(yaw)};
}

[[nodiscard]] simd_float4x4 makePerspective(float fovyRadians, float aspect, float nearZ, float farZ) {
    const float ys = 1.0F / std::tan(fovyRadians * 0.5F);
    const float xs = ys / std::max(aspect, 1.0e-6F);
    const float zs = farZ / (nearZ - farZ);
    return (simd_float4x4){
        (simd_float4){xs, 0, 0, 0},
        (simd_float4){0, ys, 0, 0},
        (simd_float4){0, 0, zs, -1},
        (simd_float4){0, 0, nearZ * zs, 0},
    };
}

[[nodiscard]] simd_float4x4 makeLookAt(Vec3 eyeValue, Vec3 centerValue) {
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

[[nodiscard]] NSString* nsString(const std::string& value) {
    return [NSString stringWithUTF8String:value.c_str()];
}

static NSString* const kMetalShaders = @R"METAL(
#include <metal_stdlib>
using namespace metal;

struct GPUSplat {
    float4 positionScaleX;
    float4 scaleYZOpacity;
    float4 rotation;
    float4 colorMark;
};
struct GPUSurfaceVertex { float4 position; float4 normal; float4 color; };
struct GPUUniforms {
    float4x4 viewProjection;
    float4 viewportAndScale;
    float4 renderParams;
};
struct SplatOut {
    float4 position [[position]];
    float2 local;
    float3 color;
    float opacity;
    float mark;
};
struct SurfaceOut {
    float4 position [[position]];
    float3 normal;
    float3 color;
};

float3 rotateByQuaternion(float4 q, float3 v) {
    // q is xyzw.
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

float2 projectedDelta(float3 center, float3 axis, float4 centerClip, float4x4 vp) {
    float4 p = vp * float4(center + axis, 1.0);
    float cw = (abs(centerClip.w) < 1e-6) ? copysign(1e-6, centerClip.w) : centerClip.w;
    float pw = (abs(p.w) < 1e-6) ? copysign(1e-6, p.w) : p.w;
    return p.xy / pw - centerClip.xy / cw;
}

vertex SplatOut splatVertex(uint vertexId [[vertex_id]],
                            uint instanceId [[instance_id]],
                            constant GPUSplat* splats [[buffer(0)]],
                            constant uint* order [[buffer(1)]],
                            constant GPUUniforms& u [[buffer(2)]]) {
    const uint index = order[instanceId];
    const GPUSplat s = splats[index];
    const float3 center = s.positionScaleX.xyz;
    const float3 scales = max(float3(s.positionScaleX.w, s.scaleYZOpacity.x, s.scaleYZOpacity.y), float3(1e-7));
    const float4 q = normalize(s.rotation);
    const float4 centerClip = u.viewProjection * float4(center, 1.0);

    const float3 ax = rotateByQuaternion(q, float3(scales.x, 0, 0));
    const float3 ay = rotateByQuaternion(q, float3(0, scales.y, 0));
    const float3 az = rotateByQuaternion(q, float3(0, 0, scales.z));
    float2 dx = projectedDelta(center, ax, centerClip, u.viewProjection);
    float2 dy = projectedDelta(center, ay, centerClip, u.viewProjection);
    float2 dz = projectedDelta(center, az, centerClip, u.viewProjection);

    // Project the 3D Gaussian covariance into screen space. The three projected
    // scaled basis vectors form J*R*S; C_2D = A*A^T.
    float a = dot(float3(dx.x, dy.x, dz.x), float3(dx.x, dy.x, dz.x));
    float b = dot(float3(dx.x, dy.x, dz.x), float3(dx.y, dy.y, dz.y));
    float c = dot(float3(dx.y, dy.y, dz.y), float3(dx.y, dy.y, dz.y));
    float trace = a + c;
    float disc = sqrt(max(0.0, (a - c) * (a - c) + 4.0 * b * b));
    float l0 = max(1e-12, 0.5 * (trace + disc));
    float l1 = max(1e-12, 0.5 * (trace - disc));
    float2 e0;
    if (abs(b) > 1e-10) e0 = normalize(float2(b, l0 - a));
    else e0 = (a >= c) ? float2(1, 0) : float2(0, 1);
    float2 e1 = float2(-e0.y, e0.x);
    float2 axis0 = e0 * sqrt(l0) * u.viewportAndScale.z;
    float2 axis1 = e1 * sqrt(l1) * u.viewportAndScale.z;

    // Keep tiny Gaussians visible and pathological source scales bounded.
    float2 halfViewport = max(u.viewportAndScale.xy * 0.5, float2(1));
    float r0 = length(axis0 * halfViewport);
    float r1 = length(axis1 * halfViewport);
    float maxRadius = max(r0, r1);
    if (maxRadius < 0.75) {
        float boost = 0.75 / max(maxRadius, 1e-6);
        axis0 *= boost; axis1 *= boost;
    }
    maxRadius = max(length(axis0 * halfViewport), length(axis1 * halfViewport));
    const float cap = max(8.0, u.viewportAndScale.w);
    if (maxRadius > cap) {
        float shrink = cap / maxRadius;
        axis0 *= shrink; axis1 *= shrink;
    }

    const float2 corners[4] = {float2(-1,-1), float2(1,-1), float2(-1,1), float2(1,1)};
    const float2 corner = corners[vertexId & 3u];
    const float cutoff = 3.0;
    const float2 ndcOffset = (axis0 * corner.x + axis1 * corner.y) * cutoff;

    SplatOut out;
    out.position = centerClip;
    out.position.xy += ndcOffset * centerClip.w;
    out.local = corner * cutoff;
    out.color = s.colorMark.rgb;
    out.opacity = s.scaleYZOpacity.z;
    out.mark = s.colorMark.w;
    return out;
}

fragment float4 splatFragment(SplatOut in [[stage_in]], constant GPUUniforms& u [[buffer(2)]]) {
    const float r2 = dot(in.local, in.local);
    if (r2 > 9.0) discard_fragment();
    float alpha = exp(-0.5 * r2) * in.opacity * u.renderParams.x;
    if (alpha < 0.004) discard_fragment();
    float3 color = in.color;
    if (in.mark > 0.5 && u.renderParams.z > 0.5) color = mix(color, float3(1.0, 0.28, 0.035), 0.88);
    color = 1.0 - exp(-color * max(u.renderParams.y, 0.01) * 1.25);
    return float4(color, alpha);
}

vertex SurfaceOut surfaceVertex(uint vertexId [[vertex_id]],
                                constant GPUSurfaceVertex* vertices [[buffer(0)]],
                                constant GPUUniforms& u [[buffer(2)]]) {
    const GPUSurfaceVertex v = vertices[vertexId];
    SurfaceOut out;
    out.position = u.viewProjection * float4(v.position.xyz, 1.0);
    out.normal = v.normal.xyz;
    out.color = v.color.rgb;
    return out;
}

fragment float4 surfaceFragment(SurfaceOut in [[stage_in]], constant GPUUniforms& u [[buffer(2)]]) {
    float3 n = normalize(in.normal);
    float3 key = normalize(float3(-0.45, 0.75, 0.85));
    float3 fill = normalize(float3(0.75, 0.15, 0.55));
    float kd = max(dot(n, key), 0.0);
    float fd = max(dot(n, fill), 0.0);
    float rim = pow(1.0 - abs(n.z), 2.0);
    float3 base = in.color;
    if (u.renderParams.z < 0.5 && base.r > 0.8 && base.g < 0.65) base = float3(0.28, 0.53, 0.95);
    float3 color = base * (0.24 + 0.72 * kd + 0.17 * fd) + float3(0.18, 0.28, 0.52) * rim * 0.18;
    color = 1.0 - exp(-color * max(u.renderParams.y, 0.01) * 1.15);
    return float4(color, 1.0);
}

vertex float4 lineVertex(uint vertexId [[vertex_id]],
                         constant float4* positions [[buffer(0)]],
                         constant GPUUniforms& u [[buffer(2)]]) {
    return u.viewProjection * positions[vertexId];
}
fragment float4 lineFragment() { return float4(0.22, 0.36, 0.58, 0.38); }
)METAL";

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

    NSError* error = nil;
    id<MTLLibrary> library = [_device newLibraryWithSource:kMetalShaders options:nil error:&error];
    if (!library) throw std::runtime_error("Metal shader compilation failed: " + std::string(error.localizedDescription.UTF8String ?: "unknown"));

    auto makePipeline = ^id<MTLRenderPipelineState>(NSString* vertex, NSString* fragment, BOOL blending) {
        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:vertex];
        descriptor.fragmentFunction = [library newFunctionWithName:fragment];
        descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat;
        descriptor.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
        if (blending) {
            auto* attachment = descriptor.colorAttachments[0];
            attachment.blendingEnabled = YES;
            attachment.rgbBlendOperation = MTLBlendOperationAdd;
            attachment.alphaBlendOperation = MTLBlendOperationAdd;
            attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
            attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        }
        NSError* pipelineError = nil;
        id<MTLRenderPipelineState> result = [_device newRenderPipelineStateWithDescriptor:descriptor error:&pipelineError];
        if (!result) throw std::runtime_error("Metal pipeline creation failed: " + std::string(pipelineError.localizedDescription.UTF8String ?: "unknown"));
        return result;
    };
    _splatPipeline = makePipeline(@"splatVertex", @"splatFragment", YES);
    _surfacePipeline = makePipeline(@"surfaceVertex", @"surfaceFragment", NO);
    _linePipeline = makePipeline(@"lineVertex", @"lineFragment", YES);

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
    std::vector<GPUSplat> result;
    result.reserve(source.size());
    for (const auto& g : source) {
        GPUSplat value;
        value.positionScaleX = {(float)g.position.x, (float)g.position.y, (float)g.position.z, g.scale[0]};
        value.scaleYZOpacity = {g.scale[1], g.scale[2], g.opacity, 0.0F};
        // CPU storage is wxyz; Metal helper expects xyzw.
        value.rotation = {g.rotation[1], g.rotation[2], g.rotation[3], g.rotation[0]};
        value.colorMark = {g.color[0], g.color[1], g.color[2], 0.0F};
        result.push_back(value);
    }
    return result;
}

- (void)uploadScene {
    const auto before = [self gpuGaussians:_scene.before];
    const auto after = [self gpuGaussians:_scene.after];
    _beforeBuffer = [self bufferFromBytes:before.data() length:before.size() * sizeof(GPUSplat)];
    _afterBuffer = [self bufferFromBytes:after.data() length:after.size() * sizeof(GPUSplat)];

    std::vector<GPUSplat> particles;
    particles.reserve(_scene.particles.size());
    const float particleRadius = static_cast<float>(std::max(0.0015, _scene.bounds.radius * 0.014));
    for (const auto& p : _scene.particles) {
        GPUSplat value;
        value.positionScaleX = {(float)p.position.x, (float)p.position.y, (float)p.position.z, particleRadius};
        value.scaleYZOpacity = {particleRadius, particleRadius, 0.97F, 0};
        value.rotation = {0, 0, 0, 1};
        value.colorMark = {0.42F, 0.70F, 1.0F, p.inRewriteRegion ? 1.0F : 0.0F};
        particles.push_back(value);
    }
    _particleBuffer = [self bufferFromBytes:particles.data() length:particles.size() * sizeof(GPUSplat)];

    std::vector<GPUSurfaceVertex> surface;
    surface.reserve(_scene.surfaceTriangles.size());
    for (const auto& v : _scene.surfaceTriangles) {
        GPUSurfaceVertex gpu;
        gpu.position = {(float)v.position.x, (float)v.position.y, (float)v.position.z, 1};
        gpu.normal = {(float)v.normal.x, (float)v.normal.y, (float)v.normal.z, 0};
        gpu.color = {v.color[0], v.color[1], v.color[2], 1};
        surface.push_back(gpu);
    }
    _surfaceBuffer = [self bufferFromBytes:surface.data() length:surface.size() * sizeof(GPUSurfaceVertex)];

    _beforeOrder.resize(_scene.before.size());
    _afterOrder.resize(_scene.after.size());
    _particleOrder.resize(_scene.particles.size());
    std::iota(_beforeOrder.begin(), _beforeOrder.end(), 0U);
    std::iota(_afterOrder.begin(), _afterOrder.end(), 0U);
    std::iota(_particleOrder.begin(), _particleOrder.end(), 0U);
    _beforeOrderBuffer = [self bufferFromBytes:_beforeOrder.data() length:_beforeOrder.size() * sizeof(std::uint32_t)];
    _afterOrderBuffer = [self bufferFromBytes:_afterOrder.data() length:_afterOrder.size() * sizeof(std::uint32_t)];
    _particleOrderBuffer = [self bufferFromBytes:_particleOrder.data() length:_particleOrder.size() * sizeof(std::uint32_t)];

    std::vector<simd_float4> grid;
    grid.reserve(84);
    const double y = _scene.bounds.minimum.y - _scene.bounds.radius * 0.06;
    const double r = _scene.bounds.radius * 1.45;
    for (int i = -10; i <= 10; ++i) {
        const double t = static_cast<double>(i) / 10.0 * r;
        grid.push_back({(float)(_scene.bounds.center.x - r), (float)y, (float)(_scene.bounds.center.z + t), 1});
        grid.push_back({(float)(_scene.bounds.center.x + r), (float)y, (float)(_scene.bounds.center.z + t), 1});
        grid.push_back({(float)(_scene.bounds.center.x + t), (float)y, (float)(_scene.bounds.center.z - r), 1});
        grid.push_back({(float)(_scene.bounds.center.x + t), (float)y, (float)(_scene.bounds.center.z + r), 1});
    }
    _gridBuffer = [self bufferFromBytes:grid.data() length:grid.size() * sizeof(simd_float4)];
    _sortDirty = YES;
    [self updateStats];
}

- (void)setStatsLabel:(NSTextField*)label { _statsLabel = label; [self updateStats]; }

- (void)updateStats {
    if (!_statsLabel) return;
    NSString* text = [NSString stringWithFormat:@"Gaussians   %lu → %lu\nParticles   %lu\nRewrite     %lu\nSurface     %@\nMax Δ       %.3e\n\n1–4 modes · B/A state\nH highlight · G grid\nSpace orbit · R camera",
                      (unsigned long)_scene.before.size(), (unsigned long)_scene.after.size(),
                      (unsigned long)_scene.particles.size(), (unsigned long)_scene.rewriteParticleCount,
                      nsString(_scene.surfaceKind), _scene.maxGaussianDisplacement];
    _statsLabel.stringValue = text;
}

- (void)resetCamera {
    _target = _scene.bounds.center;
    _yaw = 0.68;
    _pitch = 0.30;
    _distance = std::max(0.12, _scene.bounds.radius * 3.2);
    _sortDirty = YES;
}

- (void)orbitDX:(double)dx dy:(double)dy {
    _yaw -= dx * 0.006;
    _pitch = std::clamp(_pitch - dy * 0.006, -1.45, 1.45);
    _sortDirty = YES;
}

- (void)panDX:(double)dx dy:(double)dy {
    const Vec3 eye = cameraPosition(_target, _yaw, _pitch, _distance);
    const Vec3 forward = vulkax::math::normalized(_target - eye);
    Vec3 right = vulkax::math::normalized(vulkax::math::cross(forward, Vec3{0, 1, 0}));
    if (vulkax::math::length(right) < 1.0e-9) right = {1, 0, 0};
    const Vec3 up = vulkax::math::normalized(vulkax::math::cross(right, forward));
    const double scale = _distance * 0.0015;
    _target += (-dx * scale) * right + (dy * scale) * up;
    _sortDirty = YES;
}

- (void)zoomDelta:(double)delta {
    _distance *= std::exp(-delta * 0.035);
    _distance = std::clamp(_distance, std::max(0.002, _scene.bounds.radius * 0.04), std::max(2.0, _scene.bounds.radius * 50.0));
    _sortDirty = YES;
}

- (void)handleKey:(NSString*)key {
    if ([key isEqualToString:@"1"]) _mode = ViewMode::Hybrid;
    else if ([key isEqualToString:@"2"]) _mode = ViewMode::Splats;
    else if ([key isEqualToString:@"3"]) _mode = ViewMode::Surface;
    else if ([key isEqualToString:@"4"]) _mode = ViewMode::Particles;
    else if ([key isEqualToString:@"b"]) _state = WorldState::Before;
    else if ([key isEqualToString:@"a"]) _state = WorldState::After;
    else if ([key isEqualToString:@"h"]) _highlight = !_highlight;
    else if ([key isEqualToString:@"g"]) _showGrid = !_showGrid;
    else if ([key isEqualToString:@"r"]) [self resetCamera];
    else if ([key isEqualToString:@" "]) _autoOrbit = !_autoOrbit;
    _sortDirty = YES;
}

- (IBAction)modeChanged:(NSSegmentedControl*)sender { _mode = static_cast<ViewMode>(sender.selectedSegment); }
- (IBAction)stateChanged:(NSSegmentedControl*)sender { _state = sender.selectedSegment == 0 ? WorldState::Before : WorldState::After; _sortDirty = YES; }
- (IBAction)highlightChanged:(NSButton*)sender { _highlight = sender.state == NSControlStateValueOn; }
- (IBAction)gridChanged:(NSButton*)sender { _showGrid = sender.state == NSControlStateValueOn; }
- (IBAction)orbitChanged:(NSButton*)sender { _autoOrbit = sender.state == NSControlStateValueOn; }
- (IBAction)splatScaleChanged:(NSSlider*)sender { _splatScale = (float)sender.doubleValue; }
- (IBAction)opacityChanged:(NSSlider*)sender { _opacity = (float)sender.doubleValue; }
- (IBAction)exposureChanged:(NSSlider*)sender { _exposure = (float)sender.doubleValue; }

- (IBAction)openPly:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedFileTypes = @[@"ply"];
    if ([panel runModal] != NSModalResponseOK || panel.URL == nil) return;
    try {
        _scene = vulkax::viewer::loadStandaloneGaussianScene(std::filesystem::path(panel.URL.path.UTF8String));
        [self uploadScene];
        [self resetCamera];
        [self updateStats];
    } catch (const std::exception& error) {
        NSAlert* alert = [NSAlert new];
        alert.messageText = @"Could not open Gaussian PLY";
        alert.informativeText = nsString(error.what());
        alert.alertStyle = NSAlertStyleWarning;
        [alert runModal];
    }
}

- (void)sortGaussians:(const std::vector<vulkax::viewer::ViewerGaussian>&)source
                 order:(std::vector<std::uint32_t>&)order
                buffer:(id<MTLBuffer>)buffer
                   eye:(Vec3)eye
               forward:(Vec3)forward {
    if (!buffer || order.size() != source.size()) return;
    std::sort(order.begin(), order.end(), [&](std::uint32_t lhs, std::uint32_t rhs) {
        const double ld = vulkax::math::dot(source[lhs].position - eye, forward);
        const double rd = vulkax::math::dot(source[rhs].position - eye, forward);
        return ld > rd;
    });
    std::memcpy(buffer.contents, order.data(), order.size() * sizeof(std::uint32_t));
}

- (void)updateSortIfNeeded {
    if (!_sortDirty) return;
    const Vec3 eye = cameraPosition(_target, _yaw, _pitch, _distance);
    const Vec3 forward = vulkax::math::normalized(_target - eye);
    [self sortGaussians:_scene.before order:_beforeOrder buffer:_beforeOrderBuffer eye:eye forward:forward];
    [self sortGaussians:_scene.after order:_afterOrder buffer:_afterOrderBuffer eye:eye forward:forward];
    _sortDirty = NO;
}

- (GPUUniforms)uniformsForView:(MTKView*)view particleMode:(BOOL)particleMode {
    const Vec3 eye = cameraPosition(_target, _yaw, _pitch, _distance);
    const float aspect = (float)(view.drawableSize.width / std::max(view.drawableSize.height, 1.0));
    const float nearZ = static_cast<float>(std::max(0.0005, _scene.bounds.radius * 0.008));
    const float farZ = static_cast<float>(std::max(10.0, _scene.bounds.radius * 60.0 + _distance));
    GPUUniforms uniforms;
    uniforms.viewProjection = simd_mul(makePerspective(45.0F * (float)M_PI / 180.0F, aspect, nearZ, farZ), makeLookAt(eye, _target));
    uniforms.viewportAndScale = {(float)view.drawableSize.width, (float)view.drawableSize.height,
                                 particleMode ? 0.72F : _splatScale, particleMode ? 34.0F : 128.0F};
    uniforms.renderParams = {_opacity, _exposure, _highlight ? 1.0F : 0.0F, particleMode ? 1.0F : 0.0F};
    return uniforms;
}

- (void)drawSplatsWithEncoder:(id<MTLRenderCommandEncoder>)encoder
                         view:(MTKView*)view
                       buffer:(id<MTLBuffer>)buffer
                  orderBuffer:(id<MTLBuffer>)orderBuffer
                        count:(NSUInteger)count
                     particles:(BOOL)particles {
    if (!buffer || !orderBuffer || count == 0) return;
    GPUUniforms uniforms = [self uniformsForView:view particleMode:particles];
    [encoder setRenderPipelineState:_splatPipeline];
    [encoder setDepthStencilState:_transparentDepth];
    [encoder setVertexBuffer:buffer offset:0 atIndex:0];
    [encoder setVertexBuffer:orderBuffer offset:0 atIndex:1];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:2];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:2];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:count];
}

- (void)drawInMTKView:(MTKView*)view {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - _lastFrame).count();
    _lastFrame = now;
    if (_autoOrbit) {
        _yaw += dt * 0.28;
        _sortDirty = YES;
    }
    [self updateSortIfNeeded];

    id<CAMetalDrawable> drawable = view.currentDrawable;
    MTLRenderPassDescriptor* pass = view.currentRenderPassDescriptor;
    if (!drawable || !pass) return;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.018, 0.028, 0.060, 1.0);
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.depthAttachment.clearDepth = 1.0;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;

    id<MTLCommandBuffer> command = [_queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];

    if (_showGrid && _gridBuffer) {
        GPUUniforms uniforms = [self uniformsForView:view particleMode:NO];
        [encoder setRenderPipelineState:_linePipeline];
        [encoder setDepthStencilState:_transparentDepth];
        [encoder setVertexBuffer:_gridBuffer offset:0 atIndex:0];
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:2];
        [encoder drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:84];
    }

    if ((_mode == ViewMode::Hybrid || _mode == ViewMode::Surface) && _surfaceBuffer && !_scene.surfaceTriangles.empty()) {
        GPUUniforms uniforms = [self uniformsForView:view particleMode:NO];
        [encoder setRenderPipelineState:_surfacePipeline];
        [encoder setDepthStencilState:_opaqueDepth];
        [encoder setVertexBuffer:_surfaceBuffer offset:0 atIndex:0];
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:2];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:2];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:_scene.surfaceTriangles.size()];
    }

    if (_mode == ViewMode::Hybrid || _mode == ViewMode::Splats) {
        const BOOL before = _state == WorldState::Before;
        [self drawSplatsWithEncoder:encoder view:view
                             buffer:before ? _beforeBuffer : _afterBuffer
                        orderBuffer:before ? _beforeOrderBuffer : _afterOrderBuffer
                              count:before ? _scene.before.size() : _scene.after.size()
                           particles:NO];
    }

    if (_mode == ViewMode::Hybrid || _mode == ViewMode::Particles) {
        [self drawSplatsWithEncoder:encoder view:view buffer:_particleBuffer orderBuffer:_particleOrderBuffer
                              count:_scene.particles.size() particles:YES];
    }

    [encoder endEncoding];
    [command presentDrawable:drawable];
    [command commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size { (void)view; (void)size; _sortDirty = YES; }

@end

[[nodiscard]] NSTextField* label(NSString* text, CGFloat size, NSFontWeight weight) {
    NSTextField* field = [NSTextField labelWithString:text];
    field.font = [NSFont systemFontOfSize:size weight:weight];
    field.textColor = [NSColor colorWithCalibratedWhite:0.92 alpha:1.0];
    return field;
}

[[nodiscard]] NSView* makeSliderRow(NSString* title, double min, double max, double value,
                                    id target, SEL action) {
    NSStackView* row = [NSStackView stackViewWithViews:@[]];
    row.orientation = NSUserInterfaceLayoutOrientationVertical;
    row.spacing = 4;
    NSTextField* caption = label(title, 11, NSFontWeightMedium);
    caption.textColor = [NSColor colorWithCalibratedRed:0.64 green:0.73 blue:0.86 alpha:1.0];
    NSSlider* slider = [NSSlider sliderWithValue:value minValue:min maxValue:max target:target action:action];
    slider.continuous = YES;
    [row addArrangedSubview:caption];
    [row addArrangedSubview:slider];
    return row;
}

[[nodiscard]] NSVisualEffectView* makeInspector(VulkaxRenderer* renderer, NSTextField** statsOut) {
    NSVisualEffectView* panel = [NSVisualEffectView new];
    panel.material = NSVisualEffectMaterialSidebar;
    panel.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    panel.state = NSVisualEffectStateActive;

    NSStackView* stack = [NSStackView stackViewWithViews:@[]];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 12;
    stack.edgeInsets = NSEdgeInsetsMake(20, 18, 18, 18);
    [panel addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:panel.topAnchor],
    ]];

    NSTextField* brand = label(@"VULKAX NATIVE VIEWER", 11, NSFontWeightSemibold);
    brand.textColor = [NSColor colorWithCalibratedRed:0.52 green:0.70 blue:1.0 alpha:1.0];
    [stack addArrangedSubview:brand];
    [stack addArrangedSubview:label(@"Interactive Gaussian World", 20, NSFontWeightBold)];

    NSSegmentedControl* mode = [NSSegmentedControl segmentedControlWithLabels:@[@"Hybrid", @"Splats", @"Surface", @"Particles"]
                                                                      trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                            target:renderer action:@selector(modeChanged:)];
    mode.selectedSegment = 0;
    mode.segmentStyle = NSSegmentStyleRounded;
    [stack addArrangedSubview:mode];

    NSSegmentedControl* state = [NSSegmentedControl segmentedControlWithLabels:@[@"Before", @"Verified After"]
                                                                       trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                             target:renderer action:@selector(stateChanged:)];
    state.selectedSegment = 1;
    [stack addArrangedSubview:state];

    NSButton* highlight = [NSButton checkboxWithTitle:@"Rewrite highlight" target:renderer action:@selector(highlightChanged:)];
    highlight.state = NSControlStateValueOn;
    NSButton* grid = [NSButton checkboxWithTitle:@"Ground grid" target:renderer action:@selector(gridChanged:)];
    grid.state = NSControlStateValueOn;
    NSButton* orbit = [NSButton checkboxWithTitle:@"Auto orbit" target:renderer action:@selector(orbitChanged:)];
    [stack addArrangedSubview:highlight];
    [stack addArrangedSubview:grid];
    [stack addArrangedSubview:orbit];

    [stack addArrangedSubview:makeSliderRow(@"Splat scale", 0.2, 4.0, 1.0, renderer, @selector(splatScaleChanged:))];
    [stack addArrangedSubview:makeSliderRow(@"Opacity", 0.05, 1.0, 0.88, renderer, @selector(opacityChanged:))];
    [stack addArrangedSubview:makeSliderRow(@"Exposure", 0.35, 2.4, 1.0, renderer, @selector(exposureChanged:))];

    NSButton* open = [NSButton buttonWithTitle:@"Open Gaussian PLY…" target:renderer action:@selector(openPly:)];
    open.bezelStyle = NSBezelStyleRounded;
    [stack addArrangedSubview:open];

    NSTextField* stats = label(@"", 11, NSFontWeightRegular);
    stats.textColor = [NSColor colorWithCalibratedRed:0.68 green:0.77 blue:0.90 alpha:1.0];
    stats.maximumNumberOfLines = 0;
    [stack addArrangedSubview:stats];
    *statsOut = stats;
    return panel;
}

@interface VulkaxAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) VulkaxRenderer* renderer;
@property(nonatomic) ViewerScene initialScene;
@end

@implementation VulkaxAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        NSAlert* alert = [NSAlert new];
        alert.messageText = @"Metal is unavailable";
        alert.informativeText = @"Vulkax Native Viewer requires a Metal-capable Mac.";
        [alert runModal];
        [NSApp terminate:nil];
        return;
    }

    NSRect frame = NSMakeRect(0, 0, 1320, 820);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                         NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                                                backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"Vulkax — Native Gaussian World Viewer";
    self.window.minSize = NSMakeSize(920, 620);
    self.window.delegate = self;
    [self.window center];

    NSView* root = [NSView new];
    self.window.contentView = root;
    VulkaxMetalView* metalView = [[VulkaxMetalView alloc] initWithFrame:NSZeroRect device:device];
    metalView.translatesAutoresizingMaskIntoConstraints = NO;
    metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    metalView.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    metalView.sampleCount = 1;
    metalView.preferredFramesPerSecond = 60;
    metalView.paused = NO;
    metalView.enableSetNeedsDisplay = NO;
    metalView.clearColor = MTLClearColorMake(0.018, 0.028, 0.060, 1.0);

    try {
        self.renderer = [[VulkaxRenderer alloc] initWithView:metalView scene:std::move(_initialScene)];
    } catch (const std::exception& error) {
        NSAlert* alert = [NSAlert new];
        alert.messageText = @"Vulkax renderer initialization failed";
        alert.informativeText = nsString(error.what());
        [alert runModal];
        [NSApp terminate:nil];
        return;
    }
    metalView.delegate = self.renderer;
    metalView.vulkaxRenderer = self.renderer;

    NSTextField* stats = nil;
    NSVisualEffectView* inspector = makeInspector(self.renderer, &stats);
    inspector.translatesAutoresizingMaskIntoConstraints = NO;
    [self.renderer setStatsLabel:stats];

    [root addSubview:metalView];
    [root addSubview:inspector];
    [NSLayoutConstraint activateConstraints:@[
        [metalView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [metalView.topAnchor constraintEqualToAnchor:root.topAnchor],
        [metalView.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
        [metalView.trailingAnchor constraintEqualToAnchor:inspector.leadingAnchor],
        [inspector.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [inspector.topAnchor constraintEqualToAnchor:root.topAnchor],
        [inspector.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
        [inspector.widthAnchor constraintEqualToConstant:300],
    ]];

    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:metalView];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { (void)sender; return YES; }
@end

void installApplicationMenu() {
    NSMenu* menuBar = [NSMenu new];
    NSMenuItem* appItem = [NSMenuItem new];
    [menuBar addItem:appItem];
    [NSApp setMainMenu:menuBar];
    NSMenu* appMenu = [NSMenu new];
    NSString* appName = @"Vulkax Viewer";
    [appMenu addItemWithTitle:[@"About " stringByAppendingString:appName] action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:appName] action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
}

struct Arguments {
    std::filesystem::path runDirectory{"build/captured-world-run"};
    std::filesystem::path particlesCsv{};
    std::filesystem::path ply{};
};

[[nodiscard]] Arguments parseArguments(int argc, const char* argv[]) {
    Arguments args;
    for (int index = 1; index < argc; ++index) {
        const std::string value = argv[index];
        const auto requireValue = [&](const char* flag) -> std::string {
            if (index + 1 >= argc) throw std::runtime_error(std::string(flag) + " requires a path");
            return argv[++index];
        };
        if (value == "--run") args.runDirectory = requireValue("--run");
        else if (value == "--particles") args.particlesCsv = requireValue("--particles");
        else if (value == "--ply") args.ply = requireValue("--ply");
        else if (value == "--help" || value == "-h") {
            std::cout << "Usage: vulkax_viewer [--run captured-world-run] [--particles particles.csv] [--ply gaussians.ply]\n"
                         "\nControls: left-drag orbit, right-drag pan, wheel zoom, R reset, 1-4 modes, B/A state.\n";
            std::exit(0);
        } else if (!value.starts_with('-')) args.runDirectory = value;
        else throw std::runtime_error("unknown argument: " + value);
    }
    return args;
}

} // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        try {
            const auto args = parseArguments(argc, argv);
            ViewerScene scene = args.ply.empty()
                                    ? vulkax::viewer::loadCapturedWorldScene(args.runDirectory, args.particlesCsv)
                                    : vulkax::viewer::loadStandaloneGaussianScene(args.ply);
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            installApplicationMenu();
            VulkaxAppDelegate* delegate = [VulkaxAppDelegate new];
            delegate.initialScene = std::move(scene);
            NSApp.delegate = delegate;
            [NSApp run];
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "vulkax_viewer: " << error.what() << '\n';
            return 1;
        }
    }
}
