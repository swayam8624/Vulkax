#include "vulkax/physics/stencil_ir.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace vulkax::physics {
namespace {

std::string number(double value) {
  std::ostringstream stream;
  stream << std::setprecision(17) << value;
  std::string result = stream.str();
  if (result.find_first_of(".eE") == std::string::npos) result += ".0";
  return result;
}

uint64_t fnv1a(uint64_t hash, std::string_view value) {
  constexpr uint64_t prime = 1099511628211ull;
  for (const unsigned char character : value) {
    hash ^= character;
    hash *= prime;
  }
  return hash;
}

uint64_t programHash(const ScalarEvolutionProgram& program) {
  uint64_t hash = 1469598103934665603ull;
  hash = fnv1a(hash, program.field);
  hash = fnv1a(hash, std::to_string(static_cast<uint32_t>(program.boundary)));
  hash = fnv1a(hash, number(program.fixedBoundaryValue));
  hash = fnv1a(hash, number(program.defaultTimestepSeconds));
  for (size_t field = 0; field < program.inputFields.size(); ++field) {
    hash = fnv1a(hash, "f:" + program.inputFields[field]);
    hash = fnv1a(hash, std::to_string(static_cast<uint32_t>(program.inputBoundaries[field])));
    hash = fnv1a(hash, number(program.inputFixedBoundaryValues[field]));
  }
  for (size_t axis = 0; axis < 3; ++axis) {
    hash = fnv1a(hash, number(program.domain.minimum[axis]));
    hash = fnv1a(hash, number(program.domain.maximum[axis]));
    hash = fnv1a(hash, std::to_string(program.domain.resolution[axis]));
  }
  for (const std::string& parameter : program.parameterNames) hash = fnv1a(hash, "p:" + parameter);
  for (const StencilInstruction& instruction : program.instructions) {
    hash = fnv1a(hash, std::to_string(static_cast<uint32_t>(instruction.opcode)));
    hash = fnv1a(hash, std::to_string(instruction.operandCount));
    for (uint8_t index = 0; index < instruction.operandCount; ++index) {
      hash = fnv1a(hash, std::to_string(instruction.operands[index]));
    }
    for (const int8_t offset : instruction.sampleOffset) {
      hash = fnv1a(hash, std::to_string(static_cast<int>(offset)));
    }
    hash = fnv1a(hash, number(instruction.immediate));
    hash = fnv1a(hash, std::to_string(instruction.parameterIndex));
    hash = fnv1a(hash, std::to_string(instruction.fieldIndex));
  }
  return hash;
}

struct LoweringContext {
  ScalarEvolutionProgram program;
  std::vector<ValidationIssue> issues;
  std::unordered_map<std::string, uint32_t> commonExpressions;

  uint32_t append(StencilInstruction instruction, const std::string& key) {
    if (const auto found = commonExpressions.find(key); found != commonExpressions.end()) return found->second;
    const uint32_t index = static_cast<uint32_t>(program.instructions.size());
    program.instructions.push_back(instruction);
    commonExpressions.emplace(key, index);
    return index;
  }

  uint32_t constant(double value) {
    StencilInstruction instruction{};
    instruction.immediate = value;
    return append(instruction, "constant:" + number(value));
  }

  uint32_t sample(uint32_t fieldIndex, std::array<int8_t, 3> offset) {
    StencilInstruction instruction{StencilOpcode::FieldSample};
    instruction.sampleOffset = offset;
    instruction.fieldIndex = fieldIndex;
    return append(instruction, "sample:" + std::to_string(fieldIndex) + ":" +
                                   std::to_string(offset[0]) + ":" +
                                   std::to_string(offset[1]) + ":" + std::to_string(offset[2]));
  }

  uint32_t unary(StencilOpcode opcode, uint32_t operand) {
    StencilInstruction instruction{opcode, {operand, 0, 0}, {}, 1};
    return append(instruction, std::to_string(static_cast<uint32_t>(opcode)) + ":" +
                                   std::to_string(operand));
  }

  uint32_t binary(StencilOpcode opcode, uint32_t left, uint32_t right) {
    StencilInstruction instruction{opcode, {left, right, 0}, {}, 2};
    return append(instruction, std::to_string(static_cast<uint32_t>(opcode)) + ":" +
                                   std::to_string(left) + ":" + std::to_string(right));
  }

  uint32_t differential(const equation::EquationNode& node) {
    if (node.children.size() != 1 || node.children[0].kind != equation::NodeKind::Variable) {
      issues.push_back({node.symbol + " requires one declared scalar field"});
      return constant(0.0);
    }
    const auto field = std::find(
        program.inputFields.begin(), program.inputFields.end(), node.children[0].symbol);
    if (field == program.inputFields.end()) {
      issues.push_back({node.symbol + " references unknown input field '" +
                        node.children[0].symbol + "'"});
      return constant(0.0);
    }
    const uint32_t fieldIndex = static_cast<uint32_t>(
        std::distance(program.inputFields.begin(), field));
    if (node.symbol == "laplacian") {
      uint32_t result = constant(0.0);
      const uint32_t center = sample(fieldIndex, {0, 0, 0});
      for (size_t axis = 0; axis < 3; ++axis) {
        std::array<int8_t, 3> negative{};
        std::array<int8_t, 3> positive{};
        negative[axis] = -1;
        positive[axis] = 1;
        const double spacing = (program.domain.maximum[axis] - program.domain.minimum[axis]) /
            static_cast<double>(program.domain.resolution[axis]);
        const uint32_t neighbours = binary(
            StencilOpcode::Add, sample(fieldIndex, negative), sample(fieldIndex, positive));
        const uint32_t twiceCenter = binary(StencilOpcode::Multiply, constant(2.0), center);
        const uint32_t secondDifference = binary(StencilOpcode::Subtract, neighbours, twiceCenter);
        const uint32_t scaled = binary(
            StencilOpcode::Multiply, secondDifference, constant(1.0 / (spacing * spacing)));
        result = binary(StencilOpcode::Add, result, scaled);
      }
      return result;
    }
    const std::array<std::pair<std::string_view, size_t>, 3> gradients{{
        {"gradient_x", 0}, {"gradient_y", 1}, {"gradient_z", 2}}};
    for (const auto& [name, axis] : gradients) {
      if (node.symbol != name) continue;
      std::array<int8_t, 3> negative{};
      std::array<int8_t, 3> positive{};
      negative[axis] = -1;
      positive[axis] = 1;
      const double spacing = (program.domain.maximum[axis] - program.domain.minimum[axis]) /
          static_cast<double>(program.domain.resolution[axis]);
      return binary(
          StencilOpcode::Multiply,
          binary(StencilOpcode::Subtract,
                 sample(fieldIndex, positive), sample(fieldIndex, negative)),
          constant(0.5 / spacing));
    }
    issues.push_back({"unsupported stencil function '" + node.symbol + "'"});
    return constant(0.0);
  }

