import SwiftUI
import MetalKit
import ObjectiveC
import Darwin

enum VisualizerMode: Float, CaseIterable, Identifiable {
    case wave = 0
    case schwarzschild = 1
    case volumeSmoke = 2

    var id: Float { rawValue }
    var title: String {
        switch self {
        case .wave: return "Wave Field"
        case .schwarzschild: return "Schwarzschild"
        case .volumeSmoke: return "Volume Smoke"
        }
    }
}

final class PhysicsModel: ObservableObject {
    @Published var amplitude: Float = 1.0
    @Published var wavenumber: Float = 2.0
    @Published var angularFrequency: Float = 3.0
    @Published var blackHoleMass: Float = 1.0
    @Published var diskGain: Float = 1.0
    @Published var cameraScale: Float = 1.0
    @Published var smokeBuoyancy: Float = 1.0
    @Published var smokeTurbulence: Float = 1.0
    @Published var volumeExtinction: Float = 2.2
    @Published var volumeEmission: Float = 1.0
    @Published var accumulationResetToken: UInt32 = 0
    @Published var playing = true
    @Published var time: Float = 0.0
    @Published var mode: VisualizerMode = .wave

    init() {
        if CommandLine.arguments.contains("--black-hole-smoke") {
            mode = .schwarzschild
        } else if CommandLine.arguments.contains("--volume-smoke") {
            mode = .volumeSmoke
        }
    }
}

struct WaveUniforms {
    var time: Float
    var amplitude: Float
    var wavenumber: Float
    var angularFrequency: Float
    var width: Float
    var height: Float
    var padding: SIMD4<Float> = .zero
    var control: SIMD4<Float> = .zero
    var renderParameters: SIMD4<Float> = .zero
}

struct GeneratedFieldParameters {
    var width: UInt32
    var height: UInt32
    var time: Float
    var amplitude: Float
    var wavenumber: Float
    var angularFrequency: Float
    var padding: SIMD2<Float> = .zero
}

private var rendererAssociationKey: UInt8 = 0

