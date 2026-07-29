#include "vulkax/physics/physics_ir.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <charconv>
#include <map>
#include <sstream>
#include <string_view>

namespace vulkax::physics {
namespace {

const FieldDeclaration* findField(const PhysicsModel& model, const std::string& name) {
  const auto found = std::find_if(model.fields.begin(), model.fields.end(), [&](const FieldDeclaration& field) {
    return field.name == name;
  });
  return found == model.fields.end() ? nullptr : &*found;
}

Dimension inverseLength() { return Dimension::dimensionless().dividedBy(Dimension::length()); }
Dimension inverseTime() { return Dimension::dimensionless().dividedBy(Dimension::time()); }

std::string describe(ValueType type) {
  switch (type) {
    case ValueType::Scalar: return "scalar";
    case ValueType::Vector2: return "vector2";
    case ValueType::Vector3: return "vector3";
  }
  return "unknown";
}

std::string trim(std::string value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
    return std::isspace(character) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
    return std::isspace(character) != 0;
  }).base();
  return first >= last ? std::string{} : std::string{first, last};
}

std::vector<std::string> words(const std::string& value) {
  std::istringstream stream(value);
  std::vector<std::string> result;
  for (std::string word; stream >> word;) result.push_back(std::move(word));
  return result;
}

std::optional<double> parseDouble(const std::string& value) {
  double result = 0.0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
  return error == std::errc{} && end == value.data() + value.size() ? std::optional<double>{result} : std::nullopt;
}

std::optional<uint32_t> parseUnsigned(const std::string& value) {
  uint32_t result = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
  return error == std::errc{} && end == value.data() + value.size() ? std::optional<uint32_t>{result} : std::nullopt;
}

std::optional<ValueType> parseValueType(const std::string& value) {
  if (value == "scalar") return ValueType::Scalar;
  if (value == "vector2") return ValueType::Vector2;
  if (value == "vector3") return ValueType::Vector3;
  return std::nullopt;
}

std::optional<FieldPlacement> parsePlacement(const std::string& value) {
  if (value == "cell") return FieldPlacement::CellCenter;
  if (value == "face_x") return FieldPlacement::FaceX;
  if (value == "face_y") return FieldPlacement::FaceY;
  if (value == "face_z") return FieldPlacement::FaceZ;
  return std::nullopt;
}

std::optional<Dimension> parseDimension(const std::string& value) {
  static const std::map<std::string, Dimension> dimensions{
      {"1", Dimension::dimensionless()},
      {"m", Dimension::length()},
      {"s", Dimension::time()},
      {"kg", Dimension::mass()},
      {"m/s", Dimension::length().dividedBy(Dimension::time())},
      {"m/s2", Dimension::length().dividedBy(Dimension::time()).dividedBy(Dimension::time())},
      {"1/s", Dimension::dimensionless().dividedBy(Dimension::time())},
      {"m2/s", Dimension::length().multipliedBy(Dimension::length()).dividedBy(Dimension::time())},
      {"pa", Dimension{{{-1, 1, -2, 0, 0, 0, 0}}}},
  };
  const auto found = dimensions.find(value);
  return found == dimensions.end() ? std::nullopt : std::optional<Dimension>{found->second};
}

std::optional<BoundaryKind> parseBoundary(const std::string& value) {
  if (value == "open") return BoundaryKind::Open;
  if (value == "periodic") return BoundaryKind::Periodic;
  if (value == "no_slip") return BoundaryKind::NoSlip;
  if (value == "fixed") return BoundaryKind::FixedValue;
  return std::nullopt;
}

std::optional<OperatorTerm> parseTerm(std::string value) {
  value = trim(std::move(value));
  const auto parseFunction = [&](const std::string& name, DifferentialOperator operation) -> std::optional<OperatorTerm> {
    const std::string prefix = name + "(";
    if (!value.starts_with(prefix) || !value.ends_with(')')) return std::nullopt;
    const std::string field = trim(value.substr(prefix.size(), value.size() - prefix.size() - 1));
    return field.empty() ? std::nullopt : std::optional<OperatorTerm>{{operation, field}};
  };
  for (const auto [name, operation] : std::array{
           std::pair{"dt", DifferentialOperator::TimeDerivative},
           std::pair{"grad", DifferentialOperator::Gradient},
           std::pair{"div", DifferentialOperator::Divergence},
           std::pair{"curl", DifferentialOperator::Curl},
           std::pair{"laplacian", DifferentialOperator::Laplacian}}) {
    if (const auto parsed = parseFunction(name, operation)) return parsed;
  }
  return value.empty() ? std::nullopt : std::optional<OperatorTerm>{{DifferentialOperator::Identity, value}};
}