  uint32_t lower(const equation::EquationNode& node) {
    const auto lowerUnary = [&](StencilOpcode opcode, const char* name) {
      if (node.children.size() != 1) {
        issues.push_back({std::string{name} + " requires one operand"});
        return constant(0.0);
      }
      return unary(opcode, lower(node.children[0]));
    };
    const auto lowerBinary = [&](StencilOpcode opcode, const char* name) {
      if (node.children.size() != 2) {
        issues.push_back({std::string{name} + " requires two operands"});
        return constant(0.0);
      }
      return binary(opcode, lower(node.children[0]), lower(node.children[1]));
    };
    switch (node.kind) {
      case equation::NodeKind::Constant: return constant(node.value);
      case equation::NodeKind::Variable: {
        if (node.symbol == "field") {
          const auto output = std::find(
              program.inputFields.begin(), program.inputFields.end(), program.field);
          return sample(static_cast<uint32_t>(
              std::distance(program.inputFields.begin(), output)), {0, 0, 0});
        }
        const auto inputField = std::find(
            program.inputFields.begin(), program.inputFields.end(), node.symbol);
        if (inputField != program.inputFields.end()) {
          return sample(static_cast<uint32_t>(
              std::distance(program.inputFields.begin(), inputField)), {0, 0, 0});
        }
        const std::array<std::pair<std::string_view, StencilOpcode>, 4> builtins{{
            {"x", StencilOpcode::CoordinateX}, {"y", StencilOpcode::CoordinateY},
            {"z", StencilOpcode::CoordinateZ}, {"t", StencilOpcode::Time}}};
        for (const auto& [name, opcode] : builtins) {
          if (node.symbol == name) return append({opcode}, "builtin:" + node.symbol);
        }
        const auto parameter = std::find(program.parameterNames.begin(), program.parameterNames.end(), node.symbol);
        if (parameter == program.parameterNames.end()) {
          issues.push_back({"unbound evolution symbol '" + node.symbol + "'"});
          return constant(0.0);
        }
        StencilInstruction instruction{StencilOpcode::Parameter};
        instruction.parameterIndex = static_cast<uint32_t>(std::distance(program.parameterNames.begin(), parameter));
        return append(instruction, "parameter:" + node.symbol);
      }
      case equation::NodeKind::Add: return lowerBinary(StencilOpcode::Add, "addition");
      case equation::NodeKind::Subtract: return lowerBinary(StencilOpcode::Subtract, "subtraction");
      case equation::NodeKind::Multiply: return lowerBinary(StencilOpcode::Multiply, "multiplication");
      case equation::NodeKind::Divide: return lowerBinary(StencilOpcode::Divide, "division");
      case equation::NodeKind::Power: return lowerBinary(StencilOpcode::Power, "power");
      case equation::NodeKind::Negate: return lowerUnary(StencilOpcode::Negate, "negation");
      case equation::NodeKind::Function: {
        if (node.symbol == "laplacian" || node.symbol == "gradient_x" ||
            node.symbol == "gradient_y" || node.symbol == "gradient_z") {
          return differential(node);
        }
        const std::map<std::string, StencilOpcode> unaryFunctions{
            {"sin", StencilOpcode::Sin}, {"cos", StencilOpcode::Cos}, {"tan", StencilOpcode::Tan},
            {"exp", StencilOpcode::Exp}, {"sqrt", StencilOpcode::Sqrt}, {"abs", StencilOpcode::Abs},
            {"log", StencilOpcode::Log}};
        if (const auto found = unaryFunctions.find(node.symbol); found != unaryFunctions.end()) {
          return lowerUnary(found->second, node.symbol.c_str());
        }
        if (node.symbol == "min") return lowerBinary(StencilOpcode::Min, "min");
        if (node.symbol == "max") return lowerBinary(StencilOpcode::Max, "max");
        if (node.symbol == "clamp") {
          if (node.children.size() != 3) {
            issues.push_back({"clamp requires three operands"});
            return constant(0.0);
          }
          const uint32_t value = lower(node.children[0]);
          const uint32_t minimum = lower(node.children[1]);
          const uint32_t maximum = lower(node.children[2]);
          StencilInstruction instruction{StencilOpcode::Clamp, {value, minimum, maximum}, {}, 3};
          return append(instruction, "clamp:" + std::to_string(value) + ":" +
                                         std::to_string(minimum) + ":" + std::to_string(maximum));
        }
        issues.push_back({"unsupported evolution function '" + node.symbol + "'"});
        return constant(0.0);
      }
    }
    issues.push_back({"unsupported evolution node"});
    return constant(0.0);
  }
};

size_t flattened(uint32_t x, uint32_t y, uint32_t z, const std::array<uint32_t, 3>& extent) {
  return (static_cast<size_t>(z) * extent[1] + y) * extent[0] + x;
}

uint32_t wrap(int value, uint32_t extent) {
  const int signedExtent = static_cast<int>(extent);
  const int remainder = value % signedExtent;
  return static_cast<uint32_t>(remainder < 0 ? remainder + signedExtent : remainder);
}

double sampleField(const ScalarEvolutionProgram& program, const std::vector<float>& input,
                   std::array<int, 3> cell, const std::array<int8_t, 3>& offset) {
  bool outside = false;
  std::array<uint32_t, 3> resolved{};
  for (size_t axis = 0; axis < 3; ++axis) {
    const int candidate = cell[axis] + offset[axis];
    const uint32_t extent = program.domain.resolution[axis];
    outside = outside || candidate < 0 || candidate >= static_cast<int>(extent);
    if (program.boundary == BoundaryKind::Periodic) resolved[axis] = wrap(candidate, extent);
    else resolved[axis] = static_cast<uint32_t>(std::clamp(candidate, 0, static_cast<int>(extent) - 1));
  }
  if (outside && program.boundary == BoundaryKind::FixedValue) return program.fixedBoundaryValue;
  return input[flattened(resolved[0], resolved[1], resolved[2], program.domain.resolution)];
}

const char* functionName(StencilOpcode opcode) {
  switch (opcode) {
    case StencilOpcode::Power: return "pow";
    case StencilOpcode::Sin: return "sin";
    case StencilOpcode::Cos: return "cos";
    case StencilOpcode::Tan: return "tan";
    case StencilOpcode::Exp: return "exp";
    case StencilOpcode::Sqrt: return "sqrt";
    case StencilOpcode::Abs: return "abs";
    case StencilOpcode::Log: return "log";
    case StencilOpcode::Min: return "min";
    case StencilOpcode::Max: return "max";
    case StencilOpcode::Clamp: return "clamp";
    default: return nullptr;
  }
}

std::string instructionExpression(const StencilInstruction& instruction,
                                  const std::vector<std::string>& parameters,
                                  bool metal,
                                  std::string_view registerPrefix = {},
                                  bool coupled = false) {
  const auto reg = [&](uint8_t index) {
    return std::string{registerPrefix} + "r" +
        std::to_string(instruction.operands[index]);
  };
  switch (instruction.opcode) {
    case StencilOpcode::Constant: return number(instruction.immediate) + (metal ? "f" : "");
    case StencilOpcode::CoordinateX: return "x";
    case StencilOpcode::CoordinateY: return "y";
    case StencilOpcode::CoordinateZ: return "z";
    case StencilOpcode::Time: return "parameters.time";
    case StencilOpcode::DeltaTime: return "parameters.dt";
    case StencilOpcode::Parameter: return "parameters." + parameters.at(instruction.parameterIndex);
    case StencilOpcode::FieldSample:
      return "sampleField(" +
          (coupled ? std::to_string(instruction.fieldIndex) + "u, " : std::string{}) +
          "cell + " + std::string{metal ? "int3(" : "ivec3("} +
          std::to_string(instruction.sampleOffset[0]) + ", " +
          std::to_string(instruction.sampleOffset[1]) + ", " +
          std::to_string(instruction.sampleOffset[2]) + "))";
    case StencilOpcode::Add: return "(" + reg(0) + " + " + reg(1) + ")";
    case StencilOpcode::Subtract: return "(" + reg(0) + " - " + reg(1) + ")";
    case StencilOpcode::Multiply: return "(" + reg(0) + " * " + reg(1) + ")";
    case StencilOpcode::Divide: return "(" + reg(0) + " / " + reg(1) + ")";
    case StencilOpcode::Negate: return "(-" + reg(0) + ")";
    default: {
      const char* name = functionName(instruction.opcode);
      if (name == nullptr) throw std::runtime_error("stencil opcode cannot be emitted");
      std::string expression = std::string{name} + "(";
      for (uint8_t index = 0; index < instruction.operandCount; ++index) {
        if (index != 0) expression += ", ";
        expression += reg(index);
      }
      return expression + ")";
    }
  }
}

}  // namespace