private let waveShader = """
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float time;
    float amplitude;
    float wavenumber;
    float angularFrequency;
    float width;
    float height;
    float4 padding;
    float4 control;
    float4 renderParameters;
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut fullscreen(uint id [[vertex_id]]) {
    constexpr float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VertexOut out;
    out.position = float4(positions[id], 0.0, 1.0);
    out.uv = positions[id] * 0.5 + 0.5;
    return out;
}

float3 palette(float value) {
    float shadow = pow(clamp(value, 0.0, 1.0), 0.72);
    return float3(
        0.025 + 0.91 * pow(shadow, 1.55),
        0.055 + 0.54 * sin(shadow * 1.47),
        0.13 + 0.70 * (1.0 - shadow) * (1.0 - shadow));
}

struct GeodesicState {
    float radius;
    float radialVelocity;
    float azimuth;
};

GeodesicState geodesicDerivative(GeodesicState state, float mass, float angularMomentum) {
    float radius2 = state.radius * state.radius;
    float radius3 = radius2 * state.radius;
    float radius4 = radius3 * state.radius;
    return GeodesicState{
        state.radialVelocity,
        angularMomentum * angularMomentum / radius3 -
            3.0 * mass * angularMomentum * angularMomentum / radius4,
        angularMomentum / radius2};
}

GeodesicState geodesicRk4(GeodesicState state, float mass, float angularMomentum, float step) {
    GeodesicState k1 = geodesicDerivative(state, mass, angularMomentum);
    GeodesicState k2 = geodesicDerivative(GeodesicState{
        state.radius + 0.5 * step * k1.radius,
        state.radialVelocity + 0.5 * step * k1.radialVelocity,
        state.azimuth + 0.5 * step * k1.azimuth}, mass, angularMomentum);
    GeodesicState k3 = geodesicDerivative(GeodesicState{
        state.radius + 0.5 * step * k2.radius,
        state.radialVelocity + 0.5 * step * k2.radialVelocity,
        state.azimuth + 0.5 * step * k2.azimuth}, mass, angularMomentum);
    GeodesicState k4 = geodesicDerivative(GeodesicState{
        state.radius + step * k3.radius,
        state.radialVelocity + step * k3.radialVelocity,
        state.azimuth + step * k3.azimuth}, mass, angularMomentum);
    return GeodesicState{
        state.radius + step * (k1.radius + 2.0 * k2.radius + 2.0 * k3.radius + k4.radius) / 6.0,
        state.radialVelocity + step * (k1.radialVelocity + 2.0 * k2.radialVelocity + 2.0 * k3.radialVelocity + k4.radialVelocity) / 6.0,
        state.azimuth + step * (k1.azimuth + 2.0 * k2.azimuth + 2.0 * k3.azimuth + k4.azimuth) / 6.0};
}

float hash21(float2 point) {
    point = fract(point * float2(123.34, 345.45));
    point += dot(point, point + 34.345);
    return fract(point.x * point.y);
}

// A per-pixel Cranley--Patterson rotation of an R2 sequence. Each progressive
// Schwarzschild frame therefore traces a distinct subpixel ray instead of
// repeatedly accumulating the same pixel-centre approximation.
float2 subpixelJitter(uint2 pixel, float sampleIndex) {
    float2 pixelSeed = float2(pixel);
    float2 rotation = float2(hash21(pixelSeed + 17.0), hash21(pixelSeed + 91.0));
    float2 r2 = fract((sampleIndex + 0.5) * float2(0.754877666, 0.569840291) + rotation);
    return r2 - 0.5;
}

float3 celestialRadiance(float2 direction) {
    float2 cell = floor(direction * float2(37.0, 19.0));
    float2 local = fract(direction * float2(37.0, 19.0)) - 0.5;
    float star = smoothstep(0.06, 0.0, length(local - (float2(hash21(cell), hash21(cell + 19.17)) - 0.5)));
    float temperature = 0.45 + 0.55 * hash21(cell + 7.2);
    return float3(0.002, 0.004, 0.012) + star * mix(float3(0.35, 0.48, 1.1), float3(1.4, 0.72, 0.36), temperature);
}

float3 schwarzschildRadiance(float2 uv, constant Uniforms& u) {
    float aspect = u.width / max(1.0, u.height);
    float2 image = (uv - 0.5) * float2(2.0 * aspect, 2.0) / max(u.renderParameters.z, 0.1);
    const float mass = max(u.renderParameters.x, 0.2);
    constexpr float observerRadius = 52.0;
    const float3 radial = float3(1.0, 0.0, 0.0);
    const float3 direction = normalize(float3(-1.0, image.x, image.y));
    const float radialVelocity = dot(direction, radial);
    const float3 tangentVector = direction - radial * radialVelocity;
    const float tangentLength = length(tangentVector);
    if (tangentLength < 1e-5) return float3(0.0);
    const float3 tangent = tangentVector / tangentLength;
    const float angularMomentum = observerRadius * tangentLength;
    GeodesicState state = GeodesicState{observerRadius, radialVelocity, 0.0};
    float3 previousPoint = radial * observerRadius;
    bool diskHit = false;
    float diskRadius = 0.0;
    bool escaped = false;
    // The native viewport uses a bounded coherent preview profile. High-step
    // references remain in the CPU/Vulkan validation paths rather than
    // monopolizing an interactive CAMetalLayer frame.
    constexpr float step = 0.20;
    for (uint iteration = 0; iteration < 960; ++iteration) {
        state = geodesicRk4(state, mass, angularMomentum, step);
        if (!isfinite(state.radius) || !isfinite(state.radialVelocity) || state.radius <= 0.0) return float3(0.0);
        if (state.radius <= 2.0 * mass * (1.0 + 1e-4)) return float3(0.0);
        const float3 orbitalDirection = radial * cos(state.azimuth) + tangent * sin(state.azimuth);
        const float3 point = orbitalDirection * state.radius;
        if (!diskHit && previousPoint.z * point.z <= 0.0 && state.radius >= 3.2 * mass && state.radius <= 17.0 * mass) {
            diskHit = true;
            diskRadius = state.radius;
        }
        previousPoint = point;
        if (iteration > 2 && state.radius >= observerRadius && state.radialVelocity > 0.0) {
            escaped = true;
            break;
        }
    }
    if (!escaped) return float3(0.0);
    const float3 escapedDirection = normalize(previousPoint);
    float2 source = float2(atan2(escapedDirection.y, escapedDirection.x) / (2.0 * M_PI_F) + 0.5,
                            asin(clamp(escapedDirection.z, -1.0, 1.0)) / M_PI_F + 0.5);
    float3 radiance = celestialRadiance(source);
    if (diskHit) {
        const float disk = (1.0 - smoothstep(4.2 * mass, 17.0 * mass, diskRadius)) * smoothstep(3.2 * mass, 4.0 * mass, diskRadius);
        const float beaming = pow(max(0.08, 1.0 + 0.55 * tangent.y), 3.0);
        const float temperature = pow(4.0 * mass / max(diskRadius, 4.0 * mass), 0.75);
        radiance += float3(2.3, 0.54, 0.08) * disk * temperature * beaming * max(u.renderParameters.y, 0.0);
    }
    return radiance;
}

kernel void simulateWave(
    texture2d<half, access::write> outputRadiance [[texture(0)]],
    constant Uniforms& u [[buffer(0)]],
    uint2 pixel [[thread_position_in_grid]]) {
    if (pixel.x >= uint(u.width) || pixel.y >= uint(u.height)) return;
    float2 uv = (float2(pixel) + 0.5) / float2(u.width, u.height);
    float aspect = u.width / max(1.0, u.height);
    float2 p = (uv - 0.5) * float2(8.0 * aspect, 8.0);
    float3 radiance;
    if (u.control.x > 0.5) {
        const float2 jitter = subpixelJitter(pixel, floor(u.control.z));
        radiance = schwarzschildRadiance(uv + jitter / float2(u.width, u.height), u);
    } else {
        float phase = u.wavenumber * p.x - u.angularFrequency * u.time;
        float radial = 0.18 * sin(1.35 * length(p) - 0.7 * u.time);
        float field = u.amplitude * (sin(phase) + radial);
        radiance = palette(0.5 + 0.5 * tanh(field));
    }
    // This image remains linear HDR GPU data. Presentation and display-space
    // dithering are intentionally a separate render pass.
    outputRadiance.write(half4(half3(radiance * 2.25), 1.0h), pixel);
}

kernel void clearScalarVolume(
    texture3d<half, access::write> density [[texture(0)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= density.get_width() || cell.y >= density.get_height() || cell.z >= density.get_depth()) return;
    density.write(half4(0.0h), cell);
}

kernel void clearVectorVolume(
    texture3d<half, access::write> velocity [[texture(0)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= velocity.get_width() || cell.y >= velocity.get_height() || cell.z >= velocity.get_depth()) return;
    velocity.write(half4(0.0h), cell);
}

float scalarAt(texture3d<half, access::read> field, int3 cell) {
    int3 maximum = int3(field.get_width() - 1, field.get_height() - 1, field.get_depth() - 1);
    return float(field.read(uint3(clamp(cell, int3(0), maximum))).r);
}

float sampleMacU(texture3d<half, access::sample> faceU, float3 position) {
    constexpr sampler linearVolume(filter::linear, mip_filter::none, address::clamp_to_edge);
    float nx = float(faceU.get_width() - 1);
    float3 coordinate = float3((position.x * nx + 0.5) / float(faceU.get_width()), position.y, position.z);
    return float(faceU.sample(linearVolume, clamp(coordinate, float3(0.001), float3(0.999))).r);
}

float sampleMacV(texture3d<half, access::sample> faceV, float3 position) {
    constexpr sampler linearVolume(filter::linear, mip_filter::none, address::clamp_to_edge);
    float ny = float(faceV.get_height() - 1);
    float3 coordinate = float3(position.x, (position.y * ny + 0.5) / float(faceV.get_height()), position.z);
    return float(faceV.sample(linearVolume, clamp(coordinate, float3(0.001), float3(0.999))).r);
}

float sampleMacW(texture3d<half, access::sample> faceW, float3 position) {
    constexpr sampler linearVolume(filter::linear, mip_filter::none, address::clamp_to_edge);
    float nz = float(faceW.get_depth() - 1);
    float3 coordinate = float3(position.x, position.y, (position.z * nz + 0.5) / float(faceW.get_depth()));
    return float(faceW.sample(linearVolume, clamp(coordinate, float3(0.001), float3(0.999))).r);
}

float3 sampleMacVelocity(
    texture3d<half, access::sample> faceU,
    texture3d<half, access::sample> faceV,
    texture3d<half, access::sample> faceW,
    float3 position) {
    return float3(sampleMacU(faceU, position), sampleMacV(faceV, position), sampleMacW(faceW, position));
}

float faceAt(texture3d<half, access::read> field, int3 face) {
    int3 maximum = int3(field.get_width() - 1, field.get_height() - 1, field.get_depth() - 1);
    return float(field.read(uint3(clamp(face, int3(0), maximum))).r);
}

uint macSubstepCount(device atomic_uint* control) {
    return max(1u, atomic_load_explicit(&control[1], memory_order_relaxed));
}

float macSubstepDt(device atomic_uint* control) {
    return as_type<float>(atomic_load_explicit(&control[2], memory_order_relaxed));
}

bool macSubstepActive(constant Uniforms& uniforms, device atomic_uint* control) {
    return uint(max(uniforms.padding.y, 0.0)) < macSubstepCount(control);
}

kernel void seedMacVelocity(
    texture3d<half, access::write> faceU [[texture(0)]],
    texture3d<half, access::write> faceV [[texture(1)]],
    texture3d<half, access::write> faceW [[texture(2)]],
    constant Uniforms& uniforms [[buffer(0)]],
    uint3 face [[thread_position_in_grid]]) {
    if (face.x < faceU.get_width() && face.y < faceU.get_height() && face.z < faceU.get_depth()) {
        faceU.write(half4(0.0h), face);
    }
    if (face.x < faceV.get_width() && face.y < faceV.get_height() && face.z < faceV.get_depth()) {
        float nx = float(faceV.get_width());
        float ny = float(faceV.get_height() - 1);
        float nz = float(faceV.get_depth());
        float3 position = float3((float(face.x) + 0.5) / nx, float(face.y) / ny, (float(face.z) + 0.5) / nz);
        float2 emitterDelta = position.xz - float2(0.5, 0.5);
        float plume = exp(-80.0 * dot(emitterDelta, emitterDelta)) *
            exp(-220.0 * (position.y - 0.08) * (position.y - 0.08));
        float velocity = 0.35 * max(uniforms.renderParameters.x, 0.0) * plume;
        if (face.y == 0 || face.y + 1 == faceV.get_height()) velocity = 0.0;
        faceV.write(half4(half(velocity), 0.0h, 0.0h, 1.0h), face);
    }
    if (face.x < faceW.get_width() && face.y < faceW.get_height() && face.z < faceW.get_depth()) {
        faceW.write(half4(0.0h), face);
    }
}

kernel void reduceMacMaximumSpeed(
    texture3d<half, access::read> faceU [[texture(0)]],
    texture3d<half, access::read> faceV [[texture(1)]],
    texture3d<half, access::read> faceW [[texture(2)]],
    device atomic_uint* control [[buffer(0)]],
    uint3 cell [[thread_position_in_grid]]) {
    uint nx = faceV.get_width();
    uint ny = faceU.get_height();
    uint nz = faceU.get_depth();
    if (cell.x >= nx || cell.y >= ny || cell.z >= nz) return;
    int3 p = int3(cell);
    float3 velocity = 0.5 * float3(
        faceAt(faceU, p) + faceAt(faceU, p + int3(1, 0, 0)),
        faceAt(faceV, p) + faceAt(faceV, p + int3(0, 1, 0)),
        faceAt(faceW, p) + faceAt(faceW, p + int3(0, 0, 1)));
    atomic_fetch_max_explicit(&control[0], as_type<uint>(length(velocity)), memory_order_relaxed);
}

kernel void finalizeMacCfl(
    device atomic_uint* control [[buffer(0)]],
    constant Uniforms& uniforms [[buffer(1)]],
    uint index [[thread_position_in_grid]]) {
    if (index != 0) return;
    float maximumSpeed = as_type<float>(atomic_load_explicit(&control[0], memory_order_relaxed));
    float frameDt = max(abs(uniforms.control.w), 1.0 / 1000.0);
    float maximumDimension = max(uniforms.padding.w, 1.0);
    float courant = maximumSpeed * frameDt * maximumDimension;
    uint substeps = uint(clamp(ceil(courant / 0.75), 1.0, 2.0));
    atomic_store_explicit(&control[1], substeps, memory_order_relaxed);
    atomic_store_explicit(&control[2], as_type<uint>(frameDt / float(substeps)), memory_order_relaxed);
    atomic_store_explicit(&control[3], as_type<uint>(courant), memory_order_relaxed);
}

bool solidAt(texture3d<half, access::read> obstacles, int3 cell) {
    int3 dimensions = int3(obstacles.get_width(), obstacles.get_height(), obstacles.get_depth());
    if (any(cell < int3(0)) || any(cell >= dimensions)) return true;
    return float(obstacles.read(uint3(cell)).r) > 0.5;
}

kernel void seedMacFields(
    texture3d<half, access::write> density [[texture(0)]],
    texture3d<half, access::write> temperature [[texture(1)]],
    texture3d<half, access::write> obstacles [[texture(2)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= density.get_width() || cell.y >= density.get_height() || cell.z >= density.get_depth()) return;
    float3 dimensions = float3(density.get_width(), density.get_height(), density.get_depth());
    float3 position = (float3(cell) + 0.5) / dimensions;
    float2 emitterDelta = position.xz - float2(0.5, 0.5);
    float emitter = exp(-75.0 * dot(emitterDelta, emitterDelta)) *
        exp(-260.0 * (position.y - 0.075) * (position.y - 0.075));
    float3 obstacleDelta = (position - float3(0.66, 0.30, 0.50)) / float3(1.0, 1.25, 1.0);
    bool obstacle = dot(obstacleDelta, obstacleDelta) < 0.008 || position.y < 0.012;
    density.write(half4(half(emitter * 0.45), 0.0h, 0.0h, 1.0h), cell);
    temperature.write(half4(half(emitter), 0.0h, 0.0h, 1.0h), cell);
    obstacles.write(half4(half(obstacle ? 1.0 : 0.0), 0.0h, 0.0h, 1.0h), cell);
}

kernel void advectMacVelocity(
    texture3d<half, access::sample> sourceU [[texture(0)]],
    texture3d<half, access::sample> sourceV [[texture(1)]],
    texture3d<half, access::sample> sourceW [[texture(2)]],
    texture3d<half, access::sample> density [[texture(3)]],
    texture3d<half, access::sample> temperature [[texture(4)]],
    texture3d<half, access::write> targetU [[texture(5)]],
    texture3d<half, access::write> targetV [[texture(6)]],
    texture3d<half, access::write> targetW [[texture(7)]],
    constant Uniforms& uniforms [[buffer(0)]],
    device atomic_uint* cflControl [[buffer(1)]],
    uint3 face [[thread_position_in_grid]]) {
    constexpr sampler linearVolume(filter::linear, mip_filter::none, address::clamp_to_edge);
    float nx = float(targetU.get_width() - 1);
    float ny = float(targetV.get_height() - 1);
    float nz = float(targetW.get_depth() - 1);
    float dt = macSubstepDt(cflControl);
    bool active = macSubstepActive(uniforms, cflControl);

    if (face.x < targetU.get_width() && face.y < targetU.get_height() && face.z < targetU.get_depth()) {
        float3 position = float3(float(face.x) / nx, (float(face.y) + 0.5) / ny, (float(face.z) + 0.5) / nz);
        float advected = sampleMacU(sourceU, position);
        if (active) {
            float3 velocity = sampleMacVelocity(sourceU, sourceV, sourceW, position);
            float3 midpoint = clamp(position - 0.5 * dt * velocity, float3(0.001), float3(0.999));
            float3 backtraced = clamp(position - dt * sampleMacVelocity(sourceU, sourceV, sourceW, midpoint), float3(0.001), float3(0.999));
            advected = sampleMacU(sourceU, backtraced) * 0.999;
        }
        float3 centered = position - 0.5;
        if (active) advected += -centered.z * 0.012 * max(uniforms.renderParameters.y, 0.0);
        if (face.x == 0 || face.x + 1 == targetU.get_width()) advected = 0.0;
        targetU.write(half4(half(clamp(advected, -2.0, 2.0)), 0.0h, 0.0h, 1.0h), face);
    }
    if (face.x < targetV.get_width() && face.y < targetV.get_height() && face.z < targetV.get_depth()) {
        float3 position = float3((float(face.x) + 0.5) / nx, float(face.y) / ny, (float(face.z) + 0.5) / nz);
        float advected = sampleMacV(sourceV, position);
        if (active) {
            float3 velocity = sampleMacVelocity(sourceU, sourceV, sourceW, position);
            float3 midpoint = clamp(position - 0.5 * dt * velocity, float3(0.001), float3(0.999));
            float3 backtraced = clamp(position - dt * sampleMacVelocity(sourceU, sourceV, sourceW, midpoint), float3(0.001), float3(0.999));
            advected = sampleMacV(sourceV, backtraced) * 0.999;
        }
        float smoke = float(density.sample(linearVolume, position).r);
        float heat = float(temperature.sample(linearVolume, position).r);
        if (active) advected += dt * max(uniforms.renderParameters.x, 0.0) * (2.4 * heat - 0.55 * smoke);
        if (face.y == 0 || face.y + 1 == targetV.get_height()) advected = 0.0;
        targetV.write(half4(half(clamp(advected, -2.0, 2.0)), 0.0h, 0.0h, 1.0h), face);
    }
    if (face.x < targetW.get_width() && face.y < targetW.get_height() && face.z < targetW.get_depth()) {
        float3 position = float3((float(face.x) + 0.5) / nx, (float(face.y) + 0.5) / ny, float(face.z) / nz);
        float advected = sampleMacW(sourceW, position);
        if (active) {
            float3 velocity = sampleMacVelocity(sourceU, sourceV, sourceW, position);
            float3 midpoint = clamp(position - 0.5 * dt * velocity, float3(0.001), float3(0.999));
            float3 backtraced = clamp(position - dt * sampleMacVelocity(sourceU, sourceV, sourceW, midpoint), float3(0.001), float3(0.999));
            advected = sampleMacW(sourceW, backtraced) * 0.999;
        }
        float3 centered = position - 0.5;
        if (active) advected += centered.x * 0.012 * max(uniforms.renderParameters.y, 0.0);
        if (face.z == 0 || face.z + 1 == targetW.get_depth()) advected = 0.0;
        targetW.write(half4(half(clamp(advected, -2.0, 2.0)), 0.0h, 0.0h, 1.0h), face);
    }
}

kernel void computeMacDivergence(
    texture3d<half, access::read> faceU [[texture(0)]],
    texture3d<half, access::read> faceV [[texture(1)]],
    texture3d<half, access::read> faceW [[texture(2)]],
    texture3d<half, access::read> obstacles [[texture(3)]],
    texture3d<half, access::write> divergence [[texture(4)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= divergence.get_width() || cell.y >= divergence.get_height() || cell.z >= divergence.get_depth()) return;
    if (solidAt(obstacles, int3(cell))) {
        divergence.write(half4(0.0h), cell);
        return;
    }
    int3 p = int3(cell);
    float dx = faceAt(faceU, p + int3(1, 0, 0)) - faceAt(faceU, p);
    float dy = faceAt(faceV, p + int3(0, 1, 0)) - faceAt(faceV, p);
    float dz = faceAt(faceW, p + int3(0, 0, 1)) - faceAt(faceW, p);
    float value = dx * float(divergence.get_width()) + dy * float(divergence.get_height()) +
        dz * float(divergence.get_depth());
    divergence.write(half4(half(value), 0.0h, 0.0h, 1.0h), cell);
}

float pressureNeighbour(
    texture3d<half, access::read> pressure,
    texture3d<half, access::read> obstacles,
    int3 center,
    int3 offset) {
    return solidAt(obstacles, center + offset) ? scalarAt(pressure, center) : scalarAt(pressure, center + offset);
}

kernel void solveMacPressureJacobi(
    texture3d<half, access::read> sourcePressure [[texture(0)]],
    texture3d<half, access::read> divergence [[texture(1)]],
    texture3d<half, access::read> obstacles [[texture(2)]],
    texture3d<half, access::write> targetPressure [[texture(3)]],
    constant Uniforms& uniforms [[buffer(0)]],
    device atomic_uint* cflControl [[buffer(1)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= targetPressure.get_width() || cell.y >= targetPressure.get_height() || cell.z >= targetPressure.get_depth()) return;
    int3 p = int3(cell);
    if (!macSubstepActive(uniforms, cflControl)) {
        targetPressure.write(half4(half(scalarAt(sourcePressure, p)), 0.0h, 0.0h, 1.0h), cell);
        return;
    }
    if (solidAt(obstacles, p)) {
        targetPressure.write(half4(0.0h), cell);
        return;
    }
    float cx = float(targetPressure.get_width() * targetPressure.get_width());
    float cy = float(targetPressure.get_height() * targetPressure.get_height());
    float cz = float(targetPressure.get_depth() * targetPressure.get_depth());
    float neighbours = cx * (pressureNeighbour(sourcePressure, obstacles, p, int3(1, 0, 0)) +
                             pressureNeighbour(sourcePressure, obstacles, p, int3(-1, 0, 0))) +
        cy * (pressureNeighbour(sourcePressure, obstacles, p, int3(0, 1, 0)) +
              pressureNeighbour(sourcePressure, obstacles, p, int3(0, -1, 0))) +
        cz * (pressureNeighbour(sourcePressure, obstacles, p, int3(0, 0, 1)) +
              pressureNeighbour(sourcePressure, obstacles, p, int3(0, 0, -1)));
    float dt = macSubstepDt(cflControl);
    float value = (neighbours - scalarAt(divergence, p) / dt) / (2.0 * (cx + cy + cz));
    targetPressure.write(half4(half(value), 0.0h, 0.0h, 1.0h), cell);
}

kernel void computeMacPressureResidual(
    texture3d<half, access::read> pressure [[texture(0)]],
    texture3d<half, access::read> divergence [[texture(1)]],
    texture3d<half, access::read> obstacles [[texture(2)]],
    texture3d<half, access::write> residual [[texture(3)]],
    constant Uniforms& uniforms [[buffer(0)]],
    device atomic_uint* cflControl [[buffer(1)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= residual.get_width() || cell.y >= residual.get_height() || cell.z >= residual.get_depth()) return;
    if (!macSubstepActive(uniforms, cflControl)) return;
    int3 p = int3(cell);
    if (solidAt(obstacles, p)) {
        residual.write(half4(0.0h), cell);
        return;
    }
    float center = scalarAt(pressure, p);
    float cx = float(residual.get_width() * residual.get_width());
    float cy = float(residual.get_height() * residual.get_height());
    float cz = float(residual.get_depth() * residual.get_depth());
    float laplacian = cx * (pressureNeighbour(pressure, obstacles, p, int3(1, 0, 0)) +
                            pressureNeighbour(pressure, obstacles, p, int3(-1, 0, 0)) - 2.0 * center) +
        cy * (pressureNeighbour(pressure, obstacles, p, int3(0, 1, 0)) +
              pressureNeighbour(pressure, obstacles, p, int3(0, -1, 0)) - 2.0 * center) +
        cz * (pressureNeighbour(pressure, obstacles, p, int3(0, 0, 1)) +
              pressureNeighbour(pressure, obstacles, p, int3(0, 0, -1)) - 2.0 * center);
    float dt = macSubstepDt(cflControl);
    residual.write(half4(half(laplacian - scalarAt(divergence, p) / dt), 0.0h, 0.0h, 1.0h), cell);
}

kernel void restrictMacResidual(
    texture3d<half, access::read> fineResidual [[texture(0)]],
    texture3d<half, access::read> fineObstacles [[texture(1)]],
    texture3d<half, access::write> coarseRhs [[texture(2)]],
    texture3d<half, access::write> coarseObstacles [[texture(3)]],
    uint3 coarseCell [[thread_position_in_grid]]) {
    if (coarseCell.x >= coarseRhs.get_width() || coarseCell.y >= coarseRhs.get_height() ||
        coarseCell.z >= coarseRhs.get_depth()) return;
    int3 base = int3(coarseCell) * 2;
    float sum = 0.0;
    float samples = 0.0;
    uint solidCount = 0;
    for (uint z = 0; z < 2; ++z) {
        for (uint y = 0; y < 2; ++y) {
            for (uint x = 0; x < 2; ++x) {
                int3 fineCell = base + int3(x, y, z);
                if (solidAt(fineObstacles, fineCell)) {
                    ++solidCount;
                } else {
                    // The stored fine residual is A*p-b, while the correction
                    // equation is A*e=b-A*p.
                    sum -= scalarAt(fineResidual, fineCell);
                    samples += 1.0;
                }
            }
        }
    }
    bool coarseSolid = solidCount >= 4;
    float rhs = coarseSolid || samples == 0.0 ? 0.0 : sum / samples;
    coarseRhs.write(half4(half(rhs), 0.0h, 0.0h, 1.0h), coarseCell);
    coarseObstacles.write(half4(half(coarseSolid ? 1.0 : 0.0), 0.0h, 0.0h, 1.0h), coarseCell);
}

kernel void solveMacCorrectionJacobi(
    texture3d<half, access::read> sourceCorrection [[texture(0)]],
    texture3d<half, access::read> coarseRhs [[texture(1)]],
    texture3d<half, access::read> coarseObstacles [[texture(2)]],
    texture3d<half, access::write> targetCorrection [[texture(3)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= targetCorrection.get_width() || cell.y >= targetCorrection.get_height() ||
        cell.z >= targetCorrection.get_depth()) return;
    int3 p = int3(cell);
    if (solidAt(coarseObstacles, p)) {
        targetCorrection.write(half4(0.0h), cell);
        return;
    }
    float cx = float(targetCorrection.get_width() * targetCorrection.get_width());
    float cy = float(targetCorrection.get_height() * targetCorrection.get_height());
    float cz = float(targetCorrection.get_depth() * targetCorrection.get_depth());
    float neighbours = cx * (pressureNeighbour(sourceCorrection, coarseObstacles, p, int3(1, 0, 0)) +
                             pressureNeighbour(sourceCorrection, coarseObstacles, p, int3(-1, 0, 0))) +
        cy * (pressureNeighbour(sourceCorrection, coarseObstacles, p, int3(0, 1, 0)) +
              pressureNeighbour(sourceCorrection, coarseObstacles, p, int3(0, -1, 0))) +
        cz * (pressureNeighbour(sourceCorrection, coarseObstacles, p, int3(0, 0, 1)) +
              pressureNeighbour(sourceCorrection, coarseObstacles, p, int3(0, 0, -1)));
    float value = (neighbours - scalarAt(coarseRhs, p)) / (2.0 * (cx + cy + cz));
    targetCorrection.write(half4(half(value), 0.0h, 0.0h, 1.0h), cell);
}

kernel void prolongateMacCorrection(
    texture3d<half, access::read> finePressure [[texture(0)]],
    texture3d<half, access::sample> coarseCorrection [[texture(1)]],
    texture3d<half, access::read> fineObstacles [[texture(2)]],
    texture3d<half, access::write> targetPressure [[texture(3)]],
    constant Uniforms& uniforms [[buffer(0)]],
    device atomic_uint* cflControl [[buffer(1)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= targetPressure.get_width() || cell.y >= targetPressure.get_height() ||
        cell.z >= targetPressure.get_depth()) return;
    if (solidAt(fineObstacles, int3(cell))) {
        targetPressure.write(half4(0.0h), cell);
        return;
    }
    if (!macSubstepActive(uniforms, cflControl)) {
        targetPressure.write(half4(half(scalarAt(finePressure, int3(cell))), 0.0h, 0.0h, 1.0h), cell);
        return;
    }
    constexpr sampler linearVolume(filter::linear, mip_filter::none, address::clamp_to_edge);
    float3 coordinate = (float3(cell) + 0.5) /
        float3(targetPressure.get_width(), targetPressure.get_height(), targetPressure.get_depth());
    float correction = float(coarseCorrection.sample(linearVolume, coordinate).r);
    targetPressure.write(half4(half(scalarAt(finePressure, int3(cell)) + correction), 0.0h, 0.0h, 1.0h), cell);
}

kernel void projectMacVelocity(
    texture3d<half, access::read> sourceU [[texture(0)]],
    texture3d<half, access::read> sourceV [[texture(1)]],
    texture3d<half, access::read> sourceW [[texture(2)]],
    texture3d<half, access::read> pressure [[texture(3)]],
    texture3d<half, access::read> obstacles [[texture(4)]],
    texture3d<half, access::write> targetU [[texture(5)]],
    texture3d<half, access::write> targetV [[texture(6)]],
    texture3d<half, access::write> targetW [[texture(7)]],
    constant Uniforms& uniforms [[buffer(0)]],
    device atomic_uint* cflControl [[buffer(1)]],
    uint3 face [[thread_position_in_grid]]) {
    int nx = int(pressure.get_width());
    int ny = int(pressure.get_height());
    int nz = int(pressure.get_depth());
    float dt = macSubstepDt(cflControl);
    bool active = macSubstepActive(uniforms, cflControl);
    if (face.x < targetU.get_width() && face.y < targetU.get_height() && face.z < targetU.get_depth()) {
        int3 right = int3(face);
        int3 left = right - int3(1, 0, 0);
        float value = faceAt(sourceU, int3(face));
        if (active) {
            if (face.x == 0 || face.x == uint(nx) || solidAt(obstacles, left) || solidAt(obstacles, right)) value = 0.0;
            else value -= dt * float(nx) * (scalarAt(pressure, right) - scalarAt(pressure, left));
        }
        targetU.write(half4(half(value), 0.0h, 0.0h, 1.0h), face);
    }
    if (face.x < targetV.get_width() && face.y < targetV.get_height() && face.z < targetV.get_depth()) {
        int3 top = int3(face);
        int3 bottom = top - int3(0, 1, 0);
        float value = faceAt(sourceV, int3(face));
        if (active) {
            if (face.y == 0 || face.y == uint(ny) || solidAt(obstacles, bottom) || solidAt(obstacles, top)) value = 0.0;
            else value -= dt * float(ny) * (scalarAt(pressure, top) - scalarAt(pressure, bottom));
        }
        targetV.write(half4(half(value), 0.0h, 0.0h, 1.0h), face);
    }
    if (face.x < targetW.get_width() && face.y < targetW.get_height() && face.z < targetW.get_depth()) {
        int3 front = int3(face);
        int3 back = front - int3(0, 0, 1);
        float value = faceAt(sourceW, int3(face));
        if (active) {
            if (face.z == 0 || face.z == uint(nz) || solidAt(obstacles, back) || solidAt(obstacles, front)) value = 0.0;
            else value -= dt * float(nz) * (scalarAt(pressure, front) - scalarAt(pressure, back));
        }
        targetW.write(half4(half(value), 0.0h, 0.0h, 1.0h), face);
    }
}

kernel void advectMacScalar(
    texture3d<half, access::sample> source [[texture(0)]],
    texture3d<half, access::sample> faceU [[texture(1)]],
    texture3d<half, access::sample> faceV [[texture(2)]],
    texture3d<half, access::sample> faceW [[texture(3)]],
    texture3d<half, access::write> target [[texture(4)]],
    constant Uniforms& uniforms [[buffer(0)]],
    device atomic_uint* cflControl [[buffer(1)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= target.get_width() || cell.y >= target.get_height() || cell.z >= target.get_depth()) return;
    constexpr sampler linearVolume(filter::linear, mip_filter::none, address::clamp_to_edge);
    float3 dimensions = float3(target.get_width(), target.get_height(), target.get_depth());
    float3 position = (float3(cell) + 0.5) / dimensions;
    float dt = macSubstepDt(cflControl) * (uniforms.padding.z < 0.0 ? -1.0 : 1.0);
    if (!macSubstepActive(uniforms, cflControl)) dt = 0.0;
    float3 velocity = sampleMacVelocity(faceU, faceV, faceW, position);
    float3 midpoint = clamp(position - 0.5 * dt * velocity, float3(0.001), float3(0.999));
    float3 backtraced = clamp(position - dt * sampleMacVelocity(faceU, faceV, faceW, midpoint), float3(0.001), float3(0.999));
    target.write(half4(source.sample(linearVolume, backtraced).r, 0.0h, 0.0h, 1.0h), cell);
}

kernel void correctMacScalar(
    texture3d<half, access::read> original [[texture(0)]],
    texture3d<half, access::read> forward [[texture(1)]],
    texture3d<half, access::read> backward [[texture(2)]],
    texture3d<half, access::read> obstacles [[texture(3)]],
    texture3d<half, access::write> target [[texture(4)]],
    constant Uniforms& uniforms [[buffer(0)]],
    device atomic_uint* cflControl [[buffer(1)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= target.get_width() || cell.y >= target.get_height() || cell.z >= target.get_depth()) return;
    int3 p = int3(cell);
    if (!macSubstepActive(uniforms, cflControl)) {
        target.write(half4(half(scalarAt(original, p)), 0.0h, 0.0h, 1.0h), cell);
        return;
    }
    if (solidAt(obstacles, p)) {
        target.write(half4(0.0h), cell);
        return;
    }
    float source = scalarAt(original, p);
    float corrected = scalarAt(forward, p) + 0.5 * (source - scalarAt(backward, p));
    float minimumValue = source;
    float maximumValue = source;
    constexpr int3 offsets[6] = { int3(1, 0, 0), int3(-1, 0, 0), int3(0, 1, 0),
                                  int3(0, -1, 0), int3(0, 0, 1), int3(0, 0, -1) };
    for (uint index = 0; index < 6; ++index) {
        float neighbour = scalarAt(original, p + offsets[index]);
        minimumValue = min(minimumValue, neighbour);
        maximumValue = max(maximumValue, neighbour);
    }
    corrected = clamp(corrected, minimumValue, maximumValue);
    float3 dimensions = float3(target.get_width(), target.get_height(), target.get_depth());
    float3 position = (float3(cell) + 0.5) / dimensions;
    float2 emitterDelta = position.xz - float2(0.5, 0.5);
    float emitter = exp(-72.0 * dot(emitterDelta, emitterDelta)) *
        exp(-250.0 * (position.y - 0.075) * (position.y - 0.075));
    float turbulence = 0.78 + 0.22 * sin(19.0 * position.x + 13.0 * position.z + 2.3 * uniforms.time);
    if (uniforms.padding.x < 0.5) corrected = corrected * 0.997 + emitter * turbulence * 0.08;
    else corrected = corrected * 0.992 + emitter * 0.14;
    target.write(half4(half(clamp(corrected, 0.0, 1.0)), 0.0h, 0.0h, 1.0h), cell);
}

float3 macVelocityAtCell(
    texture3d<half, access::read> faceU,
    texture3d<half, access::read> faceV,
    texture3d<half, access::read> faceW,
    int3 cell) {
    return 0.5 * float3(
        faceAt(faceU, cell) + faceAt(faceU, cell + int3(1, 0, 0)),
        faceAt(faceV, cell) + faceAt(faceV, cell + int3(0, 1, 0)),
        faceAt(faceW, cell) + faceAt(faceW, cell + int3(0, 0, 1)));
}

kernel void computeMacCurl(
    texture3d<half, access::read> faceU [[texture(0)]],
    texture3d<half, access::read> faceV [[texture(1)]],
    texture3d<half, access::read> faceW [[texture(2)]],
    texture3d<half, access::read> obstacles [[texture(3)]],
    texture3d<half, access::write> curl [[texture(4)]],
    uint3 cell [[thread_position_in_grid]]) {
    if (cell.x >= curl.get_width() || cell.y >= curl.get_height() || cell.z >= curl.get_depth()) return;
    int3 p = int3(cell);
    if (solidAt(obstacles, p)) {
        curl.write(half4(0.0h), cell);
        return;
    }
    float3 vx0 = macVelocityAtCell(faceU, faceV, faceW, p - int3(1, 0, 0));
    float3 vx1 = macVelocityAtCell(faceU, faceV, faceW, p + int3(1, 0, 0));
    float3 vy0 = macVelocityAtCell(faceU, faceV, faceW, p - int3(0, 1, 0));
    float3 vy1 = macVelocityAtCell(faceU, faceV, faceW, p + int3(0, 1, 0));
    float3 vz0 = macVelocityAtCell(faceU, faceV, faceW, p - int3(0, 0, 1));
    float3 vz1 = macVelocityAtCell(faceU, faceV, faceW, p + int3(0, 0, 1));
    float3 derivativeX = 0.5 * float(curl.get_width()) * (vx1 - vx0);
    float3 derivativeY = 0.5 * float(curl.get_height()) * (vy1 - vy0);
    float3 derivativeZ = 0.5 * float(curl.get_depth()) * (vz1 - vz0);
    float3 value = float3(derivativeY.z - derivativeZ.y,
                          derivativeZ.x - derivativeX.z,
                          derivativeX.y - derivativeY.x);
    curl.write(half4(half3(value), half(length(value))), cell);
}

bool intersectUnitVolume(float3 origin, float3 direction, thread float& entry, thread float& exit) {
    float3 inverseDirection = 1.0 / direction;
    float3 first = (-1.0 - origin) * inverseDirection;
    float3 second = (1.0 - origin) * inverseDirection;
    float3 nearPlane = min(first, second);
    float3 farPlane = max(first, second);
    entry = max(max(nearPlane.x, nearPlane.y), nearPlane.z);
    exit = min(min(farPlane.x, farPlane.y), farPlane.z);
    return exit > max(entry, 0.0);
}

kernel void renderVolume(
    texture3d<half, access::sample> density [[texture(0)]],
    texture3d<half, access::sample> temperature [[texture(1)]],
    texture2d<half, access::write> outputRadiance [[texture(2)]],
    constant Uniforms& u [[buffer(0)]],
    uint2 pixel [[thread_position_in_grid]]) {
    if (pixel.x >= uint(u.width) || pixel.y >= uint(u.height)) return;
    float2 uv = (float2(pixel) + 0.5) / float2(u.width, u.height);
    float aspect = u.width / max(1.0, u.height);
    float3 origin = float3(0.0, 0.1, 3.1);
    float3 direction = normalize(float3((uv.x - 0.5) * 1.65 * aspect, (0.5 - uv.y) * 1.65, -1.9));
    float entry = 0.0;
    float exit = 0.0;
    if (!intersectUnitVolume(origin, direction, entry, exit)) {
        outputRadiance.write(half4(half3(float3(0.002, 0.004, 0.011)), 1.0h), pixel);
        return;
    }
    constexpr sampler volumeSampler(filter::linear, mip_filter::none, address::clamp_to_edge);
    constexpr uint steps = 96;
    float stepLength = (exit - max(entry, 0.0)) / float(steps);
    float travel = max(entry, 0.0) + 0.5 * stepLength;
    float transmittance = 1.0;
    float3 radiance = float3(0.0);
    for (uint step = 0; step < steps && transmittance > 0.005; ++step) {
        float3 position = origin + direction * travel;
        float sampleDensity = float(density.sample(volumeSampler, position * 0.5 + 0.5).r);
        float sampleTemperature = float(temperature.sample(volumeSampler, position * 0.5 + 0.5).r);
        float extinction = sampleDensity * max(u.renderParameters.z, 0.001);
        float emission = sampleDensity * sampleDensity * sampleTemperature *
            (1.0 + 0.35 * sin(6.0 * position.y + u.time)) * max(u.renderParameters.w, 0.0);
        float3 emissionColor = mix(float3(1.35, 0.19, 0.025), float3(0.72, 0.88, 1.55),
                                   clamp(sampleTemperature * 0.75, 0.0, 1.0));
        radiance += transmittance * emissionColor * emission * stepLength;
        transmittance *= exp(-extinction * stepLength);
        travel += stepLength;
    }
    radiance += transmittance * float3(0.002, 0.004, 0.011);
    outputRadiance.write(half4(half3(radiance), 1.0h), pixel);
}

kernel void accumulateRadiance(
    texture2d<half, access::sample> currentRadiance [[texture(0)]],
    texture2d<half, access::sample> historyRadiance [[texture(1)]],
    texture2d<half, access::write> outputRadiance [[texture(2)]],
    constant Uniforms& u [[buffer(0)]],
    uint2 pixel [[thread_position_in_grid]]) {
    if (pixel.x >= uint(u.width) || pixel.y >= uint(u.height)) return;
    constexpr sampler linearSampler(filter::nearest, mip_filter::none, address::clamp_to_edge);
    float2 uv = (float2(pixel) + 0.5) / float2(u.width, u.height);
    float3 current = float3(currentRadiance.sample(linearSampler, uv).rgb);
    float3 history = float3(historyRadiance.sample(linearSampler, uv).rgb);
    float alpha = u.control.y > 0.5 ? 1.0 : 1.0 / max(1.0, u.control.z + 1.0);
    outputRadiance.write(half4(half3(mix(history, current, alpha)), 1.0h), pixel);
}

float3 acesFilm(float3 value) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0, 1.0);
}

fragment float4 display(
    VertexOut in [[stage_in]],
    texture2d<half> radiance [[texture(0)]],
    constant Uniforms& u [[buffer(0)]]) {
    constexpr sampler linearSampler(filter::linear, mip_filter::none, address::clamp_to_edge);
    float3 linear = float3(radiance.sample(linearSampler, in.uv).rgb);
    float2 texel = 1.0 / max(float2(u.width, u.height), float2(1.0));
    constexpr float2 directions[8] = {
        float2(1.0, 0.0), float2(-1.0, 0.0), float2(0.0, 1.0), float2(0.0, -1.0),
        float2(0.7071, 0.7071), float2(-0.7071, 0.7071),
        float2(0.7071, -0.7071), float2(-0.7071, -0.7071)};
    float3 bloom = float3(0.0);
    float weight = 0.0;
    for (uint index = 0; index < 8; ++index) {
        float radius = index < 4 ? 2.5 : 5.0;
        float3 sampleRadiance = float3(radiance.sample(linearSampler, in.uv + directions[index] * texel * radius).rgb);
        float luminance = dot(sampleRadiance, float3(0.2126, 0.7152, 0.0722));
        float highlight = smoothstep(0.8, 2.8, luminance);
        bloom += sampleRadiance * highlight;
        weight += highlight;
    }
    bloom /= max(weight, 1.0);
    float3 glare = float3(0.0);
    for (int offset = -3; offset <= 3; ++offset) {
        if (offset == 0) continue;
        float distance = float(abs(offset));
        float2 diagonal = float2(float(offset)) * texel * 3.0;
        glare += float3(radiance.sample(linearSampler, in.uv + diagonal).rgb) / (distance * distance + 1.0);
    }
    float cinematicWeight = u.control.x > 0.5 ? 1.0 : 0.45;
    linear += cinematicWeight * (0.12 * bloom + 0.018 * glare);
    float2 centered = in.uv * 2.0 - 1.0;
    linear *= 1.0 - 0.12 * smoothstep(0.35, 1.35, dot(centered, centered));
    float dither = fract(sin(dot(in.uv * u.width, float2(12.9898, 78.233))) * 43758.5453) - 0.5;
    return float4(max(acesFilm(linear) + dither / 255.0, 0.0), 1.0);
}
"""

