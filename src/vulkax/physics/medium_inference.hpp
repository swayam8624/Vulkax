#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vulkax::physics {

enum class SimulationMedium : uint32_t {
  Surface2D,
  Volume3D,
  ParticleSet,
  RigidBody,
  VectorField,
  RelativityRayBundle,
  Trajectory,
  AbstractField,
};

struct MediumInferenceResult {
  SimulationMedium medium = SimulationMedium::AbstractField;
  double confidence = 0.0;
  uint32_t spatialDimensions = 0;
  bool geometryRecommended = false;
  std::vector<std::string> reasons;
};

[[nodiscard]] std::string_view simulationMediumName(SimulationMedium medium) noexcept;

// Infers the most useful *visualization/simulation domain* for an equation.
// This is deliberately heuristic: equations do not uniquely encode physical
// context. Callers must expose the returned reasons/confidence and allow a
// user override. When overrideMedium is supplied the result is deterministic
// and confidence is 1.0.
[[nodiscard]] MediumInferenceResult inferSimulationMedium(
    std::string_view equationSource,
    std::optional<SimulationMedium> overrideMedium = std::nullopt);

}  // namespace vulkax::physics
