#pragma once

namespace vulkax::viewer::metal {

inline constexpr char shaderSource[] = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct GPUSplat {
    float4 positionScaleX;
    float4 scaleYZOpacity;
    float4 rotation;
    float4 colorMark;
};
struct GPUAppearance {
    float4 packed[12];
    uint4 meta;
};
struct GPUSurfaceVertex { float4 position; float4 normal; float4 color; };
struct GPUUniforms {
    float4x4 viewProjection;
    float4 viewportAndScale;
    float4 renderParams;
    float4 cameraPositionAndFlags;
    float4 cameraRightAndFocalX;
    float4 cameraUpAndFocalY;
    float4 cameraBackAndMinSigma;
};
struct GPUDepthKey { float depth; uint sourceIndex; };
struct GPUDepthUniforms { float4 cameraPosition; float4 cameraForward; uint4 countAndFlags; };
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
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

float shScalar(device const GPUAppearance* appearance, uint coefficient, uint channel) {
    uint scalarIndex = coefficient * 3u + channel;
    uint vectorIndex = scalarIndex >> 2u;
    uint lane = scalarIndex & 3u;
    return appearance->packed[vectorIndex][lane];
}

float3 evaluateSH(device const GPUAppearance* appearance, float3 direction) {
    constexpr float c0 = 0.28209479177387814;
    constexpr float c1 = 0.4886025119029199;
    constexpr float c20 = 1.0925484305920792;
    constexpr float c21 = -1.0925484305920792;
    constexpr float c22 = 0.31539156525252005;
    constexpr float c23 = -1.0925484305920792;
    constexpr float c24 = 0.5462742152960396;
    constexpr float c30 = -0.5900435899266435;
    constexpr float c31 = 2.890611442640554;
    constexpr float c32 = -0.4570457994644658;
    constexpr float c33 = 0.3731763325901154;
    constexpr float c34 = -0.4570457994644658;
    constexpr float c35 = 1.445305721320277;
    constexpr float c36 = -0.5900435899266435;

    float3 d = normalize(direction);
    float x = d.x, y = d.y, z = d.z;
    float xx = x*x, yy = y*y, zz = z*z;
    uint count = min(appearance->meta.x, 16u);
    float3 color;
    for (uint channel = 0u; channel < 3u; ++channel) {
        float value = c0 * shScalar(appearance, 0u, channel);
        if (count >= 4u) {
            value += -c1*y*shScalar(appearance,1u,channel)
                   +  c1*z*shScalar(appearance,2u,channel)
                   -  c1*x*shScalar(appearance,3u,channel);
        }
        if (count >= 9u) {
            value += c20*x*y*shScalar(appearance,4u,channel)
                   + c21*y*z*shScalar(appearance,5u,channel)
                   + c22*(2.0*zz-xx-yy)*shScalar(appearance,6u,channel)
                   + c23*x*z*shScalar(appearance,7u,channel)
                   + c24*(xx-yy)*shScalar(appearance,8u,channel);
        }
        if (count >= 16u) {
            value += c30*y*(3.0*xx-yy)*shScalar(appearance,9u,channel)
                   + c31*x*y*z*shScalar(appearance,10u,channel)
                   + c32*y*(4.0*zz-xx-yy)*shScalar(appearance,11u,channel)
                   + c33*z*(2.0*zz-3.0*xx-3.0*yy)*shScalar(appearance,12u,channel)
                   + c34*x*(4.0*zz-xx-yy)*shScalar(appearance,13u,channel)
                   + c35*z*(xx-yy)*shScalar(appearance,14u,channel)
                   + c36*x*(xx-3.0*yy)*shScalar(appearance,15u,channel);
        }
        color[channel] = clamp(value + 0.5, 0.0, 1.0);
    }
    return color;
}

float3 cameraVector(float3 worldVector, constant GPUUniforms& u) {
    return float3(dot(u.cameraRightAndFocalX.xyz, worldVector),
                  dot(u.cameraUpAndFocalY.xyz, worldVector),
                  dot(u.cameraBackAndMinSigma.xyz, worldVector));
}

float2 jacobianProject(float3 cameraAxis, float3 cameraCenter, float depth,
                       constant GPUUniforms& u) {
    float invZ = 1.0 / max(depth, 1e-6);
    float invZ2 = invZ * invZ;
    float fx = u.cameraRightAndFocalX.w;
    float fy = u.cameraUpAndFocalY.w;
    return float2(fx * (cameraAxis.x * invZ + cameraCenter.x * cameraAxis.z * invZ2),
                  fy * (cameraAxis.y * invZ + cameraCenter.y * cameraAxis.z * invZ2));
}

vertex SplatOut splatVertex(uint vertexId [[vertex_id]],
                            uint instanceId [[instance_id]],
                            device const GPUSplat* splats [[buffer(0)]],
                            device const uint* order [[buffer(1)]],
                            constant GPUUniforms& u [[buffer(2)]],
                            device const GPUAppearance* appearances [[buffer(3)]]) {
    uint sourceIndex = order[instanceId];
    GPUSplat s = splats[sourceIndex];
    float3 center = s.positionScaleX.xyz;
    float3 scales = max(float3(s.positionScaleX.w, s.scaleYZOpacity.x, s.scaleYZOpacity.y), float3(1e-7));
    float4 q = normalize(s.rotation);
    float4 centerClip = u.viewProjection * float4(center, 1.0);

    float3 centerDelta = center - u.cameraPositionAndFlags.xyz;
    float3 cameraCenter = cameraVector(centerDelta, u);
    float depth = max(-cameraCenter.z, 1e-6);

    float3 ax = rotateByQuaternion(q, float3(scales.x, 0, 0));
    float3 ay = rotateByQuaternion(q, float3(0, scales.y, 0));
    float3 az = rotateByQuaternion(q, float3(0, 0, scales.z));
    float2 dx = jacobianProject(cameraVector(ax, u), cameraCenter, depth, u);
    float2 dy = jacobianProject(cameraVector(ay, u), cameraCenter, depth, u);
    float2 dz = jacobianProject(cameraVector(az, u), cameraCenter, depth, u);

    float a = dx.x*dx.x + dy.x*dy.x + dz.x*dz.x;
    float b = dx.x*dx.y + dy.x*dy.y + dz.x*dz.y;
    float c = dx.y*dx.y + dy.y*dy.y + dz.y*dz.y;
    float disc = sqrt(max(0.0, (a-c)*(a-c) + 4.0*b*b));
    float l0 = max(1e-12, 0.5*((a+c)+disc));
    float l1 = max(1e-12, 0.5*((a+c)-disc));
    float2 e0 = abs(b) > 1e-10 ? normalize(float2(b, l0-a)) : (a >= c ? float2(1,0) : float2(0,1));
    float2 e1 = float2(-e0.y, e0.x);
    float2 axis0 = e0 * sqrt(l0) * u.viewportAndScale.z;
    float2 axis1 = e1 * sqrt(l1) * u.viewportAndScale.z;

    float2 halfViewport = max(u.viewportAndScale.xy * 0.5, float2(1));
    float maxRadius = max(length(axis0*halfViewport), length(axis1*halfViewport));
    float minimumRadius = max(0.25, u.cameraBackAndMinSigma.w);
    if (maxRadius < minimumRadius) {
        float boost = minimumRadius / max(maxRadius, 1e-6);
        axis0 *= boost;
        axis1 *= boost;
    }
    maxRadius = max(length(axis0*halfViewport), length(axis1*halfViewport));
    float cap = max(8.0, u.viewportAndScale.w);
    if (maxRadius > cap) {
        float shrink = cap / maxRadius;
        axis0 *= shrink;
        axis1 *= shrink;
    }

    const float2 corners[4] = {float2(-1,-1),float2(1,-1),float2(-1,1),float2(1,1)};
    float2 corner = corners[vertexId & 3u];
    float2 offsetNdc = (axis0*corner.x + axis1*corner.y) * 3.0;
    SplatOut out;
    out.position = centerClip;
    out.position.xy += offsetNdc * centerClip.w;
    out.local = corner * 3.0;
    bool particle = u.renderParams.w > 0.5;
    bool useSH = !particle && u.cameraPositionAndFlags.w > 0.5 && appearances[sourceIndex].meta.x > 0u;
    out.color = useSH ? evaluateSH(appearances + sourceIndex, centerDelta) : s.colorMark.rgb;
    out.opacity = s.scaleYZOpacity.z;
    out.mark = s.colorMark.w;
    return out;
}

fragment float4 splatFragment(SplatOut in [[stage_in]], constant GPUUniforms& u [[buffer(2)]]) {
    float r2 = dot(in.local, in.local);
    if (r2 > 9.0) discard_fragment();
    float alpha = exp(-0.5*r2) * in.opacity * u.renderParams.x;
    if (alpha < 0.004) discard_fragment();
    float3 color = in.color;
    if (in.mark > 0.5 && u.renderParams.z > 0.5) color = mix(color, float3(1.0,0.28,0.035), 0.88);
    color = 1.0 - exp(-color * max(u.renderParams.y, 0.01) * 1.25);
    return float4(color, alpha);
}

vertex SurfaceOut surfaceVertex(uint vertexId [[vertex_id]],
                                device const GPUSurfaceVertex* vertices [[buffer(0)]],
                                constant GPUUniforms& u [[buffer(2)]]) {
    GPUSurfaceVertex v = vertices[vertexId];
    SurfaceOut out;
    out.position = u.viewProjection * float4(v.position.xyz, 1.0);
    out.normal = v.normal.xyz;
    out.color = v.color.rgb;
    return out;
}

fragment float4 surfaceFragment(SurfaceOut in [[stage_in]], constant GPUUniforms& u [[buffer(2)]]) {
    float3 n = normalize(in.normal);
    float kd = max(dot(n, normalize(float3(-0.45,0.75,0.85))), 0.0);
    float fd = max(dot(n, normalize(float3(0.75,0.15,0.55))), 0.0);
    float rim = pow(1.0-abs(n.z), 2.0);
    float3 base = in.color;
    if (u.renderParams.z < 0.5 && base.r > 0.8 && base.g < 0.65) base = float3(0.28,0.53,0.95);
    float3 color = base*(0.24 + 0.72*kd + 0.17*fd) + float3(0.18,0.28,0.52)*rim*0.18;
    color = 1.0 - exp(-color * max(u.renderParams.y,0.01) * 1.15);
    return float4(color,1.0);
}

vertex float4 lineVertex(uint vertexId [[vertex_id]],
                         device const float4* positions [[buffer(0)]],
                         constant GPUUniforms& u [[buffer(2)]]) {
    return u.viewProjection * positions[vertexId];
}
fragment float4 lineFragment() { return float4(0.22,0.36,0.58,0.38); }

kernel void generateDepthKeys(device const GPUSplat* splats [[buffer(0)]],
                              device const uint* order [[buffer(1)]],
                              device GPUDepthKey* output [[buffer(2)]],
                              constant GPUDepthUniforms& uniforms [[buffer(3)]],
                              uint gid [[thread_position_in_grid]]) {
    uint count = uniforms.countAndFlags.x;
    if (gid >= count) return;
    uint sourceIndex = order[gid];
    float3 delta = splats[sourceIndex].positionScaleX.xyz - uniforms.cameraPosition.xyz;
    output[gid].depth = dot(delta, uniforms.cameraForward.xyz);
    output[gid].sourceIndex = sourceIndex;
}
)METAL";

} // namespace vulkax::viewer::metal