private func runNativeGpuSmoke(
    blackHole: Bool = false, volume: Bool = false,
    expectedVolumeSignature: UInt64? = nil,
    volumeCflStress: Bool = true
) -> Bool {
    guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
        FileHandle.standardError.write(Data("Vulkax Metal smoke failed: no Metal device or queue\n".utf8))
        return false
    }
    do {
        let library = try device.makeLibrary(source: waveShader, options: nil)
        guard let function = library.makeFunction(name: "simulateWave") else {
            FileHandle.standardError.write(Data("Vulkax Metal smoke failed: simulateWave entry point missing\n".utf8))
            return false
        }
        let pipeline = try device.makeComputePipelineState(function: function)
        if volume {
            guard let clearFunction = library.makeFunction(name: "clearScalarVolume"),
                  let seedFunction = library.makeFunction(name: "seedMacFields"),
                  let seedVelocityFunction = library.makeFunction(name: "seedMacVelocity"),
                  let reduceMaximumSpeedFunction = library.makeFunction(name: "reduceMacMaximumSpeed"),
                  let finalizeCflFunction = library.makeFunction(name: "finalizeMacCfl"),
                  let velocityFunction = library.makeFunction(name: "advectMacVelocity"),
                  let divergenceFunction = library.makeFunction(name: "computeMacDivergence"),
                  let pressureFunction = library.makeFunction(name: "solveMacPressureJacobi"),
                  let residualFunction = library.makeFunction(name: "computeMacPressureResidual"),
                  let restrictFunction = library.makeFunction(name: "restrictMacResidual"),
                  let correctionFunction = library.makeFunction(name: "solveMacCorrectionJacobi"),
                  let prolongateFunction = library.makeFunction(name: "prolongateMacCorrection"),
                  let projectionFunction = library.makeFunction(name: "projectMacVelocity"),
                  let scalarAdvectionFunction = library.makeFunction(name: "advectMacScalar"),
                  let scalarCorrectionFunction = library.makeFunction(name: "correctMacScalar"),
                  let curlFunction = library.makeFunction(name: "computeMacCurl"),
                  let renderFunction = library.makeFunction(name: "renderVolume") else {
                FileHandle.standardError.write(Data("Vulkax Metal volume smoke failed: volume entry point missing\n".utf8))
                return false
            }
            let clearScalar = try device.makeComputePipelineState(function: clearFunction)
            let seed = try device.makeComputePipelineState(function: seedFunction)
            let seedVelocity = try device.makeComputePipelineState(function: seedVelocityFunction)
            let reduceMaximumSpeed = try device.makeComputePipelineState(function: reduceMaximumSpeedFunction)
            let finalizeCfl = try device.makeComputePipelineState(function: finalizeCflFunction)
            let velocityAdvection = try device.makeComputePipelineState(function: velocityFunction)
            let divergence = try device.makeComputePipelineState(function: divergenceFunction)
            let pressure = try device.makeComputePipelineState(function: pressureFunction)
            let residual = try device.makeComputePipelineState(function: residualFunction)
            let restrictResidual = try device.makeComputePipelineState(function: restrictFunction)
            let solveCorrection = try device.makeComputePipelineState(function: correctionFunction)
            let prolongateCorrection = try device.makeComputePipelineState(function: prolongateFunction)
            let projection = try device.makeComputePipelineState(function: projectionFunction)
            let scalarAdvection = try device.makeComputePipelineState(function: scalarAdvectionFunction)
            let scalarCorrection = try device.makeComputePipelineState(function: scalarCorrectionFunction)
            let curl = try device.makeComputePipelineState(function: curlFunction)
            let render = try device.makeComputePipelineState(function: renderFunction)

            let nx = 16
            let ny = 20
            let nz = 16
            let makeVolume = { (format: MTLPixelFormat, width: Int, height: Int, depth: Int) -> MTLTexture? in
                let descriptor = MTLTextureDescriptor()
                descriptor.textureType = .type3D
                descriptor.pixelFormat = format
                descriptor.width = width
                descriptor.height = height
                descriptor.depth = depth
                descriptor.usage = [.shaderRead, .shaderWrite]
                descriptor.storageMode = .shared
                return device.makeTexture(descriptor: descriptor)
            }
            let radianceDescriptor = MTLTextureDescriptor.texture2DDescriptor(
                pixelFormat: .rgba16Float, width: 32, height: 32, mipmapped: false)
            radianceDescriptor.usage = [.shaderRead, .shaderWrite]
            radianceDescriptor.storageMode = .shared
            guard let sourceDensity = makeVolume(.r16Float, nx, ny, nz),
                  let targetDensity = makeVolume(.r16Float, nx, ny, nz),
                  let sourceTemperature = makeVolume(.r16Float, nx, ny, nz),
                  let targetTemperature = makeVolume(.r16Float, nx, ny, nz),
                  let scalarForward = makeVolume(.r16Float, nx, ny, nz),
                  let scalarBackward = makeVolume(.r16Float, nx, ny, nz),
                  let sourceU = makeVolume(.r16Float, nx + 1, ny, nz),
                  let sourceV = makeVolume(.r16Float, nx, ny + 1, nz),
                  let sourceW = makeVolume(.r16Float, nx, ny, nz + 1),
                  let advectedU = makeVolume(.r16Float, nx + 1, ny, nz),
                  let advectedV = makeVolume(.r16Float, nx, ny + 1, nz),
                  let advectedW = makeVolume(.r16Float, nx, ny, nz + 1),
                  let projectedU = makeVolume(.r16Float, nx + 1, ny, nz),
                  let projectedV = makeVolume(.r16Float, nx, ny + 1, nz),
                  let projectedW = makeVolume(.r16Float, nx, ny, nz + 1),
                  let divergenceBefore = makeVolume(.r16Float, nx, ny, nz),
                  let diagnosticDivergenceBefore = makeVolume(.r16Float, nx, ny, nz),
                  let divergenceAfter = makeVolume(.r16Float, nx, ny, nz),
                  let pressure0 = makeVolume(.r16Float, nx, ny, nz),
                  let pressure1 = makeVolume(.r16Float, nx, ny, nz),
                  let pressureResidual = makeVolume(.r16Float, nx, ny, nz),
                  let baselinePressure0 = makeVolume(.r16Float, nx, ny, nz),
                  let baselinePressure1 = makeVolume(.r16Float, nx, ny, nz),
                  let baselineResidual = makeVolume(.r16Float, nx, ny, nz),
                  let coarseRhs = makeVolume(.r16Float, nx / 2, ny / 2, nz / 2),
                  let coarseObstacles = makeVolume(.r16Float, nx / 2, ny / 2, nz / 2),
                  let coarseCorrection0 = makeVolume(.r16Float, nx / 2, ny / 2, nz / 2),
                  let coarseCorrection1 = makeVolume(.r16Float, nx / 2, ny / 2, nz / 2),
                  let obstacles = makeVolume(.r16Float, nx, ny, nz),
                  let curlField = makeVolume(.rgba16Float, nx, ny, nz),
                  let radiance = device.makeTexture(descriptor: radianceDescriptor),
                  let cflControl = device.makeBuffer(length: 4 * MemoryLayout<UInt32>.stride, options: .storageModeShared),
                  let command = queue.makeCommandBuffer() else {
                FileHandle.standardError.write(Data("Vulkax Metal volume smoke failed: could not allocate GPU resources\n".utf8))
                return false
            }
            let dispatch3D = { (encoder: MTLComputeCommandEncoder, pipeline: MTLComputePipelineState,
                                width: Int, height: Int, depth: Int) in
                encoder.setComputePipelineState(pipeline)
                encoder.dispatchThreadgroups(
                    MTLSize(width: (width + 3) / 4, height: (height + 3) / 4, depth: (depth + 3) / 4),
                    threadsPerThreadgroup: MTLSize(width: 4, height: 4, depth: 4))
            }
            let frameTimestep: Float = volumeCflStress ? 1.0 / 20.0 : 1.0 / 60.0
            let smokeBuoyancy: Float = volumeCflStress ? 3.0 : 1.0
            var uniforms = WaveUniforms(time: 0.5, amplitude: 1.0, wavenumber: 2.0, angularFrequency: 3.0,
                                        width: 32.0, height: 32.0,
                                        control: SIMD4(2.0, 0.0, 0.0, frameTimestep),
                                        renderParameters: SIMD4(smokeBuoyancy, 1.0, 2.2, 1.0))
            uniforms.padding.w = Float(max(nx, max(ny, nz)))
            memset(cflControl.contents(), 0, cflControl.length)
            for texture in [targetDensity, targetTemperature, scalarForward, scalarBackward,
                            divergenceBefore, diagnosticDivergenceBefore, divergenceAfter, pressure0, pressure1, pressureResidual,
                            baselinePressure0, baselinePressure1, baselineResidual,
                            coarseRhs, coarseObstacles, coarseCorrection0, coarseCorrection1] {
                guard let clear = command.makeComputeCommandEncoder() else { return false }
                clear.setTexture(texture, index: 0)
                dispatch3D(clear, clearScalar, texture.width, texture.height, texture.depth)
                clear.endEncoding()
            }
            for texture in [sourceU, sourceV, sourceW, advectedU, advectedV, advectedW,
                            projectedU, projectedV, projectedW] {
                guard let clear = command.makeComputeCommandEncoder() else { return false }
                clear.setTexture(texture, index: 0)
                dispatch3D(clear, clearScalar, texture.width, texture.height, texture.depth)
                clear.endEncoding()
            }
            guard let seedPass = command.makeComputeCommandEncoder() else { return false }
            seedPass.setTexture(sourceDensity, index: 0)
            seedPass.setTexture(sourceTemperature, index: 1)
            seedPass.setTexture(obstacles, index: 2)
            dispatch3D(seedPass, seed, nx, ny, nz)
            seedPass.endEncoding()
            guard let seedVelocityPass = command.makeComputeCommandEncoder() else { return false }
            seedVelocityPass.setTexture(sourceU, index: 0)
            seedVelocityPass.setTexture(sourceV, index: 1)
            seedVelocityPass.setTexture(sourceW, index: 2)
            seedVelocityPass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            dispatch3D(seedVelocityPass, seedVelocity, nx + 1, ny + 1, nz + 1)
            seedVelocityPass.endEncoding()
            guard let reductionPass = command.makeComputeCommandEncoder() else { return false }
            reductionPass.setTexture(sourceU, index: 0)
            reductionPass.setTexture(sourceV, index: 1)
            reductionPass.setTexture(sourceW, index: 2)
            reductionPass.setBuffer(cflControl, offset: 0, index: 0)
            dispatch3D(reductionPass, reduceMaximumSpeed, nx, ny, nz)
            reductionPass.endEncoding()
            guard let finalizePass = command.makeComputeCommandEncoder() else { return false }
            finalizePass.setComputePipelineState(finalizeCfl)
            finalizePass.setBuffer(cflControl, offset: 0, index: 0)
            finalizePass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 1)
            finalizePass.dispatchThreads(MTLSize(width: 1, height: 1, depth: 1),
                                         threadsPerThreadgroup: MTLSize(width: 1, height: 1, depth: 1))
            finalizePass.endEncoding()
            var currentDensity = sourceDensity
            var currentTemperature = sourceTemperature
            var currentU = sourceU
            var currentV = sourceV
            var currentW = sourceW
            var pressureSource = pressure0
            var baselineSource = baselinePressure0
            for substep in 0..<2 {
            uniforms.padding.y = Float(substep)
            let nextDensity = currentDensity === sourceDensity ? targetDensity : sourceDensity
            let nextTemperature = currentTemperature === sourceTemperature ? targetTemperature : sourceTemperature
            let projectedTargetU = currentU === sourceU ? projectedU : sourceU
            let projectedTargetV = currentV === sourceV ? projectedV : sourceV
            let projectedTargetW = currentW === sourceW ? projectedW : sourceW
            guard let velocityPass = command.makeComputeCommandEncoder() else { return false }
            velocityPass.setTexture(currentU, index: 0)
            velocityPass.setTexture(currentV, index: 1)
            velocityPass.setTexture(currentW, index: 2)
            velocityPass.setTexture(currentDensity, index: 3)
            velocityPass.setTexture(currentTemperature, index: 4)
            velocityPass.setTexture(advectedU, index: 5)
            velocityPass.setTexture(advectedV, index: 6)
            velocityPass.setTexture(advectedW, index: 7)
            velocityPass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            velocityPass.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(velocityPass, velocityAdvection, nx + 1, ny + 1, nz + 1)
            velocityPass.endEncoding()
            guard let divergencePass = command.makeComputeCommandEncoder() else { return false }
            divergencePass.setTexture(advectedU, index: 0)
            divergencePass.setTexture(advectedV, index: 1)
            divergencePass.setTexture(advectedW, index: 2)
            divergencePass.setTexture(obstacles, index: 3)
            divergencePass.setTexture(divergenceBefore, index: 4)
            dispatch3D(divergencePass, divergence, nx, ny, nz)
            divergencePass.endEncoding()
            if substep == 0 {
                guard let diagnosticDivergencePass = command.makeComputeCommandEncoder() else { return false }
                diagnosticDivergencePass.setTexture(advectedU, index: 0)
                diagnosticDivergencePass.setTexture(advectedV, index: 1)
                diagnosticDivergencePass.setTexture(advectedW, index: 2)
                diagnosticDivergencePass.setTexture(obstacles, index: 3)
                diagnosticDivergencePass.setTexture(diagnosticDivergenceBefore, index: 4)
                dispatch3D(diagnosticDivergencePass, divergence, nx, ny, nz)
                diagnosticDivergencePass.endEncoding()
            }
            // Jacobi-only baseline for the pressure-solver ablation.
            for _ in 0..<40 {
                let pressureTarget = baselineSource === baselinePressure0 ? baselinePressure1 : baselinePressure0
                guard let pressurePass = command.makeComputeCommandEncoder() else { return false }
                pressurePass.setTexture(baselineSource, index: 0)
                pressurePass.setTexture(divergenceBefore, index: 1)
                pressurePass.setTexture(obstacles, index: 2)
                pressurePass.setTexture(pressureTarget, index: 3)
                pressurePass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                pressurePass.setBuffer(cflControl, offset: 0, index: 1)
                dispatch3D(pressurePass, pressure, nx, ny, nz)
                pressurePass.endEncoding()
                baselineSource = pressureTarget
            }
            guard let baselineResidualPass = command.makeComputeCommandEncoder() else { return false }
            baselineResidualPass.setTexture(baselineSource, index: 0)
            baselineResidualPass.setTexture(divergenceBefore, index: 1)
            baselineResidualPass.setTexture(obstacles, index: 2)
            baselineResidualPass.setTexture(baselineResidual, index: 3)
            baselineResidualPass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            baselineResidualPass.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(baselineResidualPass, residual, nx, ny, nz)
            baselineResidualPass.endEncoding()

            // One two-level V-cycle: pre-smooth, restrict residual, solve a
            // coarse correction, prolongate, and post-smooth.
            for _ in 0..<8 {
                let pressureTarget = pressureSource === pressure0 ? pressure1 : pressure0
                guard let pressurePass = command.makeComputeCommandEncoder() else { return false }
                pressurePass.setTexture(pressureSource, index: 0)
                pressurePass.setTexture(divergenceBefore, index: 1)
                pressurePass.setTexture(obstacles, index: 2)
                pressurePass.setTexture(pressureTarget, index: 3)
                pressurePass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                pressurePass.setBuffer(cflControl, offset: 0, index: 1)
                dispatch3D(pressurePass, pressure, nx, ny, nz)
                pressurePass.endEncoding()
                pressureSource = pressureTarget
            }
            guard let preResidualPass = command.makeComputeCommandEncoder() else { return false }
            preResidualPass.setTexture(pressureSource, index: 0)
            preResidualPass.setTexture(divergenceBefore, index: 1)
            preResidualPass.setTexture(obstacles, index: 2)
            preResidualPass.setTexture(pressureResidual, index: 3)
            preResidualPass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            preResidualPass.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(preResidualPass, residual, nx, ny, nz)
            preResidualPass.endEncoding()
            guard let restrictionPass = command.makeComputeCommandEncoder() else { return false }
            restrictionPass.setTexture(pressureResidual, index: 0)
            restrictionPass.setTexture(obstacles, index: 1)
            restrictionPass.setTexture(coarseRhs, index: 2)
            restrictionPass.setTexture(coarseObstacles, index: 3)
            dispatch3D(restrictionPass, restrictResidual, nx / 2, ny / 2, nz / 2)
            restrictionPass.endEncoding()
            var coarseSource = coarseCorrection0
            for _ in 0..<24 {
                let coarseTarget = coarseSource === coarseCorrection0 ? coarseCorrection1 : coarseCorrection0
                guard let correctionPass = command.makeComputeCommandEncoder() else { return false }
                correctionPass.setTexture(coarseSource, index: 0)
                correctionPass.setTexture(coarseRhs, index: 1)
                correctionPass.setTexture(coarseObstacles, index: 2)
                correctionPass.setTexture(coarseTarget, index: 3)
                dispatch3D(correctionPass, solveCorrection, nx / 2, ny / 2, nz / 2)
                correctionPass.endEncoding()
                coarseSource = coarseTarget
            }
            let prolongedPressure = pressureSource === pressure0 ? pressure1 : pressure0
            guard let prolongationPass = command.makeComputeCommandEncoder() else { return false }
            prolongationPass.setTexture(pressureSource, index: 0)
            prolongationPass.setTexture(coarseSource, index: 1)
            prolongationPass.setTexture(obstacles, index: 2)
            prolongationPass.setTexture(prolongedPressure, index: 3)
            prolongationPass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            prolongationPass.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(prolongationPass, prolongateCorrection, nx, ny, nz)
            prolongationPass.endEncoding()
            pressureSource = prolongedPressure
            for _ in 0..<8 {
                let pressureTarget = pressureSource === pressure0 ? pressure1 : pressure0
                guard let pressurePass = command.makeComputeCommandEncoder() else { return false }
                pressurePass.setTexture(pressureSource, index: 0)
                pressurePass.setTexture(divergenceBefore, index: 1)
                pressurePass.setTexture(obstacles, index: 2)
                pressurePass.setTexture(pressureTarget, index: 3)
                pressurePass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                pressurePass.setBuffer(cflControl, offset: 0, index: 1)
                dispatch3D(pressurePass, pressure, nx, ny, nz)
                pressurePass.endEncoding()
                pressureSource = pressureTarget
            }
            guard let residualPass = command.makeComputeCommandEncoder() else { return false }
            residualPass.setTexture(pressureSource, index: 0)
            residualPass.setTexture(divergenceBefore, index: 1)
            residualPass.setTexture(obstacles, index: 2)
            residualPass.setTexture(pressureResidual, index: 3)
            residualPass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            residualPass.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(residualPass, residual, nx, ny, nz)
            residualPass.endEncoding()
            guard let projectionPass = command.makeComputeCommandEncoder() else { return false }
            projectionPass.setTexture(advectedU, index: 0)
            projectionPass.setTexture(advectedV, index: 1)
            projectionPass.setTexture(advectedW, index: 2)
            projectionPass.setTexture(pressureSource, index: 3)
            projectionPass.setTexture(obstacles, index: 4)
            projectionPass.setTexture(projectedTargetU, index: 5)
            projectionPass.setTexture(projectedTargetV, index: 6)
            projectionPass.setTexture(projectedTargetW, index: 7)
            projectionPass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            projectionPass.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(projectionPass, projection, nx + 1, ny + 1, nz + 1)
            projectionPass.endEncoding()
            guard let postDivergencePass = command.makeComputeCommandEncoder() else { return false }
            postDivergencePass.setTexture(projectedTargetU, index: 0)
            postDivergencePass.setTexture(projectedTargetV, index: 1)
            postDivergencePass.setTexture(projectedTargetW, index: 2)
            postDivergencePass.setTexture(obstacles, index: 3)
            postDivergencePass.setTexture(divergenceAfter, index: 4)
            dispatch3D(postDivergencePass, divergence, nx, ny, nz)
            postDivergencePass.endEncoding()
            guard let curlPass = command.makeComputeCommandEncoder() else { return false }
            curlPass.setTexture(projectedTargetU, index: 0)
            curlPass.setTexture(projectedTargetV, index: 1)
            curlPass.setTexture(projectedTargetW, index: 2)
            curlPass.setTexture(obstacles, index: 3)
            curlPass.setTexture(curlField, index: 4)
            dispatch3D(curlPass, curl, nx, ny, nz)
            curlPass.endEncoding()

            let advectScalar = { (source: MTLTexture, target: MTLTexture, kind: Float) -> Bool in
                uniforms.padding.x = kind
                uniforms.control.w = 1.0 / 60.0
                guard let forwardPass = command.makeComputeCommandEncoder() else { return false }
                forwardPass.setTexture(source, index: 0)
                forwardPass.setTexture(projectedTargetU, index: 1)
                forwardPass.setTexture(projectedTargetV, index: 2)
                forwardPass.setTexture(projectedTargetW, index: 3)
                forwardPass.setTexture(scalarForward, index: 4)
                forwardPass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                forwardPass.setBuffer(cflControl, offset: 0, index: 1)
                dispatch3D(forwardPass, scalarAdvection, nx, ny, nz)
                forwardPass.endEncoding()
                uniforms.padding.z = -1.0
                guard let backwardPass = command.makeComputeCommandEncoder() else { return false }
                backwardPass.setTexture(scalarForward, index: 0)
                backwardPass.setTexture(projectedTargetU, index: 1)
                backwardPass.setTexture(projectedTargetV, index: 2)
                backwardPass.setTexture(projectedTargetW, index: 3)
                backwardPass.setTexture(scalarBackward, index: 4)
                backwardPass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                backwardPass.setBuffer(cflControl, offset: 0, index: 1)
                dispatch3D(backwardPass, scalarAdvection, nx, ny, nz)
                backwardPass.endEncoding()
                uniforms.padding.z = 1.0
                guard let correctionPass = command.makeComputeCommandEncoder() else { return false }
                correctionPass.setTexture(source, index: 0)
                correctionPass.setTexture(scalarForward, index: 1)
                correctionPass.setTexture(scalarBackward, index: 2)
                correctionPass.setTexture(obstacles, index: 3)
                correctionPass.setTexture(target, index: 4)
                correctionPass.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                correctionPass.setBuffer(cflControl, offset: 0, index: 1)
                dispatch3D(correctionPass, scalarCorrection, nx, ny, nz)
                correctionPass.endEncoding()
                return true
            }
            guard advectScalar(currentDensity, nextDensity, 0.0),
                  advectScalar(currentTemperature, nextTemperature, 1.0) else { return false }
            currentDensity = nextDensity
            currentTemperature = nextTemperature
            currentU = projectedTargetU
            currentV = projectedTargetV
            currentW = projectedTargetW
            }
            guard let raymarch = command.makeComputeCommandEncoder() else { return false }
            raymarch.setComputePipelineState(render)
            raymarch.setTexture(currentDensity, index: 0)
            raymarch.setTexture(currentTemperature, index: 1)
            raymarch.setTexture(radiance, index: 2)
            raymarch.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            raymarch.dispatchThreadgroups(MTLSize(width: 2, height: 2, depth: 1),
                                          threadsPerThreadgroup: MTLSize(width: 16, height: 16, depth: 1))
            raymarch.endEncoding()
            command.commit()
            command.waitUntilCompleted()
            guard command.status == .completed else {
                let detail = command.error?.localizedDescription ?? "unknown command-buffer failure"
                FileHandle.standardError.write(Data("Vulkax Metal volume smoke failed: \(detail)\n".utf8))
                return false
            }
            let cflWords = cflControl.contents().bindMemory(to: UInt32.self, capacity: 4)
            let selectedSubsteps = cflWords[1]
            let selectedTimestep = Float(bitPattern: cflWords[2])
            let measuredCourant = Float(bitPattern: cflWords[3])
            let expectedSubsteps: UInt32 = volumeCflStress ? 2 : 1
            let expectedTimestep = frameTimestep / Float(expectedSubsteps)
            let courantIsValid = volumeCflStress ? measuredCourant > 0.75 : measuredCourant <= 0.75
            guard selectedSubsteps == expectedSubsteps,
                  abs(selectedTimestep - expectedTimestep) < 1e-6,
                  courantIsValid else {
                FileHandle.standardError.write(Data(
                    "Vulkax Metal MAC CFL validation failed: substeps=\(selectedSubsteps) dt=\(selectedTimestep) Courant=\(measuredCourant)\n".utf8))
                return false
            }
            var values = [UInt16](repeating: 0, count: 32 * 32 * 4)
            values.withUnsafeMutableBytes {
                radiance.getBytes($0.baseAddress!, bytesPerRow: 32 * 4 * MemoryLayout<UInt16>.stride,
                                  from: MTLRegionMake2D(0, 0, 32, 32), mipmapLevel: 0)
            }
            let radianceSamples = values.map { Float(Float16(bitPattern: $0)) }
            var minimumLuminance = Float.greatestFiniteMagnitude
            var maximumLuminance = Float.zero
            for pixel in 0..<(32 * 32) {
                let offset = pixel * 4
                let luminance = 0.2126 * radianceSamples[offset] + 0.7152 * radianceSamples[offset + 1] +
                    0.0722 * radianceSamples[offset + 2]
                minimumLuminance = min(minimumLuminance, luminance)
                maximumLuminance = max(maximumLuminance, luminance)
            }
            guard radianceSamples.allSatisfy(\.isFinite), maximumLuminance > minimumLuminance + 1e-5 else {
                FileHandle.standardError.write(Data("Vulkax Metal volume smoke failed: HDR radiance was invalid or flat\n".utf8))
                return false
            }
            let readScalar = { (texture: MTLTexture) -> [Float] in
                var bits = [UInt16](repeating: 0, count: nx * ny * nz)
                bits.withUnsafeMutableBytes {
                    texture.getBytes($0.baseAddress!, bytesPerRow: nx * MemoryLayout<UInt16>.stride,
                                     bytesPerImage: nx * ny * MemoryLayout<UInt16>.stride,
                                     from: MTLRegionMake3D(0, 0, 0, nx, ny, nz), mipmapLevel: 0, slice: 0)
                }
                return bits.map { Float(Float16(bitPattern: $0)) }
            }
            let l2 = { (values: [Float]) -> Float in
                sqrt(values.reduce(Float.zero) { $0 + $1 * $1 } / Float(values.count))
            }
            let densityValues = readScalar(currentDensity)
            let temperatureValues = readScalar(currentTemperature)
            let obstacleValues = readScalar(obstacles)
            let before = l2(readScalar(diagnosticDivergenceBefore))
            let after = l2(readScalar(divergenceAfter))
            let baselineResidualNorm = l2(readScalar(baselineResidual))
            let residualNorm = l2(readScalar(pressureResidual))
            var curlBits = [UInt16](repeating: 0, count: nx * ny * nz * 4)
            curlBits.withUnsafeMutableBytes {
                curlField.getBytes($0.baseAddress!, bytesPerRow: nx * 4 * MemoryLayout<UInt16>.stride,
                                   bytesPerImage: nx * ny * 4 * MemoryLayout<UInt16>.stride,
                                   from: MTLRegionMake3D(0, 0, 0, nx, ny, nz), mipmapLevel: 0, slice: 0)
            }
            let curlMagnitude = stride(from: 3, to: curlBits.count, by: 4)
                .map { Float(Float16(bitPattern: curlBits[$0])) }.max() ?? 0
            let densityMass = densityValues.reduce(0, +) / Float(densityValues.count)
            let temperatureMass = temperatureValues.reduce(0, +) / Float(temperatureValues.count)
            guard densityValues.allSatisfy(\.isFinite), temperatureValues.allSatisfy(\.isFinite),
                  before.isFinite, after.isFinite, baselineResidualNorm.isFinite,
                  residualNorm.isFinite, curlMagnitude.isFinite,
                  densityMass > 1e-5, temperatureMass > 1e-5, curlMagnitude > 1e-5,
                  obstacleValues.contains(where: { $0 < 0.5 }), obstacleValues.contains(where: { $0 > 0.5 }),
                  before > 1e-6, after < before, residualNorm < baselineResidualNorm,
                  residualNorm < before * 60.0 else {
                FileHandle.standardError.write(Data(
                    "Vulkax Metal MAC smoke validation failed: divergence=\(before)->\(after) jacobiResidual=\(baselineResidualNorm) multigridResidual=\(residualNorm) density=\(densityMass) temperature=\(temperatureMass) curl=\(curlMagnitude)\n".utf8))
                return false
            }
            let signature = values.reduce(UInt64(1_469_598_103_934_665_603)) {
                ($0 ^ UInt64($1)) &* 1_099_511_628_211
            }
            if let expectedVolumeSignature {
                guard signature == expectedVolumeSignature else {
                    FileHandle.standardError.write(Data(
                        "Vulkax Metal MAC smoke replay mismatch: expected=\(expectedVolumeSignature) actual=\(signature)\n".utf8))
                    return false
                }
                print("Vulkax native Metal MAC volume GPU smoke passed: \(device.name) CFL=\(measuredCourant) substeps=\(selectedSubsteps) dt=\(selectedTimestep) divergence \(before) -> \(after) residual Jacobi=\(baselineResidualNorm) multigrid=\(residualNorm) density \(densityMass) temperature \(temperatureMass) curl \(curlMagnitude) luminance [\(minimumLuminance), \(maximumLuminance)] deterministic-signature \(signature)")
                if volumeCflStress {
                    return runNativeGpuSmoke(volume: true, volumeCflStress: false)
                }
                return true
            }
            return runNativeGpuSmoke(
                volume: true, expectedVolumeSignature: signature,
                volumeCflStress: volumeCflStress)
        }
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba16Float, width: 32, height: 32, mipmapped: false)
        descriptor.usage = [.shaderRead, .shaderWrite]
        // Readback is limited to this verification target. Interactive frames
        // use private HDR textures and never cross the GPU/CPU boundary.
        descriptor.storageMode = blackHole ? .shared : .private
        guard let target = device.makeTexture(descriptor: descriptor) else {
            FileHandle.standardError.write(Data("Vulkax Metal smoke failed: could not allocate GPU command resources\n".utf8))
            return false
        }
        let dispatch = { (sampleIndex: Float) -> Bool in
            guard let command = queue.makeCommandBuffer(), let encoder = command.makeComputeCommandEncoder() else {
                return false
            }
            var uniforms = WaveUniforms(time: 0.0, amplitude: 1.0, wavenumber: 2.0, angularFrequency: 3.0,
                                        width: 32.0, height: 32.0,
                                        control: SIMD4(blackHole ? 1.0 : 0.0, 0.0, sampleIndex, 0.0),
                                        renderParameters: SIMD4(1.0, 1.0, 1.0, 0.0))
            encoder.setComputePipelineState(pipeline)
            encoder.setTexture(target, index: 0)
            encoder.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            encoder.dispatchThreadgroups(MTLSize(width: 2, height: 2, depth: 1),
                                         threadsPerThreadgroup: MTLSize(width: 16, height: 16, depth: 1))
            encoder.endEncoding()
            command.commit()
            command.waitUntilCompleted()
            if command.status == .completed { return true }
            let detail = command.error?.localizedDescription ?? "unknown command-buffer failure"
            FileHandle.standardError.write(Data("Vulkax Metal smoke failed: \(detail)\n".utf8))
            return false
        }
        guard dispatch(0.0) else {
            FileHandle.standardError.write(Data("Vulkax Metal smoke failed: compute dispatch did not complete\n".utf8))
            return false
        }
        if blackHole {
            var first = [UInt16](repeating: 0, count: 32 * 32 * 4)
            first.withUnsafeMutableBytes {
                target.getBytes($0.baseAddress!, bytesPerRow: 32 * 4 * MemoryLayout<UInt16>.stride,
                                from: MTLRegionMake2D(0, 0, 32, 32), mipmapLevel: 0)
            }
            var hasCapturedShadow = false
            var hasEscapedRadiance = false
            for pixel in 0..<(32 * 32) {
                let channel = pixel * 4
                let red = first[channel]
                let green = first[channel + 1]
                let blue = first[channel + 2]
                hasCapturedShadow = hasCapturedShadow || (red == 0 && green == 0 && blue == 0)
                hasEscapedRadiance = hasEscapedRadiance || (red != 0 || green != 0 || blue != 0)
            }
            guard hasCapturedShadow && hasEscapedRadiance else {
                FileHandle.standardError.write(Data("Vulkax Metal smoke failed: Schwarzschild frame did not contain both capture and escape samples\n".utf8))
                return false
            }
            guard dispatch(1.0) else {
                FileHandle.standardError.write(Data("Vulkax Metal smoke failed: second geodesic dispatch did not complete\n".utf8))
                return false
            }
            var second = [UInt16](repeating: 0, count: first.count)
            second.withUnsafeMutableBytes {
                target.getBytes($0.baseAddress!, bytesPerRow: 32 * 4 * MemoryLayout<UInt16>.stride,
                                from: MTLRegionMake2D(0, 0, 32, 32), mipmapLevel: 0)
            }
            guard first != second else {
                FileHandle.standardError.write(Data("Vulkax Metal smoke failed: progressive ray samples were identical\n".utf8))
                return false
            }
        }
        print("Vulkax native Metal \(blackHole ? "Schwarzschild" : "wave") GPU smoke passed: \(device.name)")
        return true
    } catch {
        FileHandle.standardError.write(Data("Vulkax Metal smoke failed: \(error)\n".utf8))
        return false
    }
}

