#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace vulkax::sim {

struct Vec3d {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Quaterniond {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double w = 1.0;
};

struct TriangleMesh {
  std::vector<Vec3d> vertices;
  std::vector<uint32_t> indices;
};

struct MeshDiagnostics {
  uint32_t invalidIndices = 0;
  uint32_t degenerateTriangles = 0;
  uint32_t boundaryEdges = 0;
  uint32_t nonManifoldEdges = 0;
  uint32_t inconsistentWindingEdges = 0;

  [[nodiscard]] bool watertight() const {
    return invalidIndices == 0 && degenerateTriangles == 0 && boundaryEdges == 0 &&
           nonManifoldEdges == 0 && inconsistentWindingEdges == 0;
  }
};

struct RigidBodyState {
  Vec3d position{};
  Quaterniond orientation{};
  Vec3d scale{1.0, 1.0, 1.0};
  Vec3d linearVelocity{};
  Vec3d angularVelocity{};
  double mass = 1.0;
  Vec3d diagonalInertia{1.0, 1.0, 1.0};
};

struct FluidForce {
  Vec3d force{};
  Vec3d torque{};
};

struct RigidBodyObject {
  TriangleMesh mesh;
  RigidBodyState body;
};

struct ContactStats {
  uint32_t testedPairs = 0;
  uint32_t resolvedContacts = 0;
  double maximumPenetration = 0.0;
};

struct VoxelDomain {
  Vec3d minimum{-1.0, -1.0, -1.0};
  Vec3d maximum{1.0, 1.0, 1.0};
  std::array<uint32_t, 3> resolution{32, 32, 32};
};

using ScalarSampler = std::function<double(Vec3d)>;
using VectorSampler = std::function<Vec3d(Vec3d)>;

// Reports topology defects that make closed-volume voxelization ambiguous.
// Multiple disconnected closed shells are valid; every undirected edge must
// have exactly two oppositely oriented incident triangles.
[[nodiscard]] MeshDiagnostics analyzeTriangleMesh(const TriangleMesh& mesh);

// Transforms a body-local point into world space using scale, orientation, and
// translation in that order.
[[nodiscard]] Vec3d transformRigidPoint(const RigidBodyState& body, Vec3d localPoint);

// Returns the world-space velocity of a point attached to the body, including
// the angular contribution around the centre of mass.
[[nodiscard]] Vec3d rigidPointVelocity(const RigidBodyState& body, Vec3d worldPoint);

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

// Resolves pairwise contacts between transformed mesh AABBs. Contact impulses
// include angular velocity, world-space inverse inertia, restitution, and
// Coulomb friction. The deterministic CPU path is the oracle for GPU multi-body
// coupling; narrow-phase triangle contacts can replace the AABB proxy later
// without changing body integration semantics.
[[nodiscard]] ContactStats resolveRigidBodyContacts(
    std::span<RigidBodyObject> objects,
    double restitution = 0.1,
    double friction = 0.4);

[[nodiscard]] TriangleMesh makeBoxMesh(Vec3d halfExtent);

}  // namespace vulkax::sim
