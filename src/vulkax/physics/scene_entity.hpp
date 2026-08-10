#pragma once

#include "vulkax/physics/medium_inference.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace vulkax::physics {

enum class SceneEntityRole : uint32_t {
  Visual,
  Collider,
  FluidObstacle,
  Source,
  Probe,
  DomainSurface,
};

enum class CollisionProxyKind : uint32_t {
  None,
  RenderMesh,
  ConvexHull,
  Box,
  Sphere,
};

struct SceneTransform {
  std::array<double, 3> translation{0.0, 0.0, 0.0};
  std::array<double, 3> rotationEulerRadians{0.0, 0.0, 0.0};
  std::array<double, 3> scale{1.0, 1.0, 1.0};
};

// A scene asset is intentionally separate from its simulation proxy. A dense
// or non-watertight car/model mesh can remain a visual asset while a simpler
// closed mesh, convex hull or primitive participates in collision/voxelization.
struct SceneEntityDescriptor {
  std::string id;
  std::string name;
  SceneEntityRole role = SceneEntityRole::Visual;
  SimulationMedium medium = SimulationMedium::AbstractField;
  SceneTransform transform{};
  std::filesystem::path visualAsset;
  std::optional<std::filesystem::path> simulationProxyAsset;
  CollisionProxyKind collisionProxy = CollisionProxyKind::None;
  double massKilograms = 1.0;
  double restitution = 0.2;
  double friction = 0.5;
};

[[nodiscard]] constexpr bool sceneRoleParticipatesInSimulation(SceneEntityRole role) noexcept {
  return role != SceneEntityRole::Visual;
}

[[nodiscard]] constexpr bool sceneRoleRequiresGeometry(SceneEntityRole role) noexcept {
  return role == SceneEntityRole::Collider || role == SceneEntityRole::FluidObstacle ||
         role == SceneEntityRole::DomainSurface;
}

}  // namespace vulkax::physics
