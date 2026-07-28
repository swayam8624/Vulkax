#pragma once

#include <cstdint>
#include <vector>

namespace vulkax::sim {

struct BuoyantSmokeConfig {
  uint32_t width = 128;
  uint32_t height = 128;
  float timestepSeconds = 1.0f / 60.0f;
  float velocityDissipation = 0.995f;
  float densityDissipation = 0.998f;
  float temperatureDissipation = 0.995f;
  float buoyancy = 2.0f;
  float smokeWeight = 0.08f;
  float vorticityConfinement = 0.35f;
  uint32_t pressureIterations = 40;
  uint32_t seed = 1337;
};

// Deterministic 2D incompressible smoke reference. Every step applies an
// emitter, buoyancy, vorticity confinement, semi-Lagrangian advection, and a
// Jacobi pressure projection. It is deliberately a 2D solver: the rendered
// preview is a density/temperature field, not a claimed 3D volume render.
class BuoyantSmokeSimulation {
 public:
  explicit BuoyantSmokeSimulation(BuoyantSmokeConfig config = {});

  void reset();
  void step(uint32_t count = 1);

  [[nodiscard]] const BuoyantSmokeConfig& config() const { return config_; }
  [[nodiscard]] const std::vector<float>& density() const { return density_; }
  [[nodiscard]] const std::vector<float>& temperature() const { return temperature_; }
  [[nodiscard]] const std::vector<float>& velocityX() const { return velocityX_; }
  [[nodiscard]] const std::vector<float>& velocityY() const { return velocityY_; }
  [[nodiscard]] double divergenceL2() const;
  [[nodiscard]] double densityMass() const;
  [[nodiscard]] double densityCenterY() const;
  [[nodiscard]] double timeSeconds() const { return timeSeconds_; }

 private:
  [[nodiscard]] size_t index(uint32_t x, uint32_t y) const;
  [[nodiscard]] float sample(const std::vector<float>& field, float x, float y) const;
  void injectEmitter();
  void addBuoyancy();
  void applyVorticityConfinement();
  void advect(const std::vector<float>& source, std::vector<float>& destination, float dissipation);
  void project();
  void enforceBoundary(std::vector<float>& field, bool horizontalVelocity, bool verticalVelocity);

  BuoyantSmokeConfig config_;
  std::vector<float> density_;
  std::vector<float> temperature_;
  std::vector<float> velocityX_;
  std::vector<float> velocityY_;
  std::vector<float> scratchDensity_;
  std::vector<float> scratchTemperature_;
  std::vector<float> scratchVelocityX_;
  std::vector<float> scratchVelocityY_;
  std::vector<float> pressure_;
  std::vector<float> pressureScratch_;
  std::vector<float> divergence_;
  std::vector<float> curl_;
  double timeSeconds_ = 0.0;
};

}  // namespace vulkax::sim
