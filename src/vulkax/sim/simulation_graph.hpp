#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vulkax::sim {

enum class SimulationKind { Wave, ReactionDiffusion };

struct SimulationConfig {
  SimulationKind kind = SimulationKind::Wave;
  uint32_t width = 128;
  uint32_t height = 128;
  float timestepSeconds = 1.0f / 120.0f;
  float spatialStep = 0.1f;
  uint32_t seed = 1337;
  float diffusionA = 1.0f;
  float diffusionB = 0.5f;
  float feed = 0.0367f;
  float kill = 0.0649f;
};

struct FieldMetrics {
  float minimum = 0.0f;
  float maximum = 0.0f;
  float mean = 0.0f;
  float meanSquare = 0.0f;
};

// Deterministic reference implementation for the first compute graphs. Its
// update order and boundary conditions are intentionally explicit so future
// Vulkan kernels can be compared cell-for-cell against this implementation.
class SimulationGraph {
 public:
  explicit SimulationGraph(SimulationConfig config);

  void reset();
  void step(uint32_t count = 1);
  [[nodiscard]] const std::vector<float>& primaryField() const;
  [[nodiscard]] const std::vector<float>& secondaryField() const;
  [[nodiscard]] FieldMetrics metrics() const;
  [[nodiscard]] float timeSeconds() const { return timeSeconds_; }
  [[nodiscard]] const SimulationConfig& config() const { return config_; }
  [[nodiscard]] std::string glslComputeKernel() const;

 private:
  [[nodiscard]] size_t index(uint32_t x, uint32_t y) const;
  [[nodiscard]] float laplacian(const std::vector<float>& field, uint32_t x, uint32_t y) const;
  void stepWave();
  void stepReactionDiffusion();

  SimulationConfig config_;
  std::vector<float> previous_;
  std::vector<float> current_;
  std::vector<float> next_;
  std::vector<float> secondary_;
  std::vector<float> nextSecondary_;
  float timeSeconds_ = 0.0f;
};

}  // namespace vulkax::sim