uint64_t fnv1a(uint64_t hash, std::string_view value) {
  constexpr uint64_t prime = 1099511628211ull;
  for (const char character : value) {
    hash ^= static_cast<uint8_t>(character);
    hash *= prime;
  }
  return hash;
}

uint64_t graphHash(const PhysicsModel& model, const std::vector<SolverPass>& passes) {
  uint64_t hash = 1469598103934665603ull;
  hash = fnv1a(hash, model.name);
  for (const uint32_t axis : model.domain.resolution) hash = fnv1a(hash, std::to_string(axis));
  hash = fnv1a(hash, std::to_string(static_cast<uint32_t>(model.solver.advection)));
  hash = fnv1a(hash, std::to_string(static_cast<uint32_t>(model.solver.pressure)));
  for (const InitialCondition& initial : model.initialConditions) {
    hash = fnv1a(hash, "i:" + initial.field);
    hash = fnv1a(hash, std::to_string(initial.value));
  }
  for (const SolverPass& pass : passes) {
    hash = fnv1a(hash, std::to_string(static_cast<uint32_t>(pass.kind)));
    hash = fnv1a(hash, pass.name);
    for (const std::string& resource : pass.reads) hash = fnv1a(hash, "r:" + resource);
    for (const std::string& resource : pass.writes) hash = fnv1a(hash, "w:" + resource);
  }
  return hash;
}

}  // namespace

bool ValidationResult::valid() const {
  return std::none_of(issues.begin(), issues.end(), [](const ValidationIssue& issue) { return issue.error; });
}

std::optional<TypedTerm> typeOf(const PhysicsModel& model, const OperatorTerm& term, std::string* error) {
  const FieldDeclaration* field = findField(model, term.field);
  if (field == nullptr) {
    if (error != nullptr) *error = "unknown field '" + term.field + "'";
    return std::nullopt;
  }
  TypedTerm typed{field->valueType, field->dimension};
  switch (term.operation) {
    case DifferentialOperator::Identity: break;
    case DifferentialOperator::TimeDerivative:
      typed.dimension = typed.dimension.multipliedBy(inverseTime());
      break;
    case DifferentialOperator::Gradient:
      if (typed.valueType != ValueType::Scalar) {
        if (error != nullptr) *error = "gradient requires a scalar field, got " + describe(typed.valueType);
        return std::nullopt;
      }
      typed.valueType = ValueType::Vector3;
      typed.dimension = typed.dimension.multipliedBy(inverseLength());
      break;
    case DifferentialOperator::Divergence:
      if (typed.valueType != ValueType::Vector3) {
        if (error != nullptr) *error = "divergence requires a vector3 field, got " + describe(typed.valueType);
        return std::nullopt;
      }
      typed.valueType = ValueType::Scalar;
      typed.dimension = typed.dimension.multipliedBy(inverseLength());
      break;
    case DifferentialOperator::Curl:
      if (typed.valueType != ValueType::Vector3) {
        if (error != nullptr) *error = "curl requires a vector3 field, got " + describe(typed.valueType);
        return std::nullopt;
      }
      typed.dimension = typed.dimension.multipliedBy(inverseLength());
      break;
    case DifferentialOperator::Laplacian:
      typed.dimension = typed.dimension.multipliedBy(inverseLength()).multipliedBy(inverseLength());
      break;
  }
  return typed;
}