private func runGeneratedPhysicsIrGpuSmoke(shaderPath: String) -> Bool {
    guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
        FileHandle.standardError.write(Data("Vulkax Physics IR Metal smoke failed: no Metal device\n".utf8))
        return false
    }
    do {
        let source = try String(contentsOfFile: shaderPath, encoding: .utf8)
        let library = try device.makeLibrary(source: source, options: nil)
        guard let function = library.makeFunction(name: "executeScalarField") else {
            throw NSError(domain: "VulkaxPhysicsIr", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "generated Metal entry point missing"])
        }
        let pipeline = try device.makeComputePipelineState(function: function)
        let width = 128
        let height = 72
        guard let output = device.makeBuffer(
            length: width * height * MemoryLayout<Float>.stride,
            options: .storageModeShared),
              let command = queue.makeCommandBuffer(),
              let encoder = command.makeComputeCommandEncoder() else {
            throw NSError(domain: "VulkaxPhysicsIr", code: 2,
                          userInfo: [NSLocalizedDescriptionKey: "could not allocate generated field resources"])
        }
        var parameters = GeneratedFieldParameters(
            width: UInt32(width), height: UInt32(height), time: 0.5,
            amplitude: 1.0, wavenumber: 2.0, angularFrequency: 3.0)
        encoder.setComputePipelineState(pipeline)
        encoder.setBuffer(output, offset: 0, index: 0)
        encoder.setBytes(&parameters, length: MemoryLayout<GeneratedFieldParameters>.stride, index: 1)
        encoder.dispatchThreadgroups(
            MTLSize(width: (width + 15) / 16, height: (height + 15) / 16, depth: 1),
            threadsPerThreadgroup: MTLSize(width: 16, height: 16, depth: 1))
        encoder.endEncoding()
        command.commit()
        command.waitUntilCompleted()
        guard command.status == .completed else {
            throw command.error ?? NSError(domain: "VulkaxPhysicsIr", code: 3,
                                            userInfo: [NSLocalizedDescriptionKey: "generated dispatch failed"])
        }
        let values = output.contents().bindMemory(to: Float.self, capacity: width * height)
        var maximumError = Float.zero
        var meanSquaredError = Double.zero
        for y in 0..<height {
            for x in 0..<width {
                let coordinate = Float(x) / Float(max(1, width - 1)) * 8.0 - 4.0
                let reference = sin(2.0 * coordinate - 1.5)
                let error = abs(values[y * width + x] - reference)
                maximumError = max(maximumError, error)
                meanSquaredError += Double(error * error)
            }
        }
        meanSquaredError /= Double(width * height)
        guard maximumError < 1e-5 else {
            FileHandle.standardError.write(Data(
                "Vulkax Physics IR Metal agreement failed: MSE=\(meanSquaredError) max=\(maximumError)\n".utf8))
            return false
        }
        print("Vulkax executable Physics IR Metal GPU smoke passed: \(device.name) MSE=\(meanSquaredError) max_error=\(maximumError)")
        return true
    } catch {
        FileHandle.standardError.write(Data("Vulkax Physics IR Metal smoke failed: \(error)\n".utf8))
        return false
    }
}