StencilLoweringResult lowerScalarEvolutionProgram(
    const PhysicsModel& model, const std::string& field,
    const equation::ScalarExpression& rhs, std::vector<std::string> parameterNames) {
  StencilLoweringResult result{};
  const ValidationResult validation = validate(model);
  if (!validation.valid()) {
    result.issues = validation.issues;
    return result;
  }
  const auto declaration = std::find_if(model.fields.begin(), model.fields.end(), [&](const auto& candidate) {
    return candidate.name == field;
  });
  if (declaration == model.fields.end()) {
    result.issues.push_back({"evolution references unknown field '" + field + "'"});
    return result;
  }
  if (declaration->valueType != ValueType::Scalar || declaration->placement != FieldPlacement::CellCenter) {
    result.issues.push_back({"scalar evolution requires a cell-centred scalar field"});
    return result;
  }
  std::set<std::string> uniqueParameters;
  for (const std::string& parameter : parameterNames) {
    if (parameter.empty() || parameter == field || parameter == "field" ||
        !uniqueParameters.insert(parameter).second) {
      result.issues.push_back({"evolution parameter names must be non-empty, unique, and distinct from the field"});
      return result;
    }
  }
  LoweringContext context{};
  context.program.field = field;
  context.program.domain = model.domain;
  context.program.inputFields = {field};
  context.program.defaultTimestepSeconds = model.solver.timestepSeconds;
  context.program.parameterNames = std::move(parameterNames);
  const auto boundary = std::find_if(model.boundaries.begin(), model.boundaries.end(), [&](const auto& candidate) {
    return candidate.field == field;
  });
  if (boundary != model.boundaries.end()) {
    if (boundary->kind == BoundaryKind::NoSlip) {
      result.issues.push_back({"no-slip is a vector velocity boundary, not a scalar evolution boundary"});
      return result;
    }
    context.program.boundary = boundary->kind;
    context.program.fixedBoundaryValue = boundary->fixedValue.value_or(0.0);
  }
  context.program.inputBoundaries = {context.program.boundary};
  context.program.inputFixedBoundaryValues = {context.program.fixedBoundaryValue};
  const uint32_t center = context.sample(0, {0, 0, 0});
  const uint32_t rhsRegister = context.lower(rhs.root());
  const uint32_t dt = context.append({StencilOpcode::DeltaTime}, "builtin:dt");
  context.program.outputRegister = context.binary(
      StencilOpcode::Add, center, context.binary(StencilOpcode::Multiply, dt, rhsRegister));
  if (!context.issues.empty()) {
    result.issues = std::move(context.issues);
    return result;
  }
  context.program.canonicalHash = programHash(context.program);
  result.program = std::move(context.program);
  return result;
}

