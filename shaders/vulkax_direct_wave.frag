#version 450

layout(set = 0, binding = 0) uniform sampler2D radianceImage;

layout(push_constant) uniform DirectFrameConstants {
  float timeSeconds;
  float width;
  float height;
  float exposure;
  float mode;
  float mass;
  float diskGain;
  float cameraScale;
  float spin;
  float sampleIndex;
  float observerInclination;
  float bundleScale;
} frame;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

vec3 acesFilm(vec3 value) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0, 1.0);
}

void main() {
  const vec2 texel = 1.0 / max(vec2(frame.width, frame.height), vec2(1.0));
  const vec3 radiance = texture(radianceImage, inUv).rgb;
  vec3 bloom = vec3(0.0);
  float weight = 0.0;
  const vec2 directions[8] = vec2[](
      vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0),
      vec2(0.7071, 0.7071), vec2(-0.7071, 0.7071),
      vec2(0.7071, -0.7071), vec2(-0.7071, -0.7071));
  for (uint index = 0u; index < 8u; ++index) {
    const float radius = index < 4u ? 2.5 : 5.0;
    const vec3 sampleRadiance = texture(radianceImage, inUv + directions[index] * texel * radius).rgb;
    const float luminance = dot(sampleRadiance, vec3(0.2126, 0.7152, 0.0722));
    const float highlight = smoothstep(0.8, 2.8, luminance);
    bloom += sampleRadiance * highlight;
    weight += highlight;
  }
  bloom /= max(weight, 1.0);
  vec3 glare = vec3(0.0);
  for (int offset = -3; offset <= 3; ++offset) {
    if (offset == 0) continue;
    const float distance = float(abs(offset));
    const vec2 diagonal = vec2(float(offset), float(offset)) * texel * 3.0;
    glare += texture(radianceImage, inUv + diagonal).rgb / (distance * distance + 1.0);
  }
  const float relativistic = frame.mode > 0.5 ? 1.0 : 0.45;
  vec3 displayLinear = radiance + relativistic * (0.12 * bloom + 0.018 * glare);
  const vec2 centered = inUv * 2.0 - 1.0;
  displayLinear *= 1.0 - 0.12 * smoothstep(0.35, 1.35, dot(centered, centered));
  vec3 display = acesFilm(displayLinear * max(frame.exposure, 0.0));
  const float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
  outColor = vec4(max(display + dither / 255.0, 0.0), 1.0);
}