final class MetalWaveRenderer: NSObject, MTKViewDelegate {
    private weak var model: PhysicsModel?
    private let commandQueue: MTLCommandQueue
    private let displayPipeline: MTLRenderPipelineState
    private let simulationPipeline: MTLComputePipelineState
    private let clearScalarVolumePipeline: MTLComputePipelineState
    private let clearVectorVolumePipeline: MTLComputePipelineState
    private let seedMacPipeline: MTLComputePipelineState
    private let seedMacVelocityPipeline: MTLComputePipelineState
    private let reduceMacMaximumSpeedPipeline: MTLComputePipelineState
    private let finalizeMacCflPipeline: MTLComputePipelineState
    private let velocitySimulationPipeline: MTLComputePipelineState
    private let divergencePipeline: MTLComputePipelineState
    private let pressurePipeline: MTLComputePipelineState
    private let pressureResidualPipeline: MTLComputePipelineState
    private let residualRestrictionPipeline: MTLComputePipelineState
    private let correctionSolvePipeline: MTLComputePipelineState
    private let correctionProlongationPipeline: MTLComputePipelineState
    private let projectionPipeline: MTLComputePipelineState
    private let scalarAdvectionPipeline: MTLComputePipelineState
    private let scalarCorrectionPipeline: MTLComputePipelineState
    private let curlPipeline: MTLComputePipelineState
    private let volumeRenderPipeline: MTLComputePipelineState
    private let accumulationPipeline: MTLComputePipelineState
    private var hdrRadiance: MTLTexture?
    private var accumulationRadiance: [MTLTexture] = []
    private var activeAccumulationRadiance = 0
    private var accumulationSamples = 0
    private var accumulatedMode: VisualizerMode?
    private var accumulationSignature: SIMD4<Float>?
    private var volumeDensity: [MTLTexture] = []
    private var volumeTemperature: [MTLTexture] = []
    private var activeVolumeDensity = 0
    private var volumeFaceU: [MTLTexture] = []
    private var volumeFaceV: [MTLTexture] = []
    private var volumeFaceW: [MTLTexture] = []
    private var activeVolumeVelocity = 0
    private var volumePressure: [MTLTexture] = []
    private var activeVolumePressure = 0
    private var volumeDivergence: MTLTexture?
    private var volumePostDivergence: MTLTexture?
    private var volumePressureResidual: MTLTexture?
    private var volumeCoarseRhs: MTLTexture?
    private var volumeCoarseObstacles: MTLTexture?
    private var volumeCoarseCorrection: [MTLTexture] = []
    private var volumeObstacles: MTLTexture?
    private var volumeCurl: MTLTexture?
    private var volumeScalarScratch: [MTLTexture] = []
    private var volumeCflControl: MTLBuffer?
    private var volumeInitialized = false
    private var lastTime = CACurrentMediaTime()
    private let inFlightFrames = DispatchSemaphore(value: 3)