CoupledStencilLoweringResult lowerCoupledScalarEvolutionProgram(
    const PhysicsModel& model,
    const std::vector<ScalarEvolutionEquationSource>& equations,
    std::vector<std::string> parameterNames) {
  CoupledStencilLoweringResult result{};
  const ValidationResult validation = validate(model);
  if (!validation.valid()) {
    result.issues = validation.issues;
    return result;
  }
  if (equations.size() < 2) {
    result.issues.push_back({"coupled evolution requires at least two field equations"});
    return result;
  }

  CoupledScalarEvolutionProgram coupled{};
  coupled.domain = model.domain;
  coupled.defaultTimestepSeconds = model.solver.timestepSeconds;
  coupled.parameterNames = std::move(parameterNames);
  std::set<std::string> uniqueFields;
  for (const ScalarEvolutionEquationSource& equationSource : equations) {
    if (!uniqueFields.insert(equationSource.field).second) {
      result.issues.push_back({"duplicate coupled evolution for field '" + equationSource.field + "'"});
      return result;
    }
    const auto declaration = std::find_if(
        model.fields.begin(), model.fields.end(), [&](const FieldDeclaration& candidate) {
          return candidate.name == equationSource.field;
        });
    if (declaration == model.fields.end()) {
      result.issues.push_back({"coupled evolution references unknown field '" + equationSource.field + "'"});
      return result;
    }
    if (declaration->valueType != ValueType::Scalar ||
        declaration->placement != FieldPlacement::CellCenter) {
      result.issues.push_back({"coupled evolution supports only cell-centred scalar fields"});
      return result;
    }
    coupled.fields.push_back(equationSource.field);
  }
  std::set<std::string> uniqueParameters;
  for (const std::string& parameter : coupled.parameterNames) {
    if (parameter.empty() || uniqueFields.contains(parameter) || parameter == "field" ||
        parameter == "x" || parameter == "y" || parameter == "z" || parameter == "t" ||
        !uniqueParameters.insert(parameter).second) {
      result.issues.push_back({
          "coupled parameter names must be non-empty, unique, and distinct from fields and builtins"});
      return result;
    }
  }

  for (const std::string& field : coupled.fields) {
    BoundaryKind kind = BoundaryKind::Open;
    double fixedValue = 0.0;
    const auto boundary = std::find_if(
        model.boundaries.begin(), model.boundaries.end(), [&](const BoundaryCondition& candidate) {
          return candidate.field == field;
        });
    if (boundary != model.boundaries.end()) {
      if (boundary->kind == BoundaryKind::NoSlip) {
        result.issues.push_back({"no-slip is not valid for coupled scalar field '" + field + "'"});
        return result;
      }
      kind = boundary->kind;
      fixedValue = boundary->fixedValue.value_or(0.0);
    }
    coupled.boundaries.push_back(kind);
    coupled.fixedBoundaryValues.push_back(fixedValue);
  }

  for (size_t equationIndex = 0; equationIndex < equations.size(); ++equationIndex) {
    LoweringContext context{};
    context.program.field = equations[equationIndex].field;
    context.program.domain = coupled.domain;
    context.program.inputFields = coupled.fields;
    context.program.inputBoundaries = coupled.boundaries;
    context.program.inputFixedBoundaryValues = coupled.fixedBoundaryValues;
    context.program.boundary = coupled.boundaries[equationIndex];
    context.program.fixedBoundaryValue = coupled.fixedBoundaryValues[equationIndex];
    context.program.defaultTimestepSeconds = coupled.defaultTimestepSeconds;
    context.program.parameterNames = coupled.parameterNames;
    const uint32_t center = context.sample(static_cast<uint32_t>(equationIndex), {0, 0, 0});
    const uint32_t rhs = context.lower(equations[equationIndex].rhs.root());
    const uint32_t dt = context.append({StencilOpcode::DeltaTime}, "builtin:dt");
    context.program.outputRegister = context.binary(
        StencilOpcode::Add, center,
        context.binary(StencilOpcode::Multiply, dt, rhs));
    if (!context.issues.empty()) {
      result.issues.insert(
          result.issues.end(), context.issues.begin(), context.issues.end());
      return result;
    }
    context.program.canonicalHash = programHash(context.program);
    coupled.equations.push_back(std::move(context.program));
  }

  uint64_t hash = 1469598103934665603ull;
  hash = fnv1a(hash, "coupled-scalar-evolution-v1");
  for (const ScalarEvolutionProgram& equationProgram : coupled.equations) {
    hash = fnv1a(hash, std::to_string(equationProgram.canonicalHash));
  }
  coupled.canonicalHash = hash;
  result.program = std::move(coupled);
  return result;
}

