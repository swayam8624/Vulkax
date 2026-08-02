#include "vulkax/sim/fluid_rigid_coupling.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::sim {
namespace {

Vec3d operator+(Vec3d a, Vec3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3d operator-(Vec3d a, Vec3d b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3d operator*(Vec3d a, double scale) { return {a.x * scale, a.y * scale, a.z * scale}; }
Vec3d operator/(Vec3d a, double scale) { return a * (1.0 / scale); }
double dot(Vec3d a, Vec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3d cross(Vec3d a, Vec3d b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(Vec3d value) { return std::sqrt(dot(value, value)); }

Quaterniond operator*(Quaterniond a, Quaterniond b) {
  return {
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quaterniond normalize(Quaterniond value) {
  const double magnitude = std::sqrt(
      value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
  if (!(magnitude > 1e-15) || !std::isfinite(magnitude)) {
    throw std::invalid_argument("rigid body orientation must be finite and non-zero");
  }
  return {
      value.x / magnitude, value.y / magnitude, value.z / magnitude, value.w / magnitude};
}

Quaterniond conjugate(Quaterniond value) {
  return {-value.x, -value.y, -value.z, value.w};
}

Vec3d rotate(Quaterniond orientation, Vec3d value) {
  orientation = normalize(orientation);
  const Quaterniond vector{value.x, value.y, value.z, 0.0};
  const Quaterniond rotated = orientation * vector * conjugate(orientation);
  return {rotated.x, rotated.y, rotated.z};
}

Vec3d componentMultiply(Vec3d a, Vec3d b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3d worldVertex(const TriangleMesh& mesh, const RigidBodyState& body, uint32_t index) {
  if (index >= mesh.vertices.size()) throw std::out_of_range("mesh index is out of range");
  return transformRigidPoint(body, mesh.vertices[index]);
}

bool rayHitsPositiveX(Vec3d origin, Vec3d a, Vec3d b, Vec3d c) {
  const Vec3d direction{1.0, 0.0, 0.0};
  const Vec3d edge1 = b - a;
  const Vec3d edge2 = c - a;
  const Vec3d p = cross(direction, edge2);
  const double determinant = dot(edge1, p);
  if (std::abs(determinant) < 1e-12) return false;
  const double inverse = 1.0 / determinant;
  const Vec3d t = origin - a;
  const double u = dot(t, p) * inverse;
  if (u < 0.0 || u > 1.0) return false;
  const Vec3d q = cross(t, edge1);
  const double v = dot(direction, q) * inverse;
  if (v < 0.0 || u + v > 1.0) return false;
  return dot(edge2, q) * inverse > 1e-10;
}

}  // namespace

Vec3d transformRigidPoint(const RigidBodyState& body, Vec3d localPoint) {
  if (body.scale.x <= 0.0 || body.scale.y <= 0.0 || body.scale.z <= 0.0) {
    throw std::invalid_argument("rigid body scale must be positive");
  }
  return body.position + rotate(body.orientation, componentMultiply(localPoint, body.scale));
}

Vec3d rigidPointVelocity(const RigidBodyState& body, Vec3d worldPoint) {
  return body.linearVelocity + cross(body.angularVelocity, worldPoint - body.position);
}

std::vector<uint8_t> voxelizeClosedMesh(
    const TriangleMesh& mesh, const RigidBodyState& body, const VoxelDomain& domain) {
  if (mesh.indices.size() % 3 != 0) throw std::invalid_argument("mesh indices must form triangles");
  const auto resolution = domain.resolution;
  const size_t cellCount = static_cast<size_t>(resolution[0]) * resolution[1] * resolution[2];
  std::vector<uint8_t> mask(cellCount, 0);
  const Vec3d span = domain.maximum - domain.minimum;
  for (uint32_t z = 0; z < resolution[2]; ++z) {
    for (uint32_t y = 0; y < resolution[1]; ++y) {
      for (uint32_t x = 0; x < resolution[0]; ++x) {
        Vec3d point{
            domain.minimum.x + (x + 0.5) * span.x / resolution[0],
            domain.minimum.y + (y + 0.5) * span.y / resolution[1],
            domain.minimum.z + (z + 0.5) * span.z / resolution[2]};
        // Offset two coordinates irrationally relative to the grid to avoid
        // double-counting shared triangle edges in the parity test.
        point.y += 1.0e-9;
        point.z += 1.7e-9;
        uint32_t intersections = 0;
        for (size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
          const Vec3d a = worldVertex(mesh, body, mesh.indices[triangle]);
          const Vec3d b = worldVertex(mesh, body, mesh.indices[triangle + 1]);
          const Vec3d c = worldVertex(mesh, body, mesh.indices[triangle + 2]);
          if (rayHitsPositiveX(point, a, b, c)) ++intersections;
        }
        mask[(static_cast<size_t>(z) * resolution[1] + y) * resolution[0] + x] =
            static_cast<uint8_t>(intersections & 1u);
      }
    }
  }
  return mask;
}

FluidForce integrateFluidForce(
    const TriangleMesh& mesh,
    const RigidBodyState& body,
    const ScalarSampler& pressure,
    const VectorSampler& velocity,
    double fluidDensity,
    double dragCoefficient) {
  if (mesh.indices.size() % 3 != 0 || body.mass <= 0.0 || fluidDensity < 0.0 ||
      dragCoefficient < 0.0) {
    throw std::invalid_argument("invalid fluid-rigid coupling input");
  }
  FluidForce result{};
  for (size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
    const Vec3d a = worldVertex(mesh, body, mesh.indices[triangle]);
    const Vec3d b = worldVertex(mesh, body, mesh.indices[triangle + 1]);
    const Vec3d c = worldVertex(mesh, body, mesh.indices[triangle + 2]);
    const Vec3d areaNormal = cross(b - a, c - a) * 0.5;
    const double area = length(areaNormal);
    if (area <= 1e-14) continue;
    const Vec3d normal = areaNormal / area;
    const Vec3d centroid = (a + b + c) / 3.0;
    const Vec3d relativeVelocity = velocity(centroid) - rigidPointVelocity(body, centroid);
    const double incidentSpeed = std::max(0.0, -dot(relativeVelocity, normal));
    const Vec3d pressureForce = normal * (-pressure(centroid) * area);
    const Vec3d dragForce =
        normal * (-0.5 * fluidDensity * dragCoefficient * incidentSpeed * incidentSpeed * area);
    const Vec3d triangleForce = pressureForce + dragForce;
    result.force = result.force + triangleForce;
    result.torque = result.torque + cross(centroid - body.position, triangleForce);
  }
  return result;
}

void advanceRigidBody(RigidBodyState& body, const FluidForce& force, double timestepSeconds) {
  if (body.mass <= 0.0 || timestepSeconds < 0.0 || body.diagonalInertia.x <= 0.0 ||
      body.diagonalInertia.y <= 0.0 || body.diagonalInertia.z <= 0.0) {
    throw std::invalid_argument("rigid body mass, inertia, and timestep must be valid");
  }
  body.linearVelocity = body.linearVelocity + force.force * (timestepSeconds / body.mass);
  body.position = body.position + body.linearVelocity * timestepSeconds;
  const Quaterniond orientation = normalize(body.orientation);
  const Vec3d bodyTorque = rotate(conjugate(orientation), force.torque);
  const Vec3d bodyAngularAcceleration{
      bodyTorque.x / body.diagonalInertia.x,
      bodyTorque.y / body.diagonalInertia.y,
      bodyTorque.z / body.diagonalInertia.z};
  body.angularVelocity =
      body.angularVelocity + rotate(orientation, bodyAngularAcceleration) * timestepSeconds;
  const Quaterniond spin{
      body.angularVelocity.x, body.angularVelocity.y, body.angularVelocity.z, 0.0};
  const Quaterniond derivative = spin * orientation;
  body.orientation = normalize({
      orientation.x + 0.5 * derivative.x * timestepSeconds,
      orientation.y + 0.5 * derivative.y * timestepSeconds,
      orientation.z + 0.5 * derivative.z * timestepSeconds,
      orientation.w + 0.5 * derivative.w * timestepSeconds});
}

TriangleMesh makeBoxMesh(Vec3d h) {
  TriangleMesh mesh{};
  mesh.vertices = {
      {-h.x, -h.y, -h.z},
      {h.x, -h.y, -h.z},
      {h.x, h.y, -h.z},
      {-h.x, h.y, -h.z},
      {-h.x, -h.y, h.z},
      {h.x, -h.y, h.z},
      {h.x, h.y, h.z},
      {-h.x, h.y, h.z}};
  mesh.indices = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
                  1, 2, 6, 1, 6, 5, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2};
  return mesh;
}

}  // namespace vulkax::sim