    init?(view: MTKView, model: PhysicsModel) {
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else { return nil }
        self.model = model
        self.commandQueue = queue
        do {
            let library = try device.makeLibrary(source: waveShader, options: nil)
            let descriptor = MTLRenderPipelineDescriptor()
            descriptor.vertexFunction = library.makeFunction(name: "fullscreen")
            descriptor.fragmentFunction = library.makeFunction(name: "display")
            descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
            descriptor.colorAttachments[0].isBlendingEnabled = false
            self.displayPipeline = try device.makeRenderPipelineState(descriptor: descriptor)
            guard let simulation = library.makeFunction(name: "simulateWave") else { return nil }
            guard let clearScalarVolume = library.makeFunction(name: "clearScalarVolume"),
                  let clearVectorVolume = library.makeFunction(name: "clearVectorVolume"),
                  let seedMac = library.makeFunction(name: "seedMacFields"),
                  let seedMacVelocity = library.makeFunction(name: "seedMacVelocity"),
                  let reduceMacMaximumSpeed = library.makeFunction(name: "reduceMacMaximumSpeed"),
                  let finalizeMacCfl = library.makeFunction(name: "finalizeMacCfl"),
                  let velocitySimulation = library.makeFunction(name: "advectMacVelocity"),
                  let divergence = library.makeFunction(name: "computeMacDivergence"),
                  let pressure = library.makeFunction(name: "solveMacPressureJacobi"),
                  let pressureResidual = library.makeFunction(name: "computeMacPressureResidual"),
                  let restrictResidual = library.makeFunction(name: "restrictMacResidual"),
                  let solveCorrection = library.makeFunction(name: "solveMacCorrectionJacobi"),
                  let prolongateCorrection = library.makeFunction(name: "prolongateMacCorrection"),
                  let projection = library.makeFunction(name: "projectMacVelocity"),
                  let scalarAdvection = library.makeFunction(name: "advectMacScalar"),
                  let scalarCorrection = library.makeFunction(name: "correctMacScalar"),
                  let curl = library.makeFunction(name: "computeMacCurl") else { return nil }
            guard let volumeRender = library.makeFunction(name: "renderVolume") else { return nil }
            guard let accumulate = library.makeFunction(name: "accumulateRadiance") else { return nil }
            self.simulationPipeline = try device.makeComputePipelineState(function: simulation)
            self.clearScalarVolumePipeline = try device.makeComputePipelineState(function: clearScalarVolume)
            self.clearVectorVolumePipeline = try device.makeComputePipelineState(function: clearVectorVolume)
            self.seedMacPipeline = try device.makeComputePipelineState(function: seedMac)
            self.seedMacVelocityPipeline = try device.makeComputePipelineState(function: seedMacVelocity)
            self.reduceMacMaximumSpeedPipeline = try device.makeComputePipelineState(function: reduceMacMaximumSpeed)
            self.finalizeMacCflPipeline = try device.makeComputePipelineState(function: finalizeMacCfl)
            self.velocitySimulationPipeline = try device.makeComputePipelineState(function: velocitySimulation)
            self.divergencePipeline = try device.makeComputePipelineState(function: divergence)
            self.pressurePipeline = try device.makeComputePipelineState(function: pressure)
            self.pressureResidualPipeline = try device.makeComputePipelineState(function: pressureResidual)
            self.residualRestrictionPipeline = try device.makeComputePipelineState(function: restrictResidual)
            self.correctionSolvePipeline = try device.makeComputePipelineState(function: solveCorrection)
            self.correctionProlongationPipeline = try device.makeComputePipelineState(function: prolongateCorrection)
            self.projectionPipeline = try device.makeComputePipelineState(function: projection)
            self.scalarAdvectionPipeline = try device.makeComputePipelineState(function: scalarAdvection)
            self.scalarCorrectionPipeline = try device.makeComputePipelineState(function: scalarCorrection)
            self.curlPipeline = try device.makeComputePipelineState(function: curl)
            self.volumeRenderPipeline = try device.makeComputePipelineState(function: volumeRender)
            self.accumulationPipeline = try device.makeComputePipelineState(function: accumulate)
        } catch {
            FileHandle.standardError.write(Data("Vulkax Metal pipeline error: \(error)\n".utf8))
            return nil
        }
        super.init()
        view.device = device
        view.delegate = self
        view.framebufferOnly = true
        view.enableSetNeedsDisplay = false
        view.isPaused = false
        view.preferredFramesPerSecond = 60
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    private func ensureHdrRadiance(_ size: CGSize, scale: CGFloat, device: MTLDevice) -> MTLTexture? {
        let width = max(1, Int(size.width * scale))
        let height = max(1, Int(size.height * scale))
        if let hdrRadiance, hdrRadiance.width == width, hdrRadiance.height == height { return hdrRadiance }
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba16Float, width: width, height: height, mipmapped: false)
        descriptor.usage = [.shaderRead, .shaderWrite]
        descriptor.storageMode = .private
        hdrRadiance = device.makeTexture(descriptor: descriptor)
        accumulationRadiance = []
        activeAccumulationRadiance = 0
        accumulationSamples = 0
        accumulatedMode = nil
        accumulationSignature = nil
        return hdrRadiance
    }

