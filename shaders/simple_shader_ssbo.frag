#version 450

layout (location = 0) in vec3 fragColor;
layout (location = 1) in vec3 fragPosWorld;
layout (location = 2) in vec3 fragNormalWorld;

layout (location = 0) out vec4 outColor;

struct LegacyPointLight {
  vec4 position;
  vec4 color;
};

struct SsboPointLight {
  vec4 positionRadius;
  vec4 colorIntensity;
};

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 invView;
  vec4 ambientLightColor;
  LegacyPointLight pointLights[10];
  int numLights;
} ubo;

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer {
  SsboPointLight lights[];
} lightBuffer;

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  mat4 normalMatrix;
} push;

void main() {
  vec3 diffuseLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;
  vec3 specularLight = vec3(0.0);
  vec3 surfaceNormal = normalize(fragNormalWorld);
  vec3 cameraPosWorld = ubo.invView[3].xyz;
  vec3 viewDirection = normalize(cameraPosWorld - fragPosWorld);

  for (int i = 0; i < ubo.numLights; i++) {
    SsboPointLight light = lightBuffer.lights[i];
    vec3 directionToLight = light.positionRadius.xyz - fragPosWorld;
    float distanceSquared = dot(directionToLight, directionToLight);
    float radius = max(light.positionRadius.w, 0.0001);
    float radiusFactor = max(1.0 - distanceSquared / (radius * radius), 0.0);
    float attenuation = (radiusFactor * radiusFactor) / max(distanceSquared, 0.0001);
    directionToLight = normalize(directionToLight);

    float cosAngIncidence = max(dot(surfaceNormal, directionToLight), 0);
    vec3 intensity = light.colorIntensity.xyz * light.colorIntensity.w * attenuation;
    diffuseLight += intensity * cosAngIncidence;

    vec3 halfAngle = normalize(directionToLight + viewDirection);
    float blinnTerm = clamp(dot(surfaceNormal, halfAngle), 0, 1);
    blinnTerm = pow(blinnTerm, 512.0);
    specularLight += intensity * blinnTerm;
  }

  outColor = vec4(diffuseLight * fragColor + specularLight * fragColor, 1.0);
}