std::vector<float> executeScalarEvolution3D(
    const ScalarEvolutionProgram& program, const std::vector<float>& input,
    double timestepSeconds, double timeSeconds,
    const std::map<std::string, double>& parameterValues) {
  if (!(timestepSeconds > 0.0) || !std::isfinite(timestepSeconds)) {
    throw std::invalid_argument("evolution timestep must be finite and positive");
  }
  const size_t count = static_cast<size_t>(program.domain.resolution[0]) *
      program.domain.resolution[1] * program.domain.resolution[2];
  if (input.size() != count) throw std::invalid_argument("evolution input does not match the declared domain resolution");
  std::vector<double> parameters;
  parameters.reserve(program.parameterNames.size());
  for (const std::string& name : program.parameterNames) {
    const auto found = parameterValues.find(name);
    if (found == parameterValues.end()) throw std::invalid_argument("missing evolution parameter '" + name + "'");
    parameters.push_back(found->second);
  }
  std::vector<float> output(count);
  for (uint32_t z = 0; z < program.domain.resolution[2]; ++z) {
    for (uint32_t y = 0; y < program.domain.resolution[1]; ++y) {
      for (uint32_t x = 0; x < program.domain.resolution[0]; ++x) {
        const std::array<int, 3> cell{static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)};
        const std::array<double, 3> position{
            program.domain.minimum[0] + (static_cast<double>(x) + 0.5) *
                (program.domain.maximum[0] - program.domain.minimum[0]) / program.domain.resolution[0],
            program.domain.minimum[1] + (static_cast<double>(y) + 0.5) *
                (program.domain.maximum[1] - program.domain.minimum[1]) / program.domain.resolution[1],
            program.domain.minimum[2] + (static_cast<double>(z) + 0.5) *
                (program.domain.maximum[2] - program.domain.minimum[2]) / program.domain.resolution[2]};
        std::vector<double> registers;
        registers.reserve(program.instructions.size());
        const auto value = [&](const StencilInstruction& instruction, uint8_t index) {
          return registers.at(instruction.operands[index]);
        };
        for (const StencilInstruction& instruction : program.instructions) {
          double result = 0.0;
          switch (instruction.opcode) {
            case StencilOpcode::Constant: result = instruction.immediate; break;
            case StencilOpcode::CoordinateX: result = position[0]; break;
            case StencilOpcode::CoordinateY: result = position[1]; break;
            case StencilOpcode::CoordinateZ: result = position[2]; break;
            case StencilOpcode::Time: result = timeSeconds; break;
            case StencilOpcode::DeltaTime: result = timestepSeconds; break;
            case StencilOpcode::Parameter: result = parameters.at(instruction.parameterIndex); break;
            case StencilOpcode::FieldSample: result = sampleField(program, input, cell, instruction.sampleOffset); break;
            case StencilOpcode::Add: result = value(instruction, 0) + value(instruction, 1); break;
            case StencilOpcode::Subtract: result = value(instruction, 0) - value(instruction, 1); break;
            case StencilOpcode::Multiply: result = value(instruction, 0) * value(instruction, 1); break;
            case StencilOpcode::Divide: result = value(instruction, 0) / value(instruction, 1); break;
            case StencilOpcode::Power: result = std::pow(value(instruction, 0), value(instruction, 1)); break;
            case StencilOpcode::Negate: result = -value(instruction, 0); break;
            case StencilOpcode::Sin: result = std::sin(value(instruction, 0)); break;
            case StencilOpcode::Cos: result = std::cos(value(instruction, 0)); break;
            case StencilOpcode::Tan: result = std::tan(value(instruction, 0)); break;
            case StencilOpcode::Exp: result = std::exp(value(instruction, 0)); break;
            case StencilOpcode::Sqrt: result = std::sqrt(value(instruction, 0)); break;
            case StencilOpcode::Abs: result = std::abs(value(instruction, 0)); break;
            case StencilOpcode::Log: result = std::log(value(instruction, 0)); break;
            case StencilOpcode::Min: result = std::min(value(instruction, 0), value(instruction, 1)); break;
            case StencilOpcode::Max: result = std::max(value(instruction, 0), value(instruction, 1)); break;
            case StencilOpcode::Clamp:
              result = std::clamp(value(instruction, 0), value(instruction, 1), value(instruction, 2));
              break;
          }
          if (!std::isfinite(result)) throw std::runtime_error("scalar evolution produced a non-finite value");
          registers.push_back(result);
        }
        output[flattened(x, y, z, program.domain.resolution)] =
            static_cast<float>(registers.at(program.outputRegister));
      }
    }
  }
  return output;
}