    private func ensureAccumulationRadiance(_ radiance: MTLTexture, device: MTLDevice) -> Bool {
        if accumulationRadiance.count == 2 { return true }
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba16Float, width: radiance.width, height: radiance.height, mipmapped: false)
        descriptor.usage = [.shaderRead, .shaderWrite]
        descriptor.storageMode = .private
        guard let first = device.makeTexture(descriptor: descriptor), let second = device.makeTexture(descriptor: descriptor) else {
            return false
        }
        accumulationRadiance = [first, second]
        activeAccumulationRadiance = 0
        accumulationSamples = 0
        accumulatedMode = nil
        accumulationSignature = nil
        return true
    }

    private func makeVolumeTexture(
        device: MTLDevice, format: MTLPixelFormat,
        width: Int = 64, height: Int = 96, depth: Int = 64
    ) -> MTLTexture? {
        let descriptor = MTLTextureDescriptor()
        descriptor.textureType = .type3D
        descriptor.pixelFormat = format
        descriptor.width = width
        descriptor.height = height
        descriptor.depth = depth
        descriptor.mipmapLevelCount = 1
        descriptor.usage = [.shaderRead, .shaderWrite]
        descriptor.storageMode = .private
        return device.makeTexture(descriptor: descriptor)
    }

    private func ensureVolumeSolver(device: MTLDevice) -> Bool {
        if volumeDensity.count == 2, volumeTemperature.count == 2,
           volumeFaceU.count == 2, volumeFaceV.count == 2, volumeFaceW.count == 2,
           volumePressure.count == 2, volumeScalarScratch.count == 2,
           volumeCoarseCorrection.count == 2,
           volumeDivergence != nil, volumePostDivergence != nil, volumePressureResidual != nil,
           volumeCoarseRhs != nil, volumeCoarseObstacles != nil,
           volumeObstacles != nil, volumeCurl != nil, volumeCflControl != nil { return true }
        guard let density0 = makeVolumeTexture(device: device, format: .r16Float),
              let density1 = makeVolumeTexture(device: device, format: .r16Float),
              let temperature0 = makeVolumeTexture(device: device, format: .r16Float),
              let temperature1 = makeVolumeTexture(device: device, format: .r16Float),
              let faceU0 = makeVolumeTexture(device: device, format: .r16Float, width: 65, height: 96, depth: 64),
              let faceU1 = makeVolumeTexture(device: device, format: .r16Float, width: 65, height: 96, depth: 64),
              let faceV0 = makeVolumeTexture(device: device, format: .r16Float, width: 64, height: 97, depth: 64),
              let faceV1 = makeVolumeTexture(device: device, format: .r16Float, width: 64, height: 97, depth: 64),
              let faceW0 = makeVolumeTexture(device: device, format: .r16Float, width: 64, height: 96, depth: 65),
              let faceW1 = makeVolumeTexture(device: device, format: .r16Float, width: 64, height: 96, depth: 65),
              let pressure0 = makeVolumeTexture(device: device, format: .r16Float),
              let pressure1 = makeVolumeTexture(device: device, format: .r16Float),
              let divergence = makeVolumeTexture(device: device, format: .r16Float),
              let postDivergence = makeVolumeTexture(device: device, format: .r16Float),
              let pressureResidual = makeVolumeTexture(device: device, format: .r16Float),
              let coarseRhs = makeVolumeTexture(device: device, format: .r16Float, width: 32, height: 48, depth: 32),
              let coarseObstacles = makeVolumeTexture(device: device, format: .r16Float, width: 32, height: 48, depth: 32),
              let coarseCorrection0 = makeVolumeTexture(device: device, format: .r16Float, width: 32, height: 48, depth: 32),
              let coarseCorrection1 = makeVolumeTexture(device: device, format: .r16Float, width: 32, height: 48, depth: 32),
              let obstacles = makeVolumeTexture(device: device, format: .r16Float),
              let curl = makeVolumeTexture(device: device, format: .rgba16Float),
              let scalarForward = makeVolumeTexture(device: device, format: .r16Float),
              let scalarBackward = makeVolumeTexture(device: device, format: .r16Float),
              let cflControl = device.makeBuffer(length: 4 * MemoryLayout<UInt32>.stride, options: .storageModePrivate) else {
            return false
        }
        volumeDensity = [density0, density1]
        volumeTemperature = [temperature0, temperature1]
        volumeFaceU = [faceU0, faceU1]
        volumeFaceV = [faceV0, faceV1]
        volumeFaceW = [faceW0, faceW1]
        volumePressure = [pressure0, pressure1]
        volumeDivergence = divergence
        volumePostDivergence = postDivergence
        volumePressureResidual = pressureResidual
        volumeCoarseRhs = coarseRhs
        volumeCoarseObstacles = coarseObstacles
        volumeCoarseCorrection = [coarseCorrection0, coarseCorrection1]
        volumeObstacles = obstacles
        volumeCurl = curl
        volumeScalarScratch = [scalarForward, scalarBackward]
        volumeCflControl = cflControl
        activeVolumeDensity = 0
        activeVolumeVelocity = 0
        activeVolumePressure = 0
        volumeInitialized = false
        return true
    }

    private func dispatch3D(_ encoder: MTLComputeCommandEncoder, pipeline: MTLComputePipelineState, texture: MTLTexture) {
        dispatch3D(encoder, pipeline: pipeline, width: texture.width, height: texture.height, depth: texture.depth)
    }

    private func dispatch3D(
        _ encoder: MTLComputeCommandEncoder, pipeline: MTLComputePipelineState,
        width: Int, height: Int, depth: Int
    ) {
        encoder.setComputePipelineState(pipeline)
        let threads = MTLSize(width: 4, height: 4, depth: 4)
        let groups = MTLSize(width: (width + 3) / 4, height: (height + 3) / 4, depth: (depth + 3) / 4)
        encoder.dispatchThreadgroups(groups, threadsPerThreadgroup: threads)
    }

    func draw(in view: MTKView) {
        guard let model, let device = view.device, let drawable = view.currentDrawable, let pass = view.currentRenderPassDescriptor,
              let command = commandQueue.makeCommandBuffer() else { return }
        guard inFlightFrames.wait(timeout: .now()) == .success else { return }
        var submitted = false
        defer { if !submitted { inFlightFrames.signal() } }
        let renderScale: CGFloat = model.mode == .schwarzschild ? 0.55 : 1.0
        guard let radiance = ensureHdrRadiance(view.drawableSize, scale: renderScale, device: device) else { return }
        let now = CACurrentMediaTime()
        let delta = min(Float(now - lastTime), 1.0 / 20.0)
        lastTime = now
        let accumulate = model.mode == .schwarzschild
        let schwarzschildSignature = SIMD4<Float>(
            model.blackHoleMass, model.diskGain, model.cameraScale,
            Float(model.accumulationResetToken))
        let renderParameters: SIMD4<Float>
        switch model.mode {
        case .schwarzschild:
            renderParameters = schwarzschildSignature
        case .volumeSmoke:
            renderParameters = SIMD4(
                model.smokeBuoyancy, model.smokeTurbulence,
                model.volumeExtinction, model.volumeEmission)
        case .wave:
            renderParameters = .zero
        }
        let resetAccumulation = accumulatedMode != model.mode || accumulationSignature != schwarzschildSignature
        let uniforms = WaveUniforms(time: model.time, amplitude: model.amplitude, wavenumber: model.wavenumber,
                                   angularFrequency: model.angularFrequency, width: Float(radiance.width),
                                   height: Float(radiance.height),
                                   control: SIMD4(model.mode.rawValue, resetAccumulation ? 1 : 0,
                                                  Float(accumulationSamples), 0),
                                   renderParameters: renderParameters)
        var copy = uniforms
        if model.mode == .volumeSmoke {
            guard ensureVolumeSolver(device: device),
                  let divergence = volumeDivergence,
                  let postDivergence = volumePostDivergence,
                  let pressureResidual = volumePressureResidual,
                  let coarseRhs = volumeCoarseRhs,
                  let coarseObstacles = volumeCoarseObstacles,
                  let obstacles = volumeObstacles,
                  let curl = volumeCurl,
                  let cflControl = volumeCflControl else { return }
            copy.control.w = max(delta, 1.0 / 240.0)
            copy.padding.w = 96.0
            if !volumeInitialized {
                for texture in volumeDensity + volumeTemperature + volumePressure +
                    volumeScalarScratch + volumeCoarseCorrection +
                    [divergence, postDivergence, pressureResidual, coarseRhs, coarseObstacles] {
                    guard let clear = command.makeComputeCommandEncoder() else { return }
                    clear.setTexture(texture, index: 0)
                    dispatch3D(clear, pipeline: clearScalarVolumePipeline, texture: texture)
                    clear.endEncoding()
                }
                for texture in volumeFaceU + volumeFaceV + volumeFaceW {
                    guard let clear = command.makeComputeCommandEncoder() else { return }
                    clear.setTexture(texture, index: 0)
                    dispatch3D(clear, pipeline: clearScalarVolumePipeline, texture: texture)
                    clear.endEncoding()
                }
                guard let clear = command.makeComputeCommandEncoder() else { return }
                clear.setTexture(curl, index: 0)
                dispatch3D(clear, pipeline: clearVectorVolumePipeline, texture: curl)
                clear.endEncoding()
                guard let seed = command.makeComputeCommandEncoder() else { return }
                seed.setTexture(volumeDensity[activeVolumeDensity], index: 0)
                seed.setTexture(volumeTemperature[activeVolumeDensity], index: 1)
                seed.setTexture(obstacles, index: 2)
                dispatch3D(seed, pipeline: seedMacPipeline, texture: volumeDensity[activeVolumeDensity])
                seed.endEncoding()
                guard let seedVelocity = command.makeComputeCommandEncoder() else { return }
                seedVelocity.setTexture(volumeFaceU[activeVolumeVelocity], index: 0)
                seedVelocity.setTexture(volumeFaceV[activeVolumeVelocity], index: 1)
                seedVelocity.setTexture(volumeFaceW[activeVolumeVelocity], index: 2)
                seedVelocity.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                dispatch3D(seedVelocity, pipeline: seedMacVelocityPipeline, width: 65, height: 97, depth: 65)
                seedVelocity.endEncoding()
                volumeInitialized = true
            }
            guard let resetCfl = command.makeBlitCommandEncoder() else { return }
            resetCfl.fill(buffer: cflControl, range: 0..<cflControl.length, value: 0)
            resetCfl.endEncoding()
            guard let reduceCfl = command.makeComputeCommandEncoder() else { return }
            reduceCfl.setTexture(volumeFaceU[activeVolumeVelocity], index: 0)
            reduceCfl.setTexture(volumeFaceV[activeVolumeVelocity], index: 1)
            reduceCfl.setTexture(volumeFaceW[activeVolumeVelocity], index: 2)
            reduceCfl.setBuffer(cflControl, offset: 0, index: 0)
            dispatch3D(reduceCfl, pipeline: reduceMacMaximumSpeedPipeline, width: 64, height: 96, depth: 64)
            reduceCfl.endEncoding()
            guard let finalizeCfl = command.makeComputeCommandEncoder() else { return }
            finalizeCfl.setComputePipelineState(finalizeMacCflPipeline)
            finalizeCfl.setBuffer(cflControl, offset: 0, index: 0)
            finalizeCfl.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 1)
            finalizeCfl.dispatchThreads(MTLSize(width: 1, height: 1, depth: 1),
                                        threadsPerThreadgroup: MTLSize(width: 1, height: 1, depth: 1))
            finalizeCfl.endEncoding()
            for substep in 0..<2 {
            copy.padding.y = Float(substep)
            let nextDensity = 1 - activeVolumeDensity
            let nextVelocity = 1 - activeVolumeVelocity
            guard let velocitySimulation = command.makeComputeCommandEncoder() else { return }
            velocitySimulation.setTexture(volumeFaceU[activeVolumeVelocity], index: 0)
            velocitySimulation.setTexture(volumeFaceV[activeVolumeVelocity], index: 1)
            velocitySimulation.setTexture(volumeFaceW[activeVolumeVelocity], index: 2)
            velocitySimulation.setTexture(volumeDensity[activeVolumeDensity], index: 3)
            velocitySimulation.setTexture(volumeTemperature[activeVolumeDensity], index: 4)
            velocitySimulation.setTexture(volumeFaceU[nextVelocity], index: 5)
            velocitySimulation.setTexture(volumeFaceV[nextVelocity], index: 6)
            velocitySimulation.setTexture(volumeFaceW[nextVelocity], index: 7)
            velocitySimulation.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            velocitySimulation.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(velocitySimulation, pipeline: velocitySimulationPipeline, width: 65, height: 97, depth: 65)
            velocitySimulation.endEncoding()

            guard let divergenceEncoder = command.makeComputeCommandEncoder() else { return }
            divergenceEncoder.setTexture(volumeFaceU[nextVelocity], index: 0)
            divergenceEncoder.setTexture(volumeFaceV[nextVelocity], index: 1)
            divergenceEncoder.setTexture(volumeFaceW[nextVelocity], index: 2)
            divergenceEncoder.setTexture(obstacles, index: 3)
            divergenceEncoder.setTexture(divergence, index: 4)
            dispatch3D(divergenceEncoder, pipeline: divergencePipeline, texture: divergence)
            divergenceEncoder.endEncoding()

            var pressureSource = activeVolumePressure
            for _ in 0..<8 {
                let pressureTarget = 1 - pressureSource
                guard let pressureEncoder = command.makeComputeCommandEncoder() else { return }
                pressureEncoder.setTexture(volumePressure[pressureSource], index: 0)
                pressureEncoder.setTexture(divergence, index: 1)
                pressureEncoder.setTexture(obstacles, index: 2)
                pressureEncoder.setTexture(volumePressure[pressureTarget], index: 3)
                pressureEncoder.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                pressureEncoder.setBuffer(cflControl, offset: 0, index: 1)
                dispatch3D(pressureEncoder, pipeline: pressurePipeline, texture: volumePressure[pressureTarget])
                pressureEncoder.endEncoding()
                pressureSource = pressureTarget
            }
            guard let preResidualEncoder = command.makeComputeCommandEncoder() else { return }
            preResidualEncoder.setTexture(volumePressure[pressureSource], index: 0)
            preResidualEncoder.setTexture(divergence, index: 1)
            preResidualEncoder.setTexture(obstacles, index: 2)
            preResidualEncoder.setTexture(pressureResidual, index: 3)
            preResidualEncoder.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            preResidualEncoder.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(preResidualEncoder, pipeline: pressureResidualPipeline, texture: pressureResidual)
            preResidualEncoder.endEncoding()

            guard let restrictionEncoder = command.makeComputeCommandEncoder() else { return }
            restrictionEncoder.setTexture(pressureResidual, index: 0)
            restrictionEncoder.setTexture(obstacles, index: 1)
            restrictionEncoder.setTexture(coarseRhs, index: 2)
            restrictionEncoder.setTexture(coarseObstacles, index: 3)
            dispatch3D(restrictionEncoder, pipeline: residualRestrictionPipeline, width: 32, height: 48, depth: 32)
            restrictionEncoder.endEncoding()

            for texture in volumeCoarseCorrection {
                guard let clear = command.makeComputeCommandEncoder() else { return }
                clear.setTexture(texture, index: 0)
                dispatch3D(clear, pipeline: clearScalarVolumePipeline, texture: texture)
                clear.endEncoding()
            }
            var coarseSource = 0
            for _ in 0..<20 {
                let coarseTarget = 1 - coarseSource
                guard let correctionEncoder = command.makeComputeCommandEncoder() else { return }
                correctionEncoder.setTexture(volumeCoarseCorrection[coarseSource], index: 0)
                correctionEncoder.setTexture(coarseRhs, index: 1)
                correctionEncoder.setTexture(coarseObstacles, index: 2)
                correctionEncoder.setTexture(volumeCoarseCorrection[coarseTarget], index: 3)
                dispatch3D(correctionEncoder, pipeline: correctionSolvePipeline, width: 32, height: 48, depth: 32)
                correctionEncoder.endEncoding()
                coarseSource = coarseTarget
            }
            let prolongedPressure = 1 - pressureSource
            guard let prolongationEncoder = command.makeComputeCommandEncoder() else { return }
            prolongationEncoder.setTexture(volumePressure[pressureSource], index: 0)
            prolongationEncoder.setTexture(volumeCoarseCorrection[coarseSource], index: 1)
            prolongationEncoder.setTexture(obstacles, index: 2)
            prolongationEncoder.setTexture(volumePressure[prolongedPressure], index: 3)
            prolongationEncoder.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            prolongationEncoder.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(prolongationEncoder, pipeline: correctionProlongationPipeline, texture: volumePressure[prolongedPressure])
            prolongationEncoder.endEncoding()
            pressureSource = prolongedPressure

            for _ in 0..<8 {
                let pressureTarget = 1 - pressureSource
                guard let pressureEncoder = command.makeComputeCommandEncoder() else { return }
                pressureEncoder.setTexture(volumePressure[pressureSource], index: 0)
                pressureEncoder.setTexture(divergence, index: 1)
                pressureEncoder.setTexture(obstacles, index: 2)
                pressureEncoder.setTexture(volumePressure[pressureTarget], index: 3)
                pressureEncoder.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                pressureEncoder.setBuffer(cflControl, offset: 0, index: 1)
                dispatch3D(pressureEncoder, pipeline: pressurePipeline, texture: volumePressure[pressureTarget])
                pressureEncoder.endEncoding()
                pressureSource = pressureTarget
            }
            activeVolumePressure = pressureSource

            guard let pressureResidualEncoder = command.makeComputeCommandEncoder() else { return }
            pressureResidualEncoder.setTexture(volumePressure[activeVolumePressure], index: 0)
            pressureResidualEncoder.setTexture(divergence, index: 1)
            pressureResidualEncoder.setTexture(obstacles, index: 2)
            pressureResidualEncoder.setTexture(pressureResidual, index: 3)
            pressureResidualEncoder.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            pressureResidualEncoder.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(pressureResidualEncoder, pipeline: pressureResidualPipeline, texture: pressureResidual)
            pressureResidualEncoder.endEncoding()

            guard let projectionEncoder = command.makeComputeCommandEncoder() else { return }
            projectionEncoder.setTexture(volumeFaceU[nextVelocity], index: 0)
            projectionEncoder.setTexture(volumeFaceV[nextVelocity], index: 1)
            projectionEncoder.setTexture(volumeFaceW[nextVelocity], index: 2)
            projectionEncoder.setTexture(volumePressure[activeVolumePressure], index: 3)
            projectionEncoder.setTexture(obstacles, index: 4)
            projectionEncoder.setTexture(volumeFaceU[activeVolumeVelocity], index: 5)
            projectionEncoder.setTexture(volumeFaceV[activeVolumeVelocity], index: 6)
            projectionEncoder.setTexture(volumeFaceW[activeVolumeVelocity], index: 7)
            projectionEncoder.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            projectionEncoder.setBuffer(cflControl, offset: 0, index: 1)
            dispatch3D(projectionEncoder, pipeline: projectionPipeline, width: 65, height: 97, depth: 65)
            projectionEncoder.endEncoding()

            guard let postDivergenceEncoder = command.makeComputeCommandEncoder() else { return }
            postDivergenceEncoder.setTexture(volumeFaceU[activeVolumeVelocity], index: 0)
            postDivergenceEncoder.setTexture(volumeFaceV[activeVolumeVelocity], index: 1)
            postDivergenceEncoder.setTexture(volumeFaceW[activeVolumeVelocity], index: 2)
            postDivergenceEncoder.setTexture(obstacles, index: 3)
            postDivergenceEncoder.setTexture(postDivergence, index: 4)
            dispatch3D(postDivergenceEncoder, pipeline: divergencePipeline, texture: postDivergence)
            postDivergenceEncoder.endEncoding()

            guard let curlEncoder = command.makeComputeCommandEncoder() else { return }
            curlEncoder.setTexture(volumeFaceU[activeVolumeVelocity], index: 0)
            curlEncoder.setTexture(volumeFaceV[activeVolumeVelocity], index: 1)
            curlEncoder.setTexture(volumeFaceW[activeVolumeVelocity], index: 2)
            curlEncoder.setTexture(obstacles, index: 3)
            curlEncoder.setTexture(curl, index: 4)
            dispatch3D(curlEncoder, pipeline: curlPipeline, texture: curl)
            curlEncoder.endEncoding()

            let advectScalar = { (source: MTLTexture, target: MTLTexture, kind: Float) -> Bool in
                copy.padding.x = kind
                copy.padding.z = 1.0
                guard let forward = command.makeComputeCommandEncoder() else { return false }
                forward.setTexture(source, index: 0)
                forward.setTexture(self.volumeFaceU[self.activeVolumeVelocity], index: 1)
                forward.setTexture(self.volumeFaceV[self.activeVolumeVelocity], index: 2)
                forward.setTexture(self.volumeFaceW[self.activeVolumeVelocity], index: 3)
                forward.setTexture(self.volumeScalarScratch[0], index: 4)
                forward.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                forward.setBuffer(cflControl, offset: 0, index: 1)
                self.dispatch3D(forward, pipeline: self.scalarAdvectionPipeline, texture: target)
                forward.endEncoding()
                copy.padding.z = -1.0
                guard let backward = command.makeComputeCommandEncoder() else { return false }
                backward.setTexture(self.volumeScalarScratch[0], index: 0)
                backward.setTexture(self.volumeFaceU[self.activeVolumeVelocity], index: 1)
                backward.setTexture(self.volumeFaceV[self.activeVolumeVelocity], index: 2)
                backward.setTexture(self.volumeFaceW[self.activeVolumeVelocity], index: 3)
                backward.setTexture(self.volumeScalarScratch[1], index: 4)
                backward.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                backward.setBuffer(cflControl, offset: 0, index: 1)
                self.dispatch3D(backward, pipeline: self.scalarAdvectionPipeline, texture: target)
                backward.endEncoding()
                copy.padding.z = 1.0
                guard let correction = command.makeComputeCommandEncoder() else { return false }
                correction.setTexture(source, index: 0)
                correction.setTexture(self.volumeScalarScratch[0], index: 1)
                correction.setTexture(self.volumeScalarScratch[1], index: 2)
                correction.setTexture(obstacles, index: 3)
                correction.setTexture(target, index: 4)
                correction.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                correction.setBuffer(cflControl, offset: 0, index: 1)
                self.dispatch3D(correction, pipeline: self.scalarCorrectionPipeline, texture: target)
                correction.endEncoding()
                return true
            }
            guard advectScalar(volumeDensity[activeVolumeDensity], volumeDensity[nextDensity], 0.0),
                  advectScalar(volumeTemperature[activeVolumeDensity], volumeTemperature[nextDensity], 1.0) else { return }
            activeVolumeDensity = nextDensity
            }
            guard let volumeRender = command.makeComputeCommandEncoder() else { return }
            volumeRender.setComputePipelineState(volumeRenderPipeline)
            volumeRender.setTexture(volumeDensity[activeVolumeDensity], index: 0)
            volumeRender.setTexture(volumeTemperature[activeVolumeDensity], index: 1)
            volumeRender.setTexture(radiance, index: 2)
            volumeRender.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            let threads = MTLSize(width: 16, height: 16, depth: 1)
            let groups = MTLSize(width: (radiance.width + 15) / 16, height: (radiance.height + 15) / 16, depth: 1)
            volumeRender.dispatchThreadgroups(groups, threadsPerThreadgroup: threads)
            volumeRender.endEncoding()
        } else {
            guard let compute = command.makeComputeCommandEncoder() else { return }
            compute.setComputePipelineState(simulationPipeline)
            compute.setTexture(radiance, index: 0)
            compute.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            let threads = MTLSize(width: 16, height: 16, depth: 1)
            let groups = MTLSize(width: (radiance.width + 15) / 16, height: (radiance.height + 15) / 16, depth: 1)
            compute.dispatchThreadgroups(groups, threadsPerThreadgroup: threads)
            compute.endEncoding()
        }

        var displayRadiance = radiance
        if accumulate {
            guard ensureAccumulationRadiance(radiance, device: device) else { return }
            let nextAccumulation = 1 - activeAccumulationRadiance
            guard let accumulator = command.makeComputeCommandEncoder() else { return }
            accumulator.setComputePipelineState(accumulationPipeline)
            accumulator.setTexture(radiance, index: 0)
            accumulator.setTexture(accumulationRadiance[activeAccumulationRadiance], index: 1)
            accumulator.setTexture(accumulationRadiance[nextAccumulation], index: 2)
            accumulator.setBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
            let threads = MTLSize(width: 16, height: 16, depth: 1)
            let groups = MTLSize(width: (radiance.width + 15) / 16, height: (radiance.height + 15) / 16, depth: 1)
            accumulator.dispatchThreadgroups(groups, threadsPerThreadgroup: threads)
            accumulator.endEncoding()
            activeAccumulationRadiance = nextAccumulation
            accumulationSamples = resetAccumulation ? 1 : min(accumulationSamples + 1, 256)
            accumulatedMode = model.mode
            accumulationSignature = schwarzschildSignature
            displayRadiance = accumulationRadiance[activeAccumulationRadiance]
        } else {
            accumulationSamples = 0
            accumulatedMode = nil
            accumulationSignature = nil
        }

        guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return }
        encoder.setRenderPipelineState(displayPipeline)
        encoder.setFragmentTexture(displayRadiance, index: 0)
        encoder.setFragmentBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        encoder.endEncoding()
        command.addCompletedHandler { [inFlightFrames] _ in inFlightFrames.signal() }
        command.present(drawable)
        command.commit()
        submitted = true
        if model.playing { DispatchQueue.main.async { model.time += delta } }
    }
}