ValidationResult validate(const PhysicsModel& model) {
  ValidationResult result{};
  if (model.name.empty()) result.issues.push_back({"physics model name cannot be empty"});
  for (size_t axis = 0; axis < 3; ++axis) {
    if (!(std::isfinite(model.domain.minimum[axis]) && std::isfinite(model.domain.maximum[axis])) ||
        model.domain.maximum[axis] <= model.domain.minimum[axis]) {
      result.issues.push_back({"domain bounds must be finite and increasing"});
      break;
    }
    if (model.domain.resolution[axis] < 2) {
      result.issues.push_back({"domain resolution must be at least two cells on every axis"});
      break;
    }
  }
  for (size_t index = 0; index < model.fields.size(); ++index) {
    if (model.fields[index].name.empty()) result.issues.push_back({"field name cannot be empty"});
    for (size_t other = index + 1; other < model.fields.size(); ++other) {
      if (model.fields[index].name == model.fields[other].name) {
        result.issues.push_back({"duplicate field '" + model.fields[index].name + "'"});
      }
    }
  }
  const auto validateEquation = [&](const Equation& equation, const char* category) {
    std::string message;
    const auto lhs = typeOf(model, equation.lhs, &message);
    if (!lhs) {
      result.issues.push_back({std::string{category} + " '" + equation.name + "': " + message});
      return;
    }
    if (equation.rhs.empty()) {
      result.issues.push_back({std::string{category} + " '" + equation.name + "' has no right-hand term"});
      return;
    }
    for (const OperatorTerm& rhsTerm : equation.rhs) {
      const auto rhs = typeOf(model, rhsTerm, &message);
      if (!rhs) {
        result.issues.push_back({std::string{category} + " '" + equation.name + "': " + message});
      } else if (rhs->valueType != lhs->valueType || rhs->dimension != lhs->dimension) {
        result.issues.push_back({std::string{category} + " '" + equation.name + "' has incompatible term types or dimensions"});
      }
    }
  };
  for (const Equation& equation : model.equations) validateEquation(equation, "equation");
  for (const Equation& constraint : model.constraints) validateEquation(constraint, "constraint");
  for (const BoundaryCondition& boundary : model.boundaries) {
    if (findField(model, boundary.field) == nullptr) {
      result.issues.push_back({"boundary references unknown field '" + boundary.field + "'"});
    }
    if (boundary.kind == BoundaryKind::FixedValue && !boundary.fixedValue.has_value()) {
      result.issues.push_back({"fixed-value boundary needs a value"});
    }
  }
  for (size_t index = 0; index < model.initialConditions.size(); ++index) {
    const InitialCondition& initial = model.initialConditions[index];
    if (findField(model, initial.field) == nullptr) {
      result.issues.push_back({"initial condition references unknown field '" + initial.field + "'"});
    }
    if (!std::isfinite(initial.value)) {
      result.issues.push_back({"initial condition value must be finite"});
    }
    for (size_t other = index + 1; other < model.initialConditions.size(); ++other) {
      if (initial.field == model.initialConditions[other].field) {
        result.issues.push_back({"duplicate initial condition for field '" + initial.field + "'"});
      }
    }
  }
  if (!(model.solver.timestepSeconds > 0.0) || !std::isfinite(model.solver.timestepSeconds)) {
    result.issues.push_back({"solver timestep must be finite and positive"});
  }
  if (model.visualization.volume && model.visualization.extinction < 0.0) {
    result.issues.push_back({"volume extinction cannot be negative"});
  }
  return result;
}