std::map<std::string, std::vector<float>> executeCoupledScalarEvolution3D(
    const CoupledScalarEvolutionProgram& program,
    const std::map<std::string, std::vector<float>>& input,
    double timestepSeconds,
    double timeSeconds,
    const std::map<std::string, double>& parameterValues) {
  if (!(timestepSeconds > 0.0) || !std::isfinite(timestepSeconds)) {
    throw std::invalid_argument("coupled evolution timestep must be finite and positive");
  }
  if (program.fields.size() != program.equations.size() ||
      program.fields.size() != program.boundaries.size() ||
      program.fields.size() != program.fixedBoundaryValues.size()) {
    throw std::invalid_argument("coupled evolution program has an inconsistent field ABI");
  }
  const size_t cellCount = static_cast<size_t>(program.domain.resolution[0]) *
      program.domain.resolution[1] * program.domain.resolution[2];
  std::vector<const std::vector<float>*> inputs;
  inputs.reserve(program.fields.size());
  for (const std::string& field : program.fields) {
    const auto found = input.find(field);
    if (found == input.end() || found->second.size() != cellCount) {
      throw std::invalid_argument(
          "coupled input field '" + field + "' does not match the declared domain");
    }
    inputs.push_back(&found->second);
  }
  std::vector<double> parameters;
  parameters.reserve(program.parameterNames.size());
  for (const std::string& name : program.parameterNames) {
    const auto found = parameterValues.find(name);
    if (found == parameterValues.end()) {
      throw std::invalid_argument("missing coupled evolution parameter '" + name + "'");
    }
    parameters.push_back(found->second);
  }

  std::map<std::string, std::vector<float>> output;
  for (const std::string& field : program.fields) output.emplace(field, std::vector<float>(cellCount));
  for (uint32_t z = 0; z < program.domain.resolution[2]; ++z) {
    for (uint32_t y = 0; y < program.domain.resolution[1]; ++y) {
      for (uint32_t x = 0; x < program.domain.resolution[0]; ++x) {
        const std::array<int, 3> cell{
            static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)};
        const std::array<double, 3> position{
            program.domain.minimum[0] + (static_cast<double>(x) + 0.5) *
                (program.domain.maximum[0] - program.domain.minimum[0]) /
                program.domain.resolution[0],
            program.domain.minimum[1] + (static_cast<double>(y) + 0.5) *
                (program.domain.maximum[1] - program.domain.minimum[1]) /
                program.domain.resolution[1],
            program.domain.minimum[2] + (static_cast<double>(z) + 0.5) *
                (program.domain.maximum[2] - program.domain.minimum[2]) /
                program.domain.resolution[2]};
        for (size_t equationIndex = 0; equationIndex < program.equations.size(); ++equationIndex) {
          const ScalarEvolutionProgram& equationProgram = program.equations[equationIndex];
          std::vector<double> registers;
          registers.reserve(equationProgram.instructions.size());
          const auto operand = [&](const StencilInstruction& instruction, uint8_t index) {
            return registers.at(instruction.operands[index]);
          };
          for (const StencilInstruction& instruction : equationProgram.instructions) {
            double value = 0.0;
            switch (instruction.opcode) {
              case StencilOpcode::Constant: value = instruction.immediate; break;
              case StencilOpcode::CoordinateX: value = position[0]; break;
              case StencilOpcode::CoordinateY: value = position[1]; break;
              case StencilOpcode::CoordinateZ: value = position[2]; break;
              case StencilOpcode::Time: value = timeSeconds; break;
              case StencilOpcode::DeltaTime: value = timestepSeconds; break;
              case StencilOpcode::Parameter:
                value = parameters.at(instruction.parameterIndex);
                break;
              case StencilOpcode::FieldSample: {
                const uint32_t fieldIndex = instruction.fieldIndex;
                if (fieldIndex >= inputs.size()) {
                  throw std::runtime_error("coupled stencil references an invalid field index");
                }
                ScalarEvolutionProgram samplingProgram{};
                samplingProgram.domain = program.domain;
                samplingProgram.boundary = program.boundaries[fieldIndex];
                samplingProgram.fixedBoundaryValue = program.fixedBoundaryValues[fieldIndex];
                value = sampleField(
                    samplingProgram, *inputs[fieldIndex], cell, instruction.sampleOffset);
                break;
              }
              case StencilOpcode::Add: value = operand(instruction, 0) + operand(instruction, 1); break;
              case StencilOpcode::Subtract: value = operand(instruction, 0) - operand(instruction, 1); break;
              case StencilOpcode::Multiply: value = operand(instruction, 0) * operand(instruction, 1); break;
              case StencilOpcode::Divide: value = operand(instruction, 0) / operand(instruction, 1); break;
              case StencilOpcode::Power: value = std::pow(operand(instruction, 0), operand(instruction, 1)); break;
              case StencilOpcode::Negate: value = -operand(instruction, 0); break;
              case StencilOpcode::Sin: value = std::sin(operand(instruction, 0)); break;
              case StencilOpcode::Cos: value = std::cos(operand(instruction, 0)); break;
              case StencilOpcode::Tan: value = std::tan(operand(instruction, 0)); break;
              case StencilOpcode::Exp: value = std::exp(operand(instruction, 0)); break;
              case StencilOpcode::Sqrt: value = std::sqrt(operand(instruction, 0)); break;
              case StencilOpcode::Abs: value = std::abs(operand(instruction, 0)); break;
              case StencilOpcode::Log: value = std::log(operand(instruction, 0)); break;
              case StencilOpcode::Min: value = std::min(operand(instruction, 0), operand(instruction, 1)); break;
              case StencilOpcode::Max: value = std::max(operand(instruction, 0), operand(instruction, 1)); break;
              case StencilOpcode::Clamp:
                value = std::clamp(
                    operand(instruction, 0), operand(instruction, 1), operand(instruction, 2));
                break;
            }
            if (!std::isfinite(value)) {
              throw std::runtime_error("coupled scalar evolution produced a non-finite value");
            }
            registers.push_back(value);
          }
          output.at(program.fields[equationIndex])[flattened(
              x, y, z, program.domain.resolution)] =
              static_cast<float>(registers.at(equationProgram.outputRegister));
        }
      }
    }
  }
  return output;
}

std::string emitScalarEvolutionGlsl(const ScalarEvolutionProgram& program) {
  std::ostringstream shader;
  shader << "#version 450\n"
            "layout(local_size_x = 8, local_size_y = 8, local_size_z = 4) in;\n"
            "layout(std430, binding = 0) readonly buffer FieldInput { float values[]; } inputField;\n"
            "layout(std430, binding = 1) writeonly buffer FieldOutput { float values[]; } outputField;\n"
            "layout(std140, binding = 2) uniform EvolutionParameters {\n"
            "  uint width; uint height; uint depth; float dt; float time;\n";
  for (const std::string& parameter : program.parameterNames) shader << "  float " << parameter << ";\n";
  shader << "} parameters;\n"
            "uint fieldIndex(ivec3 cell) { return uint((cell.z * int(parameters.height) + cell.y) * int(parameters.width) + cell.x); }\n"
            "float sampleField(ivec3 cell) {\n";
  if (program.boundary == BoundaryKind::Periodic) {
    shader << "  ivec3 extent = ivec3(parameters.width, parameters.height, parameters.depth);\n"
              "  cell = ivec3((cell.x % extent.x + extent.x) % extent.x, (cell.y % extent.y + extent.y) % extent.y, (cell.z % extent.z + extent.z) % extent.z);\n";
  } else if (program.boundary == BoundaryKind::FixedValue) {
    shader << "  if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(parameters.width, parameters.height, parameters.depth)))) return "
           << number(program.fixedBoundaryValue) << ";\n";
  } else {
    shader << "  cell = clamp(cell, ivec3(0), ivec3(parameters.width, parameters.height, parameters.depth) - 1);\n";
  }
  shader << "  return inputField.values[fieldIndex(cell)];\n}\nvoid main() {\n"
            "  uvec3 gid = gl_GlobalInvocationID.xyz;\n"
            "  if (gid.x >= parameters.width || gid.y >= parameters.height || gid.z >= parameters.depth) return;\n"
            "  ivec3 cell = ivec3(gid);\n"
         << "  float x = " << number(program.domain.minimum[0]) << " + (float(cell.x) + 0.5) * "
         << number((program.domain.maximum[0] - program.domain.minimum[0]) / program.domain.resolution[0]) << ";\n"
         << "  float y = " << number(program.domain.minimum[1]) << " + (float(cell.y) + 0.5) * "
         << number((program.domain.maximum[1] - program.domain.minimum[1]) / program.domain.resolution[1]) << ";\n"
         << "  float z = " << number(program.domain.minimum[2]) << " + (float(cell.z) + 0.5) * "
         << number((program.domain.maximum[2] - program.domain.minimum[2]) / program.domain.resolution[2]) << ";\n";
  for (size_t index = 0; index < program.instructions.size(); ++index) {
    shader << "  float r" << index << " = " << instructionExpression(program.instructions[index], program.parameterNames, false) << ";\n";
  }
  shader << "  outputField.values[fieldIndex(cell)] = r" << program.outputRegister << ";\n}\n";
  return shader.str();
}

