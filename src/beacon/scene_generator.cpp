#include "beacon/scene_generator.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace lve::beacon {
namespace {

float lerp(float a, float b, float t) { return a + (b - a) * t; }

glm::vec3 randomInBox(std::mt19937& rng, const glm::vec3& min, const glm::vec3& max) {
  std::uniform_real_distribution<float> unit{0.f, 1.f};
  return {lerp(min.x, max.x, unit(rng)), lerp(min.y, max.y, unit(rng)), lerp(min.z, max.z, unit(rng))};
}

GpuPointLight makeLight(const glm::vec3& position, float radius, const glm::vec3& color, float intensity) {
  GpuPointLight light{};
  light.positionRadius = {position.x, position.y, position.z, radius};
  light.colorIntensity = {color.x, color.y, color.z, intensity};
  return light;
}

}  // namespace

BenchmarkScene generateBenchmarkScene(const BenchmarkConfig& config, uint32_t frameIndex) {
  BenchmarkScene scene{};
  std::mt19937 rng{config.randomSeed};
  std::uniform_real_distribution<float> unit{0.f, 1.f};
  std::uniform_real_distribution<float> signedUnit{-1.f, 1.f};
  std::uniform_real_distribution<float> color{0.2f, 1.f};

  scene.objects.reserve(config.objectCount);
  uint32_t columns = std::max<uint32_t>(1, static_cast<uint32_t>(std::sqrt(config.objectCount)));
  for (uint32_t i = 0; i < config.objectCount; ++i) {
    BenchmarkObject object{};
    if (config.scene == ScenePreset::DepthHeavy) {
      float z = lerp(scene.worldMax.z, scene.worldMin.z, static_cast<float>(i) / std::max(1u, config.objectCount - 1u));
      object.position = {signedUnit(rng) * 8.f, signedUnit(rng) * 2.5f, z};
    } else {
      float x = (static_cast<float>(i % columns) / std::max(1u, columns - 1u) - 0.5f) * 18.f;
      float z = (static_cast<float>(i / columns) / std::max(1u, columns - 1u));
      object.position = {x + signedUnit(rng) * 0.2f, signedUnit(rng) * 2.5f, lerp(-4.f, -35.f, z)};
    }
    object.radius = config.scene == ScenePreset::RepeatedGeometry ? 0.45f : lerp(0.25f, 0.9f, unit(rng));
    object.meshIndex = config.scene == ScenePreset::UniqueObjects ? i : i % 5;
    object.materialIndex = config.scene == ScenePreset::UniqueObjects ? i % 32 : i % 4;
    scene.objects.push_back(object);
  }

  scene.lights.reserve(config.lightCount);
  glm::vec3 hotspot{0.f, 0.f, -12.f};
  float swarmPhase = static_cast<float>(frameIndex) * 0.025f;
  for (uint32_t i = 0; i < config.lightCount; ++i) {
    glm::vec3 position{};
    float radius = 4.f;
    float intensity = 4.f;
    switch (config.lightDistribution) {
      case LightDistribution::SingleHotspot:
        position = hotspot + glm::vec3{signedUnit(rng) * 2.f, signedUnit(rng) * 1.5f, signedUnit(rng) * 2.f};
        break;
      case LightDistribution::MultiHotspot: {
        glm::vec3 centers[] = {{-6.f, 0.f, -8.f}, {5.f, 1.f, -17.f}, {1.f, -1.f, -28.f}};
        position = centers[i % 3] + glm::vec3{signedUnit(rng) * 1.8f, signedUnit(rng) * 1.4f, signedUnit(rng) * 1.8f};
        break;
      }
      case LightDistribution::DepthStacked:
        position = {signedUnit(rng) * 1.5f, signedUnit(rng) * 1.5f,
                    lerp(scene.worldMax.z, scene.worldMin.z, static_cast<float>(i) / std::max(1u, config.lightCount - 1u))};
        break;
      case LightDistribution::MovingSwarm:
        position = {std::sin(swarmPhase + i * 0.37f) * 7.f,
                    (unit(rng) * 2.f - 1.f) * 2.f,
                    -18.f + std::cos(swarmPhase + i * 0.19f) * 10.f};
        break;
      case LightDistribution::LargeRadiusAdversarial:
        position = randomInBox(rng, scene.worldMin, scene.worldMax);
        radius = 18.f;
        intensity = 2.5f;
        break;
      case LightDistribution::CameraAttached:
        position = {signedUnit(rng) * 2.f, signedUnit(rng) * 1.5f, -2.f - unit(rng) * 4.f};
        radius = 10.f;
        intensity = 5.f;
        break;
      case LightDistribution::Tutorial:
      case LightDistribution::Uniform:
      default:
        position = randomInBox(rng, scene.worldMin, scene.worldMax);
        break;
    }
    scene.lights.push_back(makeLight(position, radius, {color(rng), color(rng), color(rng)}, intensity));
  }

  return scene;
}

}  // namespace lve::beacon
