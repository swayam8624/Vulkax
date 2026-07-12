#pragma once

#include "beacon/benchmark_config.hpp"
#include "beacon/beacon_research.hpp"

#include <vector>

namespace lve::beacon {

struct BenchmarkObject {
  glm::vec3 position{};
  float radius = 0.5f;
  uint32_t meshIndex = 0;
  uint32_t materialIndex = 0;
};

struct BenchmarkScene {
  std::vector<BenchmarkObject> objects;
  std::vector<GpuPointLight> lights;
  glm::vec3 worldMin{-12.f, -4.f, -40.f};
  glm::vec3 worldMax{12.f, 4.f, -1.f};
};

BenchmarkScene generateBenchmarkScene(const BenchmarkConfig& config, uint32_t frameIndex = 0);

}  // namespace lve::beacon