std::string emitScalarEvolutionMsl(const ScalarEvolutionProgram& program) {
  std::ostringstream shader;
  shader << "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct EvolutionParameters { uint width; uint height; uint depth; float dt; float time;";
  for (const std::string& parameter : program.parameterNames) shader << " float " << parameter << ";";
  shader << " };\nkernel void executeScalarEvolution(\n"
            "    device const float* inputField [[buffer(0)]], device float* outputField [[buffer(1)]],\n"
            "    constant EvolutionParameters& parameters [[buffer(2)]], uint3 gid [[thread_position_in_grid]]) {\n"
            "  if (gid.x >= parameters.width || gid.y >= parameters.height || gid.z >= parameters.depth) return;\n"
            "  int3 cell = int3(gid);\n"
            "  auto fieldIndex = [&](int3 p) -> uint { return uint((p.z * int(parameters.height) + p.y) * int(parameters.width) + p.x); };\n"
            "  auto sampleField = [&](int3 p) -> float {\n";
  if (program.boundary == BoundaryKind::Periodic) {
    shader << "    int3 extent = int3(parameters.width, parameters.height, parameters.depth);\n"
              "    p = int3((p.x % extent.x + extent.x) % extent.x, (p.y % extent.y + extent.y) % extent.y, (p.z % extent.z + extent.z) % extent.z);\n";
  } else if (program.boundary == BoundaryKind::FixedValue) {
    shader << "    if (any(p < int3(0)) || any(p >= int3(parameters.width, parameters.height, parameters.depth))) return "
           << number(program.fixedBoundaryValue) << "f;\n";
  } else {
    shader << "    p = clamp(p, int3(0), int3(parameters.width, parameters.height, parameters.depth) - 1);\n";
  }
  shader << "    return inputField[fieldIndex(p)];\n  };\n"
         << "  float x = " << number(program.domain.minimum[0]) << "f + (float(cell.x) + 0.5f) * "
         << number((program.domain.maximum[0] - program.domain.minimum[0]) / program.domain.resolution[0]) << "f;\n"
         << "  float y = " << number(program.domain.minimum[1]) << "f + (float(cell.y) + 0.5f) * "
         << number((program.domain.maximum[1] - program.domain.minimum[1]) / program.domain.resolution[1]) << "f;\n"
         << "  float z = " << number(program.domain.minimum[2]) << "f + (float(cell.z) + 0.5f) * "
         << number((program.domain.maximum[2] - program.domain.minimum[2]) / program.domain.resolution[2]) << "f;\n";
  for (size_t index = 0; index < program.instructions.size(); ++index) {
    shader << "  float r" << index << " = " << instructionExpression(program.instructions[index], program.parameterNames, true) << ";\n";
  }
  shader << "  outputField[fieldIndex(cell)] = r" << program.outputRegister << ";\n}\n";
  return shader.str();
}

std::string emitCoupledScalarEvolutionGlsl(const CoupledScalarEvolutionProgram& program) {
  if (program.fields.size() != program.equations.size()) {
    throw std::invalid_argument("cannot emit an inconsistent coupled evolution program");
  }
  std::ostringstream shader;
  shader << "#version 450\n"
            "layout(local_size_x = 8, local_size_y = 8, local_size_z = 4) in;\n"
            "layout(std430, binding = 0) readonly buffer CoupledInput { float values[]; } inputState;\n"
            "layout(std430, binding = 1) writeonly buffer CoupledOutput { float values[]; } outputState;\n"
            "layout(std140, binding = 2) uniform EvolutionParameters {\n"
            "  uint width; uint height; uint depth; float dt; float time;\n";
  for (const std::string& parameter : program.parameterNames) {
    shader << "  float " << parameter << ";\n";
  }
  shader << "} parameters;\n"
            "uint cellIndex(ivec3 cell) { return uint((cell.z * int(parameters.height) + cell.y) * int(parameters.width) + cell.x); }\n"
            "uint cellCount() { return parameters.width * parameters.height * parameters.depth; }\n"
            "float sampleField(uint field, ivec3 cell) {\n"
            "  bool outside = any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(parameters.width, parameters.height, parameters.depth)));\n";
  for (size_t field = 0; field < program.fields.size(); ++field) {
    shader << (field == 0 ? "  if" : "  else if") << " (field == " << field << "u) {\n";
    if (program.boundaries[field] == BoundaryKind::Periodic) {
      shader << "    ivec3 extent = ivec3(parameters.width, parameters.height, parameters.depth);\n"
                "    cell = ivec3((cell.x % extent.x + extent.x) % extent.x, (cell.y % extent.y + extent.y) % extent.y, (cell.z % extent.z + extent.z) % extent.z);\n";
    } else if (program.boundaries[field] == BoundaryKind::FixedValue) {
      shader << "    if (outside) return " << number(program.fixedBoundaryValues[field]) << ";\n"
                "    cell = clamp(cell, ivec3(0), ivec3(parameters.width, parameters.height, parameters.depth) - 1);\n";
    } else {
      shader << "    cell = clamp(cell, ivec3(0), ivec3(parameters.width, parameters.height, parameters.depth) - 1);\n";
    }
    shader << "  }\n";
  }
  shader << "  else { return 0.0; }\n"
            "  return inputState.values[field * cellCount() + cellIndex(cell)];\n"
            "}\nvoid main() {\n"
            "  uvec3 gid = gl_GlobalInvocationID.xyz;\n"
            "  if (gid.x >= parameters.width || gid.y >= parameters.height || gid.z >= parameters.depth) return;\n"
            "  ivec3 cell = ivec3(gid);\n"
         << "  float x = " << number(program.domain.minimum[0]) << " + (float(cell.x) + 0.5) * "
         << number((program.domain.maximum[0] - program.domain.minimum[0]) /
                   program.domain.resolution[0]) << ";\n"
         << "  float y = " << number(program.domain.minimum[1]) << " + (float(cell.y) + 0.5) * "
         << number((program.domain.maximum[1] - program.domain.minimum[1]) /
                   program.domain.resolution[1]) << ";\n"
         << "  float z = " << number(program.domain.minimum[2]) << " + (float(cell.z) + 0.5) * "
         << number((program.domain.maximum[2] - program.domain.minimum[2]) /
                   program.domain.resolution[2]) << ";\n";
  for (size_t equation = 0; equation < program.equations.size(); ++equation) {
    const std::string prefix = "e" + std::to_string(equation) + "_";
    const ScalarEvolutionProgram& equationProgram = program.equations[equation];
    for (size_t instruction = 0; instruction < equationProgram.instructions.size(); ++instruction) {
      shader << "  float " << prefix << "r" << instruction << " = "
             << instructionExpression(
                    equationProgram.instructions[instruction], program.parameterNames,
                    false, prefix, true)
             << ";\n";
    }
    shader << "  outputState.values[" << equation << "u * cellCount() + cellIndex(cell)] = "
           << prefix << "r" << equationProgram.outputRegister << ";\n";
  }
  shader << "}\n";
  return shader.str();
}

