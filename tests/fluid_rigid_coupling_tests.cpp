#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

#include "vulkax/sim/fluid_rigid_coupling.hpp"

int main() {
  using namespace vulkax::sim;
  const TriangleMesh box = makeBoxMesh({0.3, 0.2, 0.25});
  const MeshDiagnostics boxDiagnostics = analyzeTriangleMesh(box);
  assert(boxDiagnostics.watertight());
  TriangleMesh openTriangle{{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}},
                            {0, 1, 2}};
  const MeshDiagnostics openDiagnostics = analyzeTriangleMesh(openTriangle);
  assert(!openDiagnostics.watertight());
  assert(openDiagnostics.boundaryEdges == 3);
  bool rejectedOpenMesh = false;
  try {
    (void)voxelizeClosedMesh(openTriangle, RigidBodyState{}, VoxelDomain{});
  } catch (const std::invalid_argument&) {
    rejectedOpenMesh = true;
  }
  assert(rejectedOpenMesh);
  RigidBodyState body{};
  body.mass = 2.0;
  const VoxelDomain domain{{-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0}, {32, 32, 32}};
  const auto mask = voxelizeClosedMesh(box, body, domain);
  const auto occupied = std::accumulate(mask.begin(), mask.end(), uint64_t{0});
  assert(occupied > 100);
  assert(occupied < mask.size() / 4);

  constexpr double kPi = 3.14159265358979323846;
  body.orientation = {0.0, std::sin(kPi / 8.0), 0.0, std::cos(kPi / 8.0)};
  body.scale = {1.0, 1.4, 0.7};
  const Vec3d transformed = transformRigidPoint(body, {0.3, 0.0, 0.0});
  assert(std::abs(transformed.x - 0.3 / std::sqrt(2.0)) < 1e-12);
  assert(std::abs(transformed.z + 0.3 / std::sqrt(2.0)) < 1e-12);
  const auto transformedMask = voxelizeClosedMesh(box, body, domain);
  const auto transformedOccupied =
      std::accumulate(transformedMask.begin(), transformedMask.end(), uint64_t{0});
  assert(transformedOccupied > 50);
  body.orientation = {};
  body.scale = {1.0, 1.0, 1.0};

  const auto still = integrateFluidForce(
      box,
      body,
      [](Vec3d) { return 0.0; },
      [](Vec3d) { return Vec3d{}; },
      1.225,
      1.0);
  assert(std::abs(still.force.x) < 1e-12);
  assert(std::abs(still.force.y) < 1e-12);
  assert(std::abs(still.force.z) < 1e-12);

  const auto airflow = integrateFluidForce(
      box,
      body,
      [](Vec3d) { return 0.0; },
      [](Vec3d) { return Vec3d{12.0, 0.0, 0.0}; },
      1.225,
      1.05);
  assert(airflow.force.x > 1.0);
  assert(std::abs(airflow.force.y) < 1e-9);
  assert(std::abs(airflow.force.z) < 1e-9);
  const double oldX = body.position.x;
  advanceRigidBody(body, airflow, 1.0 / 60.0);
  assert(body.linearVelocity.x > 0.0);
  assert(body.position.x > oldX);

  body.angularVelocity = {0.0, 0.0, 2.0};
  const Vec3d surfacePoint{body.position.x, body.position.y + 0.5, body.position.z};
  const Vec3d surfaceVelocity = rigidPointVelocity(body, surfacePoint);
  assert(std::abs(surfaceVelocity.x - (body.linearVelocity.x - 1.0)) < 1e-12);
  const Quaterniond oldOrientation = body.orientation;
  advanceRigidBody(body, FluidForce{{}, {0.0, 3.0, 0.0}}, 1.0 / 60.0);
  const double orientationNorm = std::sqrt(
      body.orientation.x * body.orientation.x + body.orientation.y * body.orientation.y +
      body.orientation.z * body.orientation.z + body.orientation.w * body.orientation.w);
  assert(std::abs(orientationNorm - 1.0) < 1e-12);
  assert(body.orientation.x != oldOrientation.x || body.orientation.y != oldOrientation.y ||
         body.orientation.z != oldOrientation.z || body.orientation.w != oldOrientation.w);
  assert(body.angularVelocity.y > 0.0);

  std::vector<RigidBodyObject> objects(2);
  objects[0].mesh = makeBoxMesh({0.25, 0.20, 0.20});
  objects[1].mesh = objects[0].mesh;
  objects[0].body.position = {-0.18, 0.0, 0.0};
  objects[1].body.position = {0.18, 0.08, 0.0};
  objects[0].body.linearVelocity = {1.0, 0.5, 0.0};
  objects[1].body.linearVelocity = {-1.0, -0.2, 0.0};
  const ContactStats contacts = resolveRigidBodyContacts(objects, 0.25, 0.5);
  assert(contacts.testedPairs == 1);
  assert(contacts.resolvedContacts == 1);
  assert(contacts.maximumPenetration > 0.0);
  assert(objects[0].body.position.x < -0.18);
  assert(objects[1].body.position.x > 0.18);
  assert(objects[0].body.linearVelocity.x < 1.0);
  assert(objects[1].body.linearVelocity.x > -1.0);
  assert(std::abs(objects[0].body.angularVelocity.z) > 1e-6 ||
         std::abs(objects[1].body.angularVelocity.z) > 1e-6);

  std::cout << "Vulkax fluid-rigid coupling tests passed: occupied=" << occupied
            << " drag=" << airflow.force.x << '\n';
}