struct MetalWaveView: NSViewRepresentable {
    @ObservedObject var model: PhysicsModel

    func makeNSView(context: Context) -> MTKView {
        let view = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        view.colorPixelFormat = .bgra8Unorm_srgb
        view.clearColor = MTLClearColor(red: 0.01, green: 0.02, blue: 0.06, alpha: 1.0)
        let renderer = MetalWaveRenderer(view: view, model: model)
        view.delegate = renderer
        objc_setAssociatedObject(view, &rendererAssociationKey, renderer, .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
        return view
    }

    func updateNSView(_ view: MTKView, context: Context) {}
}

struct ContentView: View {
    @StateObject private var model = PhysicsModel()

    var body: some View {
        HSplitView {
            VStack(alignment: .leading, spacing: 18) {
                Text("VULKAX").font(.system(size: 24, weight: .bold, design: .rounded)).foregroundStyle(.mint)
                Text("PHYSICS STUDIO").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
                Divider()
                Text("DIRECT GPU VIEWPORT").font(.caption.weight(.bold)).foregroundStyle(.secondary)
                Label("CAMetalLayer presentation", systemImage: "display")
                Label("Metal compute plus HDR presentation", systemImage: "cpu")
                Label("No CPU image bridge", systemImage: "bolt.fill")
                Spacer()
            }
            .frame(minWidth: 210, idealWidth: 230)
            .padding(20)
            .background(Color(red: 0.045, green: 0.07, blue: 0.12))

            VStack(spacing: 0) {
                MetalWaveView(model: model)
                    .overlay(alignment: .topLeading) {
                        Text(model.playing ? "LIVE GPU" : "PAUSED")
                            .font(.caption.bold()).foregroundStyle(model.playing ? .mint : .orange)
                            .padding(8).background(.black.opacity(0.45), in: RoundedRectangle(cornerRadius: 4))
                            .padding(14)
                    }
                HStack {
                    Button(model.playing ? "Pause" : "Play") { model.playing.toggle() }
                    Button("Reset") {
                        model.time = 0
                        model.accumulationResetToken &+= 1
                    }
                    Spacer()
                    Text(String(format: "t = %.2f s", model.time)).monospacedDigit().foregroundStyle(.secondary)
                }.padding(14).background(.thinMaterial)
            }
            .frame(minWidth: 640, minHeight: 540)

            VStack(alignment: .leading, spacing: 18) {
                Picker("Visualization", selection: $model.mode) {
                    ForEach(VisualizerMode.allCases) { mode in Text(mode.title).tag(mode) }
                }.pickerStyle(.segmented)
                Text(model.mode == .wave ? "WAVE FIELD" : model.mode == .schwarzschild ? "SCHWARZSCHILD LENSING" : "3D VOLUME SMOKE").font(.headline)
                if model.mode == .wave {
                    parameter("Amplitude", value: $model.amplitude, range: 0...4)
                    parameter("Wavenumber", value: $model.wavenumber, range: 0.1...18)
                    parameter("Frequency", value: $model.angularFrequency, range: 0.1...18)
                } else if model.mode == .schwarzschild {
                    parameter("Mass", value: $model.blackHoleMass, range: 0.25...1.6)
                    parameter("Disk gain", value: $model.diskGain, range: 0...4)
                    parameter("Camera scale", value: $model.cameraScale, range: 0.5...2.0)
                    Text("GPU 3D Schwarzschild RK4 with disk-plane crossings").font(.subheadline).foregroundStyle(.secondary)
                    Text("Progressive subpixel rays reset after a lens parameter change").font(.subheadline).foregroundStyle(.secondary)
                } else {
                    parameter("Buoyancy", value: $model.smokeBuoyancy, range: 0...3)
                    parameter("Turbulence", value: $model.smokeTurbulence, range: 0...3)
                    parameter("Extinction", value: $model.volumeExtinction, range: 0.1...5)
                    parameter("Emission", value: $model.volumeEmission, range: 0...3)
                    Text("GPU-resident 64 x 96 x 64 staggered MAC grid").font(.subheadline).foregroundStyle(.secondary)
                    Text("RK2/MacCormack transport, two-level multigrid, and HDR ray marching").font(.subheadline).foregroundStyle(.secondary)
                }
                Spacer()
                Text("The displayed radiance is computed at backing resolution, held in RGBA16Float, then tone-mapped into the Metal drawable.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            .frame(minWidth: 260, idealWidth: 300)
            .padding(20)
            .background(Color(red: 0.045, green: 0.07, blue: 0.12))
        }
        .frame(minWidth: 1120, minHeight: 720)
        .preferredColorScheme(.dark)
    }

    @ViewBuilder private func parameter(_ label: String, value: Binding<Float>, range: ClosedRange<Float>) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack { Text(label).font(.subheadline.weight(.medium)); Spacer(); Text(String(format: "%.2f", value.wrappedValue)).monospacedDigit().foregroundStyle(.secondary) }
            Slider(value: value, in: range)
        }
    }
}

@main struct VulkaxPhysicsStudioMacApp: App {
    init() {
        if let option = CommandLine.arguments.firstIndex(of: "--native-physics-ir-gpu-smoke") {
            guard option + 1 < CommandLine.arguments.count else {
                FileHandle.standardError.write(Data("--native-physics-ir-gpu-smoke requires an MSL path\n".utf8))
                exit(EXIT_FAILURE)
            }
            exit(runGeneratedPhysicsIrGpuSmoke(shaderPath: CommandLine.arguments[option + 1])
                 ? EXIT_SUCCESS : EXIT_FAILURE)
        }
        if CommandLine.arguments.contains("--native-gpu-smoke") {
            exit(runNativeGpuSmoke() ? EXIT_SUCCESS : EXIT_FAILURE)
        }
        if CommandLine.arguments.contains("--native-black-hole-gpu-smoke") {
            exit(runNativeGpuSmoke(blackHole: true) ? EXIT_SUCCESS : EXIT_FAILURE)
        }
        if CommandLine.arguments.contains("--native-volume-gpu-smoke") {
            exit(runNativeGpuSmoke(volume: true) ? EXIT_SUCCESS : EXIT_FAILURE)
        }
    }

    var body: some Scene { WindowGroup("Vulkax Physics Studio") { ContentView() } }
}