std::string emitCoupledScalarEvolutionMsl(const CoupledScalarEvolutionProgram& program) {
  if (program.fields.size() != program.equations.size()) {
    throw std::invalid_argument("cannot emit an inconsistent coupled evolution program");
  }
  std::ostringstream shader;
  shader << "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct EvolutionParameters { uint width; uint height; uint depth; float dt; float time;";
  for (const std::string& parameter : program.parameterNames) {
    shader << " float " << parameter << ";";
  }
  shader << " };\nkernel void executeCoupledScalarEvolution(\n"
            "    device const float* inputState [[buffer(0)]], device float* outputState [[buffer(1)]],\n"
            "    constant EvolutionParameters& parameters [[buffer(2)]], uint3 gid [[thread_position_in_grid]]) {\n"
            "  if (gid.x >= parameters.width || gid.y >= parameters.height || gid.z >= parameters.depth) return;\n"
            "  int3 cell = int3(gid);\n"
            "  uint cells = parameters.width * parameters.height * parameters.depth;\n"
            "  auto cellIndex = [&](int3 p) -> uint { return uint((p.z * int(parameters.height) + p.y) * int(parameters.width) + p.x); };\n"
            "  auto sampleField = [&](uint field, int3 p) -> float {\n"
            "    bool outside = any(p < int3(0)) || any(p >= int3(parameters.width, parameters.height, parameters.depth));\n";
  for (size_t field = 0; field < program.fields.size(); ++field) {
    shader << (field == 0 ? "    if" : "    else if") << " (field == " << field << "u) {\n";
    if (program.boundaries[field] == BoundaryKind::Periodic) {
      shader << "      int3 extent = int3(parameters.width, parameters.height, parameters.depth);\n"
                "      p = int3((p.x % extent.x + extent.x) % extent.x, (p.y % extent.y + extent.y) % extent.y, (p.z % extent.z + extent.z) % extent.z);\n";
    } else if (program.boundaries[field] == BoundaryKind::FixedValue) {
      shader << "      if (outside) return " << number(program.fixedBoundaryValues[field]) << "f;\n"
                "      p = clamp(p, int3(0), int3(parameters.width, parameters.height, parameters.depth) - 1);\n";
    } else {
      shader << "      p = clamp(p, int3(0), int3(parameters.width, parameters.height, parameters.depth) - 1);\n";
    }
    shader << "    }\n";
  }
  shader << "    else { return 0.0f; }\n"
            "    return inputState[field * cells + cellIndex(p)];\n"
            "  };\n"
         << "  float x = " << number(program.domain.minimum[0]) << "f + (float(cell.x) + 0.5f) * "
         << number((program.domain.maximum[0] - program.domain.minimum[0]) /
                   program.domain.resolution[0]) << "f;\n"
         << "  float y = " << number(program.domain.minimum[1]) << "f + (float(cell.y) + 0.5f) * "
         << number((program.domain.maximum[1] - program.domain.minimum[1]) /
                   program.domain.resolution[1]) << "f;\n"
         << "  float z = " << number(program.domain.minimum[2]) << "f + (float(cell.z) + 0.5f) * "
         << number((program.domain.maximum[2] - program.domain.minimum[2]) /
                   program.domain.resolution[2]) << "f;\n";
  for (size_t equation = 0; equation < program.equations.size(); ++equation) {
    const std::string prefix = "e" + std::to_string(equation) + "_";
    const ScalarEvolutionProgram& equationProgram = program.equations[equation];
    for (size_t instruction = 0; instruction < equationProgram.instructions.size(); ++instruction) {
      shader << "  float " << prefix << "r" << instruction << " = "
             << instructionExpression(
                    equationProgram.instructions[instruction], program.parameterNames,
                    true, prefix, true)
             << ";\n";
    }
    shader << "  outputState[" << equation << "u * cells + cellIndex(cell)] = "
           << prefix << "r" << equationProgram.outputRegister << ";\n";
  }
  shader << "}\n";
  return shader.str();
}

}  // namespace vulkax::physics
