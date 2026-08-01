#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>

#include "vulkax/sim/fluid_rigid_coupling.hpp"

int main() {
  using namespace vulkax::sim;
  const TriangleMesh box = makeBoxMesh({0.3, 0.2, 0.25});
  RigidBodyState body{};
  body.mass = 2.0;
  const VoxelDomain domain{{-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0}, {32, 32, 32}};
  const auto mask = voxelizeClosedMesh(box, body, domain);
  const auto occupied = std::accumulate(mask.begin(), mask.end(), uint64_t{0});
  assert(occupied > 100);
  assert(occupied < mask.size() / 4);

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

  std::cout << "Vulkax fluid-rigid coupling tests passed: occupied=" << occupied
            << " drag=" << airflow.force.x << '\n';
}
