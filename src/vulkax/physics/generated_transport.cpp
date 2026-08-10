#include "vulkax/physics/generated_transport.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace vulkax::physics {
namespace {

using equation::EquationNode;
using equation::NodeKind;

EquationNode constant(double value) {
  EquationNode node{};
  node.kind = NodeKind::Constant;
  node.value = value;
  return node;
}

EquationNode variable(std::string name) {
  EquationNode node{};
  node.kind = NodeKind::Variable;
  node.symbol = std::move(name);
  return node;
}

EquationNode function(std::string name, EquationNode argument) {
  EquationNode node{};
  node.kind = NodeKind::Function;
  node.symbol = std::move(name);
  node.children.push_back(std::move(argument));
  return node;
}

EquationNode binary(NodeKind kind, EquationNode left, EquationNode right) {
  EquationNode node{};
  node.kind = kind;
  node.children.push_back(std::move(left));
  node.children.push_back(std::move(right));
  return node;
}

EquationNode scaled(double coefficient, EquationNode expression) {
  return binary(NodeKind::Multiply, constant(coefficient), std::move(expression));
}

EquationNode sum(std::vector<EquationNode> terms) {
  if (terms.empty()) return constant(0.0);
  EquationNode result = std::move(terms.front());
  for (size_t index = 1; index < terms.size(); ++index) {
    result = binary(NodeKind::Add, std::move(result), std::move(terms[index]));
  }
  return result;
}

uint64_t mix(uint64_t hash, uint64_t value) {
  constexpr uint64_t prime = 1099511628211ull;
  for (uint32_t shift = 0; shift < 64; shift += 8) {
    hash ^= static_cast<uint8_t>((value >> shift) & 0xffu);
    hash *= prime;
  }
  return hash;
}

}  // namespace

GeneratedTransportResult generateTransportDiffusionPlan(
    const PhysicsModel& model,
    const GeneratedTransportSpec& spec) {
  GeneratedTransportResult result{};
  if (spec.field.empty()) {
    result.issues.push_back({"generated transport field cannot be empty"});
    return result;
  }
  if (!std::isfinite(spec.diffusivity) || spec.diffusivity < 0.0) {
    result.issues.push_back({"transport diffusivity must be finite and non-negative"});
  }
  if (!std::isfinite(spec.advectiveCfl) || !(spec.advectiveCfl > 0.0) || spec.advectiveCfl > 1.0) {
    result.issues.push_back({"advective CFL must be finite and in (0, 1]"});
  }
  if (!std::isfinite(spec.diffusionSafety) || !(spec.diffusionSafety > 0.0) || spec.diffusionSafety > 1.0) {
    result.issues.push_back({"diffusion safety factor must be finite and in (0, 1]"});
  }
  for (const double velocity : spec.velocity) {
    if (!std::isfinite(velocity)) result.issues.push_back({"transport velocity must be finite"});
  }
  const auto field = std::find_if(model.fields.begin(), model.fields.end(), [&](const FieldDeclaration& declaration) {
    return declaration.name == spec.field;
  });
  if (field == model.fields.end()) result.issues.push_back({"transport field is not declared"});
  else if (field->valueType != ValueType::Scalar || field->placement != FieldPlacement::CellCenter) {
    result.issues.push_back({"generated transport requires a scalar cell-centred field"});
  }
  if (!result.issues.empty()) return result;

  std::vector<EquationNode> terms;
  const std::array<const char*, 3> gradientNames{"gradient_x", "gradient_y", "gradient_z"};
  for (size_t axis = 0; axis < 3; ++axis) {
    if (spec.velocity[axis] == 0.0) continue;
    terms.push_back(scaled(
        -spec.velocity[axis], function(gradientNames[axis], variable(spec.field))));
  }
  if (spec.diffusivity > 0.0) {
    terms.push_back(scaled(spec.diffusivity, function("laplacian", variable(spec.field))));
  }
  if (spec.source.has_value()) terms.push_back(spec.source->root());

  equation::ScalarExpression rhs{sum(std::move(terms))};
  auto lowered = lowerScalarEvolutionProgram(model, spec.field, rhs, spec.parameterNames);
  if (!lowered.valid()) {
    result.issues = std::move(lowered.issues);
    return result;
  }

  double advectiveRate = 0.0;
  double inverseSquaredSpacingSum = 0.0;
  for (size_t axis = 0; axis < 3; ++axis) {
    const double extent = model.domain.maximum[axis] - model.domain.minimum[axis];
    const double spacing = extent / static_cast<double>(model.domain.resolution[axis]);
    if (!(spacing > 0.0) || !std::isfinite(spacing)) {
      result.issues.push_back({"transport domain spacing must be finite and positive"});
      return result;
    }
    advectiveRate += std::abs(spec.velocity[axis]) / spacing;
    inverseSquaredSpacingSum += 1.0 / (spacing * spacing);
  }

  const double infinity = std::numeric_limits<double>::infinity();
  const double advectiveLimit = advectiveRate > 0.0 ? spec.advectiveCfl / advectiveRate : infinity;
  // Forward Euler + centred Laplacian stability for anisotropic 3D spacing:
  // dt <= 1 / (2 * D * sum(1/dx_i^2)). Apply an explicit safety factor.
  const double diffusiveLimit = spec.diffusivity > 0.0
      ? spec.diffusionSafety / (2.0 * spec.diffusivity * inverseSquaredSpacingSum)
      : infinity;
  const double recommended = std::min({
      advectiveLimit,
      diffusiveLimit,
      lowered.program->defaultTimestepSeconds});
  if (!(recommended > 0.0) || !std::isfinite(recommended)) {
    // If both physical limits are infinite, the model timestep is the finite
    // authoritative cadence. This branch mainly protects malformed models.
    if (!(lowered.program->defaultTimestepSeconds > 0.0) ||
        !std::isfinite(lowered.program->defaultTimestepSeconds)) {
      result.issues.push_back({"generated transport could not determine a stable timestep"});
      return result;
    }
  }

  GeneratedTransportPlan plan{};
  plan.program = std::move(*lowered.program);
  plan.advectiveTimestepLimit = advectiveLimit;
  plan.diffusiveTimestepLimit = diffusiveLimit;
  plan.recommendedTimestep = std::isfinite(recommended)
      ? recommended : plan.program.defaultTimestepSeconds;
  uint64_t hash = plan.program.canonicalHash;
  for (const double velocity : spec.velocity) {
    hash = mix(hash, std::bit_cast<uint64_t>(velocity));
  }
  hash = mix(hash, std::bit_cast<uint64_t>(spec.diffusivity));
  hash = mix(hash, std::bit_cast<uint64_t>(spec.advectiveCfl));
  hash = mix(hash, std::bit_cast<uint64_t>(spec.diffusionSafety));
  plan.canonicalHash = hash;
  result.plan = std::move(plan);
  return result;
}

}  // namespace vulkax::physics
