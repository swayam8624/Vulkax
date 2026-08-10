#include <cassert>
#include <iostream>

#include "vulkax/physics/medium_inference.hpp"
#include "vulkax/physics/scene_entity.hpp"

int main() {
  using namespace vulkax::physics;

  const auto surface = inferSimulationMedium("sin(x) * cos(y - 2*t)");
  assert(surface.medium == SimulationMedium::Surface2D);
  assert(surface.spatialDimensions == 2);
  assert(surface.confidence > 0.5);

  const auto volume = inferSimulationMedium(
      "density(x,y,z,t) + viscosity * laplacian(velocity) + pressure");
  assert(volume.medium == SimulationMedium::Volume3D);
  assert(volume.spatialDimensions == 3);
  assert(volume.geometryRecommended);

  const auto particles = inferSimulationMedium(
      "particle pairwise acceleration = sum_j((r_ji)/(pow(length(r_ji),3)+softening))");
  assert(particles.medium == SimulationMedium::ParticleSet);

  const auto relativity = inferSimulationMedium(
      "Kerr null geodesic with metric and Christoffel transport");
  assert(relativity.medium == SimulationMedium::RelativityRayBundle);
  assert(relativity.confidence > 0.8);

  const auto rigid = inferSimulationMedium(
      "torque = inertia * angular velocity; restitution + friction");
  assert(rigid.medium == SimulationMedium::RigidBody);
  assert(rigid.geometryRecommended);

  const auto ambiguous = inferSimulationMedium("amplitude * sin(t)");
  assert(ambiguous.medium == SimulationMedium::Trajectory);
  assert(ambiguous.confidence < 0.95);

  const auto forced = inferSimulationMedium(
      "sin(x)", SimulationMedium::Volume3D);
  assert(forced.medium == SimulationMedium::Volume3D);
  assert(forced.confidence == 1.0);
  assert(forced.reasons.size() == 1);

  SceneEntityDescriptor car{};
  car.id = "car-01";
  car.name = "Car";
  car.role = SceneEntityRole::FluidObstacle;
  car.medium = SimulationMedium::Volume3D;
  car.visualAsset = "car.glb";
  car.simulationProxyAsset = "car-collider.obj";
  car.collisionProxy = CollisionProxyKind::RenderMesh;
  assert(sceneRoleParticipatesInSimulation(car.role));
  assert(sceneRoleRequiresGeometry(car.role));

  SceneEntityDescriptor referenceModel{};
  referenceModel.role = SceneEntityRole::Visual;
  assert(!sceneRoleParticipatesInSimulation(referenceModel.role));

  std::cout << "Vulkax medium inference and scene semantics tests passed\n";
  return 0;
}
