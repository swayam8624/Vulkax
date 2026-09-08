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
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

float2 projectedDelta(float3 center, float3 axis, float4 centerClip, float4x4 vp) {
    float4 p = vp * float4(center + axis, 1.0);
    float cw = abs(centerClip.w) < 1e-6 ? copysign(1e-6, centerClip.w) : centerClip.w;
    float pw = abs(p.w) < 1e-6 ? copysign(1e-6, p.w) : p.w;
    return p.xy / pw - centerClip.xy / cw;
}

vertex SplatOut splatVertex(uint vertexId [[vertex_id]],
                            uint instanceId [[instance_id]],
                            constant GPUSplat* splats [[buffer(0)]],
                            constant uint* order [[buffer(1)]],
                            constant GPUUniforms& u [[buffer(2)]]) {
    GPUSplat s = splats[order[instanceId]];
    float3 center = s.positionScaleX.xyz;
    float3 scales = max(float3(s.positionScaleX.w, s.scaleYZOpacity.x, s.scaleYZOpacity.y), float3(1e-7));
    float4 q = normalize(s.rotation);
    float4 centerClip = u.viewProjection * float4(center, 1.0);

    float3 ax = rotateByQuaternion(q, float3(scales.x, 0, 0));
    float3 ay = rotateByQuaternion(q, float3(0, scales.y, 0));
    float3 az = rotateByQuaternion(q, float3(0, 0, scales.z));
    float2 dx = projectedDelta(center, ax, centerClip, u.viewProjection);
    float2 dy = projectedDelta(center, ay, centerClip, u.viewProjection);
    float2 dz = projectedDelta(center, az, centerClip, u.viewProjection);

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
    if (maxRadius < 0.75) {
        float boost = 0.75 / max(maxRadius, 1e-6);
        axis0 *= boost; axis1 *= boost;
    }
    maxRadius = max(length(axis0*halfViewport), length(axis1*halfViewport));
    float cap = max(8.0, u.viewportAndScale.w);
    if (maxRadius > cap) {
        float shrink = cap / maxRadius;
        axis0 *= shrink; axis1 *= shrink;
    }

    const float2 corners[4] = {float2(-1,-1),float2(1,-1),float2(-1,1),float2(1,1)};
    float2 corner = corners[vertexId & 3u];
    float2 offsetNdc = (axis0*corner.x + axis1*corner.y) * 3.0;
    SplatOut out;
    out.position = centerClip;
    out.position.xy += offsetNdc * centerClip.w;
    out.local = corner * 3.0;
    out.color = s.colorMark.rgb;
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
                                constant GPUSurfaceVertex* vertices [[buffer(0)]],
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
                         constant float4* positions [[buffer(0)]],
                         constant GPUUniforms& u [[buffer(2)]]) {
    return u.viewProjection * positions[vertexId];
}
fragment float4 lineFragment() { return float4(0.22,0.36,0.58,0.38); }
)METAL";

} // namespace vulkax::viewer::metal