ParseResult parsePhysicsDsl(const std::string& source) {
  ParseResult result{};
  PhysicsModel model{};
  std::istringstream lines(source);
  uint32_t lineNumber = 0;
  const auto fail = [&](std::string message) { result.diagnostics.push_back({lineNumber, std::move(message)}); };
  for (std::string line; std::getline(lines, line);) {
    ++lineNumber;
    const size_t comment = line.find('#');
    if (comment != std::string::npos) line.resize(comment);
    line = trim(std::move(line));
    if (line.empty()) continue;
    const auto tokens = words(line);
    if (tokens.empty()) continue;
    if (tokens[0] == "model") {
      if (tokens.size() != 2) fail("model requires one identifier");
      else model.name = tokens[1];
    } else if (tokens[0] == "field") {
      if (tokens.size() != 5 || !tokens[1].ends_with(':')) {
        fail("field syntax is: field name: scalar|vector2|vector3 [unit] placement");
        continue;
      }
      const auto type = parseValueType(tokens[2]);
      const auto placement = parsePlacement(tokens[4]);
      if (!type || !placement || tokens[3].size() < 3 || tokens[3].front() != '[' || tokens[3].back() != ']') {
        fail("field has an invalid type, dimension, or placement");
        continue;
      }
      const auto dimension = parseDimension(tokens[3].substr(1, tokens[3].size() - 2));
      if (!dimension) {
        fail("field uses an unsupported dimension");
        continue;
      }
      model.fields.push_back({tokens[1].substr(0, tokens[1].size() - 1), *type, *dimension, *placement});
    } else if (tokens[0] == "domain") {
      if (tokens.size() != 7) {
        fail("domain requires min xyz followed by max xyz");
        continue;
      }
      bool parsed = true;
      for (size_t index = 0; index < 3; ++index) {
        const auto minimum = parseDouble(tokens[index + 1]);
        const auto maximum = parseDouble(tokens[index + 4]);
        if (!minimum || !maximum) { parsed = false; break; }
        model.domain.minimum[index] = *minimum;
        model.domain.maximum[index] = *maximum;
      }
      if (!parsed) fail("domain bounds must be decimal numbers");
    } else if (tokens[0] == "resolution") {
      if (tokens.size() != 4) {
        fail("resolution requires x y z integer values");
        continue;
      }
      bool parsed = true;
      for (size_t index = 0; index < 3; ++index) {
        const auto value = parseUnsigned(tokens[index + 1]);
        if (!value) { parsed = false; break; }
        model.domain.resolution[index] = *value;
      }
      if (!parsed) fail("resolution values must be unsigned integers");
    } else if (tokens[0] == "equation" || tokens[0] == "constraint") {
      const size_t colon = line.find(':');
      const size_t equals = line.find('=');
      if (colon == std::string::npos || equals == std::string::npos || colon > equals) {
        fail("equation syntax is: equation name: operator(field) = term");
        continue;
      }
      const std::string name = trim(line.substr(tokens[0].size(), colon - tokens[0].size()));
      const auto lhs = parseTerm(line.substr(colon + 1, equals - colon - 1));
      const auto rhs = parseTerm(line.substr(equals + 1));
      if (name.empty() || !lhs || !rhs) {
        fail("equation has an invalid name or term");
        continue;
      }
      Equation equation{name, *lhs, {*rhs}};
      if (tokens[0] == "equation") model.equations.push_back(std::move(equation));
      else model.constraints.push_back(std::move(equation));
    } else if (tokens[0] == "boundary") {
      if (tokens.size() < 3 || !tokens[1].ends_with(':')) {
        fail("boundary syntax is: boundary field: open|periodic|no_slip|fixed [value]");
        continue;
      }
      const auto kind = parseBoundary(tokens[2]);
      if (!kind) {
        fail("unsupported boundary kind");
        continue;
      }
      std::optional<double> value;
      if (*kind == BoundaryKind::FixedValue) {
        if (tokens.size() != 4 || !(value = parseDouble(tokens[3]))) {
          fail("fixed boundary requires a numeric value");
          continue;
        }
      } else if (tokens.size() != 3) {
        fail("only fixed boundaries accept a numeric value");
        continue;
      }
      model.boundaries.push_back({tokens[1].substr(0, tokens[1].size() - 1), *kind, value});
    } else if (tokens[0] == "initial") {
      const size_t equals = line.find('=');
      if (equals == std::string::npos) {
        fail("initial syntax is: initial field = numeric_value");
        continue;
      }
      const std::string field = trim(line.substr(tokens[0].size(), equals - tokens[0].size()));
      const auto value = parseDouble(trim(line.substr(equals + 1)));
      if (field.empty() || !value) {
        fail("initial requires a field name and numeric value");
        continue;
      }
      model.initialConditions.push_back({field, *value});
    } else if (tokens[0] == "solver") {
      if (tokens.size() != 7 || tokens[1] != "advection" || tokens[3] != "pressure" || tokens[5] != "timestep") {
        fail("solver syntax is: solver advection maccormack pressure multigrid timestep value");
        continue;
      }
      if (tokens[2] == "semi_lagrangian") model.solver.advection = AdvectionScheme::SemiLagrangian;
      else if (tokens[2] == "maccormack") model.solver.advection = AdvectionScheme::MacCormack;
      else if (tokens[2] == "bfecc") model.solver.advection = AdvectionScheme::Bfecc;
      else { fail("unsupported advection scheme"); continue; }
      if (tokens[4] == "jacobi") model.solver.pressure = PressureSolve::Jacobi;
      else if (tokens[4] == "multigrid") model.solver.pressure = PressureSolve::Multigrid;
      else if (tokens[4] == "conjugate_gradient") model.solver.pressure = PressureSolve::ConjugateGradient;
      else { fail("unsupported pressure solver"); continue; }
      const auto timestep = parseDouble(tokens[6]);
      if (!timestep) fail("solver timestep must be numeric");
      else model.solver.timestepSeconds = *timestep;
    } else if (tokens[0] == "visualize") {
      if (tokens.size() != 8 || tokens[1] != "volume" || tokens[2] != "extinction" || tokens[4] != "scattering" || tokens[6] != "phase") {
        fail("visualize syntax is: visualize volume extinction value scattering value phase value");
        continue;
      }
      const auto extinction = parseDouble(tokens[3]);
      const auto scattering = parseDouble(tokens[5]);
      const auto phase = parseDouble(tokens[7]);
      if (!extinction || !scattering || !phase) fail("visualization values must be numeric");
      else model.visualization = {true, *extinction, *scattering, *phase};
    } else {
      fail("unknown declaration '" + tokens[0] + "'");
    }
  }
  if (result.diagnostics.empty()) {
    const ValidationResult validation = validate(model);
    for (const ValidationIssue& issue : validation.issues) result.diagnostics.push_back({0, issue.message});
  }
  if (result.diagnostics.empty()) result.model = std::move(model);
  return result;
}

