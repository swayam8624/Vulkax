#pragma once

#include "vulkax/physics/stencil_ir.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vulkax::physics {

struct GeneratedTransportSpec {
  std::string field;
  std::array<double, 3> velocity{0.0, 0.0, 0.0};
  double diffusivity = 0.0;
  // Optional additive scalar source term expressed with the normal equation
  // grammar. It may reference x/y/z/t and parameter names supplied below.
  std::optional<equation::ScalarExpression> source;
  std::vector<std::string> parameterNames;
  double advectiveCfl = 0.7;
  double diffusionSafety = 0.9;
};

struct GeneratedTransportPlan {
  ScalarEvolutionProgram program;
  double advectiveTimestepLimit = 0.0;
  double diffusiveTimestepLimit = 0.0;
  double recommendedTimestep = 0.0;
  uint64_t canonicalHash = 0;
};

struct GeneratedTransportResult {
  std::optional<GeneratedTransportPlan> plan;
  std::vector<ValidationIssue> issues;
  [[nodiscard]] bool valid() const { return plan.has_value() && issues.empty(); }
};

// Generates a constant-velocity scalar advection/diffusion equation:
//   d(field)/dt = -v.x*d(field)/dx - v.y*d(field)/dy - v.z*d(field)/dz
//                 + diffusivity*laplacian(field) + source
// and lowers it through the normal executable stencil IR. This is deliberately
// not a universal PDE solver; unsupported symbolic coefficient fields should
// use an explicit Physics IR/stencil program instead.
[[nodiscard]] GeneratedTransportResult generateTransportDiffusionPlan(
    const PhysicsModel& model,
    const GeneratedTransportSpec& spec);

}  // namespace vulkax::physics
