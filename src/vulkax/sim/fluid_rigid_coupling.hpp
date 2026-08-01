#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace vulkax::sim {

struct Vec3d {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct TriangleMesh {
  std::vector<Vec3d> vertices;
  std::vector<uint32_t> indices;
};

struct RigidBodyState {
  Vec3d position{};
  Vec3d linearVelocity{};
  Vec3d angularVelocity{};
  double mass = 1.0;
  Vec3d diagonalInertia{1.0, 1.0, 1.0};
};

struct FluidForce {
  Vec3d force{};
  Vec3d torque{};
};

struct VoxelDomain {
  Vec3d minimum{-1.0, -1.0, -1.0};
  Vec3d maximum{1.0, 1.0, 1.0};
  std::array<uint32_t, 3> resolution{32, 32, 32};
};

using ScalarSampler = std::function<double(Vec3d)>;
using VectorSampler = std::function<Vec3d(Vec3d)>;

// Produces a deterministic closed-volume mask by odd-even ray classification.
// Meshes must be watertight and consistently indexed for physical coupling.
[[nodiscard]] std::vector<uint8_t> voxelizeClosedMesh(
    const TriangleMesh& mesh, const RigidBodyState& body, const VoxelDomain& domain);

// Integrates pressure and quadratic normal drag over mesh triangles. This is a
// conservative surface coupling reference suitable for validating GPU kernels.
[[nodiscard]] FluidForce integrateFluidForce(
    const TriangleMesh& mesh,
    const RigidBodyState& body,
    const ScalarSampler& pressure,
    const VectorSampler& velocity,
    double fluidDensity,
    double dragCoefficient);

void advanceRigidBody(RigidBodyState& body, const FluidForce& force, double timestepSeconds);

[[nodiscard]] TriangleMesh makeBoxMesh(Vec3d halfExtent);

}  // namespace vulkax::sim
