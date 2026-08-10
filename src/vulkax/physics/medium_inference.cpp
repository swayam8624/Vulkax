#include "vulkax/physics/medium_inference.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>

namespace vulkax::physics {
namespace {

std::string normalized(std::string_view source) {
  std::string result;
  result.reserve(source.size());
  for (const unsigned char character : source) {
    result.push_back(static_cast<char>(std::tolower(character)));
  }
  return result;
}

bool containsAny(std::string_view source, std::initializer_list<std::string_view> needles) {
  return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
    return source.find(needle) != std::string_view::npos;
  });
}

bool hasStandaloneVariable(std::string_view source, char variable) {
  for (size_t index = 0; index < source.size(); ++index) {
    if (source[index] != variable) continue;
    const bool leftIdentifier =
        index > 0 && (std::isalnum(static_cast<unsigned char>(source[index - 1])) != 0 ||
                      source[index - 1] == '_');
    const bool rightIdentifier =
        index + 1 < source.size() &&
        (std::isalnum(static_cast<unsigned char>(source[index + 1])) != 0 ||
         source[index + 1] == '_');
    if (!leftIdentifier && !rightIdentifier) return true;
  }
  return false;
}

struct Candidate {
  double score = 0.0;
  uint32_t dimensions = 0;
  bool geometry = false;
  std::vector<std::string> reasons;
};

void add(
    std::unordered_map<SimulationMedium, Candidate>& candidates,
    SimulationMedium medium,
    double score,
    std::string reason,
    uint32_t dimensions,
    bool geometry = false) {
  Candidate& candidate = candidates[medium];
  candidate.score += score;
  candidate.dimensions = std::max(candidate.dimensions, dimensions);
  candidate.geometry = candidate.geometry || geometry;
  candidate.reasons.push_back(std::move(reason));
}

}  // namespace

std::string_view simulationMediumName(SimulationMedium medium) noexcept {
  switch (medium) {
    case SimulationMedium::Surface2D:
      return "2D surface field";
    case SimulationMedium::Volume3D:
      return "3D volume";
    case SimulationMedium::ParticleSet:
      return "particle set";
    case SimulationMedium::RigidBody:
      return "rigid body";
    case SimulationMedium::VectorField:
      return "vector field";
    case SimulationMedium::RelativityRayBundle:
      return "relativistic ray bundle";
    case SimulationMedium::Trajectory:
      return "trajectory / ODE";
    case SimulationMedium::AbstractField:
      return "abstract scalar field";
  }
  return "unknown";
}

MediumInferenceResult inferSimulationMedium(
    std::string_view equationSource,
    std::optional<SimulationMedium> overrideMedium) {
  if (overrideMedium.has_value()) {
    MediumInferenceResult result{};
    result.medium = *overrideMedium;
    result.confidence = 1.0;
    result.geometryRecommended =
        result.medium == SimulationMedium::RigidBody ||
        result.medium == SimulationMedium::Volume3D ||
        result.medium == SimulationMedium::Surface2D;
    result.spatialDimensions =
        result.medium == SimulationMedium::Surface2D ? 2u :
        (result.medium == SimulationMedium::Volume3D ||
         result.medium == SimulationMedium::RigidBody ||
         result.medium == SimulationMedium::RelativityRayBundle ? 3u : 0u);
    result.reasons.push_back("explicit user medium override");
    return result;
  }

  const std::string source = normalized(equationSource);
  std::unordered_map<SimulationMedium, Candidate> candidates;

  const bool hasX = hasStandaloneVariable(source, 'x');
  const bool hasY = hasStandaloneVariable(source, 'y');
  const bool hasZ = hasStandaloneVariable(source, 'z');
  const bool hasT = hasStandaloneVariable(source, 't');

  if (containsAny(source, {"kerr", "schwarzschild", "geodesic", "christoffel", "riemann",
                           "metric", "null ray", "null geodesic", "event horizon"})) {
    add(candidates, SimulationMedium::RelativityRayBundle, 8.0,
        "relativity/geodesic vocabulary detected", 3);
  }

  if (containsAny(source, {"navier", "stokes", "pressure", "viscosity", "vorticity",
                           "density", "temperature", "buoyancy", "incompressible"})) {
    add(candidates, SimulationMedium::Volume3D, 5.0,
        "continuum/fluid state vocabulary detected", 3, true);
  }

  if (containsAny(source, {"curl(", "div(", "divergence", "gradient", "grad(",
                           "electric field", "magnetic field", "velocity field"})) {
    add(candidates, SimulationMedium::VectorField, 4.0,
        "vector-calculus operator or field detected", hasZ ? 3u : 2u);
  }

  if (containsAny(source, {"particle", "n-body", "nbody", "r_ij", "r_ji", "sum_i", "sum_j",
                           "pairwise", "softening"})) {
    add(candidates, SimulationMedium::ParticleSet, 6.0,
        "particle/pairwise interaction vocabulary detected", 3);
  }

  if (containsAny(source, {"rigid", "inertia", "torque", "angular velocity", "quaternion",
                           "restitution", "friction"})) {
    add(candidates, SimulationMedium::RigidBody, 7.0,
        "rigid-body state vocabulary detected", 3, true);
  }

  if (containsAny(source, {"d2x/dt2", "d²x/dt²", "dx/dt", "dv/dt", "position(t)",
                           "velocity(t)", "trajectory"})) {
    add(candidates, SimulationMedium::Trajectory, 5.0,
        "time-parametric ODE/trajectory form detected", 0);
  }

  if (hasZ) {
    add(candidates, SimulationMedium::Volume3D, 3.0,
        "equation references x/y/z spatial coordinates", 3, true);
  } else if (hasX && hasY) {
    add(candidates, SimulationMedium::Surface2D, 3.0,
        "equation references a two-dimensional x/y domain", 2, true);
  } else if (hasX) {
    add(candidates, SimulationMedium::Surface2D, 1.5,
        "equation references a spatial coordinate", 2, true);
  }

  if (containsAny(source, {"laplacian", "nabla", "∇", "diffusion", "wave equation"})) {
    const auto target = hasZ ? SimulationMedium::Volume3D : SimulationMedium::Surface2D;
    add(candidates, target, 2.5, "PDE/spatial differential operator detected", hasZ ? 3u : 2u, true);
  }

  if (hasT && !hasX && !hasY && !hasZ) {
    add(candidates, SimulationMedium::Trajectory, 1.5,
        "time is the only independent coordinate", 0);
  }

  if (candidates.empty()) {
    return {
        SimulationMedium::AbstractField,
        0.35,
        0,
        false,
        {"no reliable spatial or physical-domain cue found"}};
  }

  const auto best = std::max_element(
      candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.second.score < right.second.score;
      });
  double runnerUp = 0.0;
  for (const auto& [medium, candidate] : candidates) {
    if (medium != best->first) runnerUp = std::max(runnerUp, candidate.score);
  }

  // Confidence captures both evidence strength and ambiguity. It is not a
  // probability that the inferred physics is "true".
  const double evidence = 1.0 - std::exp(-best->second.score / 4.0);
  const double separation = best->second.score <= 0.0
                                ? 0.0
                                : std::clamp(
                                      (best->second.score - runnerUp) / best->second.score,
                                      0.0,
                                      1.0);
  const double confidence = std::clamp(0.25 + 0.55 * evidence + 0.20 * separation, 0.0, 0.95);

  return {
      best->first,
      confidence,
      best->second.dimensions,
      best->second.geometry,
      best->second.reasons};
}

}  // namespace vulkax::physics