std::optional<SolverGraph> lowerToSolverGraph(
    const PhysicsModel& model, std::vector<ValidationIssue>* issues) {
  const ValidationResult validation = validate(model);
  if (!validation.valid()) {
    if (issues != nullptr) *issues = validation.issues;
    return std::nullopt;
  }
  const auto require = [&](const std::string& name, ValueType type) -> bool {
    const FieldDeclaration* field = findField(model, name);
    if (field != nullptr && field->valueType == type) return true;
    if (issues != nullptr) {
      issues->push_back({"solver lowering requires " + describe(type) + " field '" + name + "'"});
    }
    return false;
  };
  // This initial lowering targets the declared incompressible-flow vocabulary.
  // Models that do not describe these fields fail explicitly rather than being
  // silently executed as the legacy 2D reference simulation.
  if (!require("velocity", ValueType::Vector3) || !require("pressure", ValueType::Scalar) ||
      !require("density", ValueType::Scalar) || !require("temperature", ValueType::Scalar)) {
    return std::nullopt;
  }
  SolverGraph graph{};
  graph.initialConditions = model.initialConditions;
  graph.passes = {
      {SolverPassKind::AdvectVelocity, "advect_velocity", {"velocity"}, {"velocity_advected"}},
      {SolverPassKind::AdvectScalar, "advect_density", {"velocity_advected", "density"}, {"density_advected"}},
      {SolverPassKind::AdvectScalar, "advect_temperature", {"velocity_advected", "temperature"}, {"temperature_advected"}},
      {SolverPassKind::ApplyForces, "apply_buoyancy", {"velocity_advected", "density_advected", "temperature_advected"}, {"velocity_forced"}},
      {SolverPassKind::ComputeDivergence, "compute_divergence", {"velocity_forced"}, {"divergence"}},
      {SolverPassKind::SolvePressure, "solve_pressure", {"divergence", "pressure"}, {"pressure_solved"}},
      {SolverPassKind::ProjectVelocity, "project_velocity", {"velocity_forced", "pressure_solved"}, {"velocity_next"}},
  };
  if (model.visualization.volume) {
    graph.passes.push_back({SolverPassKind::DeriveOpticalProperties, "derive_optical_properties",
                            {"density_advected", "temperature_advected"}, {"extinction", "emission", "albedo"}});
  }
  graph.canonicalHash = graphHash(model, graph.passes);
  return graph;
}

PhysicsModel makeIncompressibleSmokeModel() {
  const Dimension velocity = Dimension::length().dividedBy(Dimension::time());
  const Dimension pressure{{{ -1, 1, -2, 0, 0, 0, 0 }}};
  PhysicsModel model{};
  model.name = "incompressible-smoke";
  model.domain.minimum = {-2.0, 0.0, -2.0};
  model.domain.maximum = {2.0, 6.0, 2.0};
  model.domain.resolution = {128, 192, 128};
  model.fields = {
      {"velocity", ValueType::Vector3, velocity, FieldPlacement::FaceX},
      {"pressure", ValueType::Scalar, pressure, FieldPlacement::CellCenter},
      {"density", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter},
      {"temperature", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter},
      {"divergence_source", ValueType::Scalar,
       Dimension::dimensionless().dividedBy(Dimension::time()), FieldPlacement::CellCenter},
  };
  // The pressure constraint is directly type-checked. In the canonical model
  // divergence_source is initialized to zero, but it retains physical units so
  // the equality remains dimensionally meaningful. Momentum is kept as a solver
  // graph target because its full RHS includes nonlinear products and material
  // coefficients not yet lowered by this minimal IR layer.
  model.constraints = {{"incompressibility", {DifferentialOperator::Divergence, "velocity"},
                        {{DifferentialOperator::Identity, "divergence_source"}}}};
  model.boundaries = {
      {"velocity", BoundaryKind::NoSlip, std::nullopt},
      {"density", BoundaryKind::Open, std::nullopt},
      {"temperature", BoundaryKind::Open, std::nullopt},
  };
  model.initialConditions = {
      {"velocity", 0.0},
      {"pressure", 0.0},
      {"density", 0.0},
      {"temperature", 0.0},
      {"divergence_source", 0.0},
  };
  model.solver = {AdvectionScheme::MacCormack, PressureSolve::Multigrid, true, 1.0 / 60.0};
  model.visualization = {true, 1.7, 0.6, 0.25};
  return model;
}

}  // namespace vulkax::physics
