#version 450

struct GaussianOutput {
    vec4 centerMajor;
    vec4 minorDepth;
    vec4 colorCull;
    vec4 tileBounds;
};

layout(std430, set = 0, binding = 0) readonly buffer ProjectedBuffer {
    GaussianOutput values[];
} projected;

layout(push_constant) uniform RasterParameters {
    float sigmaCutoff;
} parameters;

layout(location = 0) out vec2 outLocal;
layout(location = 1) out vec4 outColorOpacity;

const vec2 corners[6] = vec2[6](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0));

void main() {
    const uint splatIndex = uint(gl_VertexIndex) / 6u;
    const uint cornerIndex = uint(gl_VertexIndex) % 6u;
    const GaussianOutput splat = projected.values[splatIndex];
    const vec2 local = corners[cornerIndex];

    const vec2 clip = splat.centerMajor.xy +
                      local.x * splat.centerMajor.zw +
                      local.y * splat.minorDepth.xy;
    gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);
    outLocal = local * parameters.sigmaCutoff;
    outColorOpacity = vec4(
        splat.colorCull.rgb,
        clamp(splat.minorDepth.w, 0.0, 0.999));
}
