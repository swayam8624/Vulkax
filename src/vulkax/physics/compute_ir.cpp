#include "vulkax/physics/compute_ir.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
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

uint64_t fnv1a(uint64_t hash, const std::string& value) {
  constexpr uint64_t prime = 1099511628211ull;
  for (const unsigned char character : value) {
    hash ^= character;
    hash *= prime;
  }
  return hash;
}

uint64_t programHash(const ScalarComputeProgram& program) {
  uint64_t hash = 1469598103934665603ull;
  hash = fnv1a(hash, program.outputField);
  for (size_t axis = 0; axis < 3; ++axis) {
    hash = fnv1a(hash, number(program.domain.minimum[axis]));
    hash = fnv1a(hash, number(program.domain.maximum[axis]));
  }
  for (const std::string& parameter : program.parameterNames) hash = fnv1a(hash, "p:" + parameter);
  for (const ScalarInstruction& instruction : program.instructions) {
    hash = fnv1a(hash, std::to_string(static_cast<uint32_t>(instruction.opcode)));
    hash = fnv1a(hash, std::to_string(instruction.operandCount));
    for (uint8_t index = 0; index < instruction.operandCount; ++index) {
      hash = fnv1a(hash, std::to_string(instruction.operands[index]));
    }
    hash = fnv1a(hash, number(instruction.immediate));
    hash = fnv1a(hash, std::to_string(instruction.parameterIndex));
  }
  return hash;
}

struct LoweringContext {
  ScalarComputeProgram program;
  std::vector<ValidationIssue> issues;
  std::unordered_map<std::string, uint32_t> commonExpressions;

  uint32_t append(ScalarInstruction instruction, const std::string& key) {
    if (instruction.operandCount > 0) {
      std::array<double, 3> values{};
      bool constant = true;
      for (uint8_t index = 0; index < instruction.operandCount; ++index) {
        const ScalarInstruction& operand = program.instructions.at(instruction.operands[index]);
        if (operand.opcode != ScalarOpcode::Constant) {
          constant = false;
          break;
        }
        values[index] = operand.immediate;
      }
      if (constant) {
        double folded = std::numeric_limits<double>::quiet_NaN();
        switch (instruction.opcode) {
          case ScalarOpcode::Add: folded = values[0] + values[1]; break;
          case ScalarOpcode::Subtract: folded = values[0] - values[1]; break;
          case ScalarOpcode::Multiply: folded = values[0] * values[1]; break;
          case ScalarOpcode::Divide: folded = values[0] / values[1]; break;
          case ScalarOpcode::Power: folded = std::pow(values[0], values[1]); break;
          case ScalarOpcode::Negate: folded = -values[0]; break;
          case ScalarOpcode::Sin: folded = std::sin(values[0]); break;
          case ScalarOpcode::Cos: folded = std::cos(values[0]); break;
          case ScalarOpcode::Tan: folded = std::tan(values[0]); break;
          case ScalarOpcode::Exp: folded = std::exp(values[0]); break;
          case ScalarOpcode::Sqrt: folded = std::sqrt(values[0]); break;
          case ScalarOpcode::Abs: folded = std::abs(values[0]); break;
          case ScalarOpcode::Log: folded = std::log(values[0]); break;
          case ScalarOpcode::Min: folded = std::min(values[0], values[1]); break;
          case ScalarOpcode::Max: folded = std::max(values[0], values[1]); break;
          case ScalarOpcode::Clamp: folded = std::clamp(values[0], values[1], values[2]); break;
          default: break;
        }
        if (std::isfinite(folded)) {
          ScalarInstruction foldedInstruction{};
          foldedInstruction.immediate = folded;
          return append(foldedInstruction, "constant:" + number(folded));
        }
      }
    }
    if (const auto found = commonExpressions.find(key); found != commonExpressions.end()) return found->second;
    const uint32_t index = static_cast<uint32_t>(program.instructions.size());
    program.instructions.push_back(instruction);
    commonExpressions.emplace(key, index);
    return index;
  }

  uint32_t lower(const equation::EquationNode& node) {
    const auto unary = [&](ScalarOpcode opcode, const char* name) {
      if (node.children.size() != 1) {
        issues.push_back({std::string{name} + " requires one operand"});
        return append({}, "invalid:" + std::to_string(issues.size()));
      }
      const uint32_t operand = lower(node.children[0]);
      ScalarInstruction instruction{opcode, {operand, 0, 0}, 1};
      return append(instruction, std::to_string(static_cast<uint32_t>(opcode)) + ":" + std::to_string(operand));
    };
    const auto binary = [&](ScalarOpcode opcode, const char* name) {
      if (node.children.size() != 2) {
        issues.push_back({std::string{name} + " requires two operands"});
        return append({}, "invalid:" + std::to_string(issues.size()));
      }
      const uint32_t lhs = lower(node.children[0]);
      const uint32_t rhs = lower(node.children[1]);
      ScalarInstruction instruction{opcode, {lhs, rhs, 0}, 2};
      return append(instruction, std::to_string(static_cast<uint32_t>(opcode)) + ":" +
                                     std::to_string(lhs) + ":" + std::to_string(rhs));
    };
    switch (node.kind) {
      case equation::NodeKind::Constant: {
        ScalarInstruction instruction{};
        instruction.immediate = node.value;
        return append(instruction, "constant:" + number(node.value));
      }
      case equation::NodeKind::Variable: {
        const std::array<std::pair<const char*, ScalarOpcode>, 4> builtins{{
            {"x", ScalarOpcode::CoordinateX}, {"y", ScalarOpcode::CoordinateY},
            {"z", ScalarOpcode::CoordinateZ}, {"t", ScalarOpcode::Time}}};
        for (const auto& [name, opcode] : builtins) {
          if (node.symbol == name) return append({opcode}, "builtin:" + node.symbol);
        }
        const auto parameter = std::find(program.parameterNames.begin(), program.parameterNames.end(), node.symbol);
        if (parameter == program.parameterNames.end()) {
          issues.push_back({"unbound compute symbol '" + node.symbol + "'"});
          return append({}, "invalid-symbol:" + node.symbol);
        }
        ScalarInstruction instruction{ScalarOpcode::Parameter};
        instruction.parameterIndex = static_cast<uint32_t>(std::distance(program.parameterNames.begin(), parameter));
        return append(instruction, "parameter:" + node.symbol);
      }
      case equation::NodeKind::Add: return binary(ScalarOpcode::Add, "addition");
      case equation::NodeKind::Subtract: return binary(ScalarOpcode::Subtract, "subtraction");
      case equation::NodeKind::Multiply: return binary(ScalarOpcode::Multiply, "multiplication");
      case equation::NodeKind::Divide: return binary(ScalarOpcode::Divide, "division");
      case equation::NodeKind::Power: return binary(ScalarOpcode::Power, "power");
      case equation::NodeKind::Negate: return unary(ScalarOpcode::Negate, "negation");
      case equation::NodeKind::Function: {
        const std::map<std::string, ScalarOpcode> unaryFunctions{
            {"sin", ScalarOpcode::Sin}, {"cos", ScalarOpcode::Cos}, {"tan", ScalarOpcode::Tan},
            {"exp", ScalarOpcode::Exp}, {"sqrt", ScalarOpcode::Sqrt}, {"abs", ScalarOpcode::Abs},
            {"log", ScalarOpcode::Log}};
        if (const auto found = unaryFunctions.find(node.symbol); found != unaryFunctions.end()) {
          return unary(found->second, node.symbol.c_str());
        }
        if (node.symbol == "min") return binary(ScalarOpcode::Min, "min");
        if (node.symbol == "max") return binary(ScalarOpcode::Max, "max");
        if (node.symbol == "clamp") {
          if (node.children.size() != 3) {
            issues.push_back({"clamp requires three operands"});
            return append({}, "invalid-clamp");
          }
          const uint32_t value = lower(node.children[0]);
          const uint32_t minimum = lower(node.children[1]);
          const uint32_t maximum = lower(node.children[2]);
          ScalarInstruction instruction{ScalarOpcode::Clamp, {value, minimum, maximum}, 3};
          return append(instruction, "clamp:" + std::to_string(value) + ":" +
                                         std::to_string(minimum) + ":" + std::to_string(maximum));
        }
        issues.push_back({"unsupported compute function '" + node.symbol + "'"});
        return append({}, "invalid-function:" + node.symbol);
      }
    }
    issues.push_back({"unsupported equation node"});
    return append({}, "invalid-node");
  }
};

double apply(const ScalarInstruction& instruction, const std::vector<double>& registers,
             const std::array<double, 3>& position, double timeSeconds,
             const std::vector<double>& parameters) {
  const auto value = [&](uint8_t index) { return registers.at(instruction.operands[index]); };
  switch (instruction.opcode) {
    case ScalarOpcode::Constant: return instruction.immediate;
    case ScalarOpcode::CoordinateX: return position[0];
    case ScalarOpcode::CoordinateY: return position[1];
    case ScalarOpcode::CoordinateZ: return position[2];
    case ScalarOpcode::Time: return timeSeconds;
    case ScalarOpcode::Parameter: return parameters.at(instruction.parameterIndex);
    case ScalarOpcode::Add: return value(0) + value(1);
    case ScalarOpcode::Subtract: return value(0) - value(1);
    case ScalarOpcode::Multiply: return value(0) * value(1);
    case ScalarOpcode::Divide: return value(0) / value(1);
    case ScalarOpcode::Power: return std::pow(value(0), value(1));
    case ScalarOpcode::Negate: return -value(0);
    case ScalarOpcode::Sin: return std::sin(value(0));
    case ScalarOpcode::Cos: return std::cos(value(0));
    case ScalarOpcode::Tan: return std::tan(value(0));
    case ScalarOpcode::Exp: return std::exp(value(0));
    case ScalarOpcode::Sqrt: return std::sqrt(value(0));
    case ScalarOpcode::Abs: return std::abs(value(0));
    case ScalarOpcode::Log: return std::log(value(0));
    case ScalarOpcode::Min: return std::min(value(0), value(1));
    case ScalarOpcode::Max: return std::max(value(0), value(1));
    case ScalarOpcode::Clamp: return std::clamp(value(0), value(1), value(2));
  }
  throw std::runtime_error("invalid scalar opcode");
}

const char* functionName(ScalarOpcode opcode) {
  switch (opcode) {
    case ScalarOpcode::Power: return "pow";
    case ScalarOpcode::Sin: return "sin";
    case ScalarOpcode::Cos: return "cos";
    case ScalarOpcode::Tan: return "tan";
    case ScalarOpcode::Exp: return "exp";
    case ScalarOpcode::Sqrt: return "sqrt";
    case ScalarOpcode::Abs: return "abs";
    case ScalarOpcode::Log: return "log";
    case ScalarOpcode::Min: return "min";
    case ScalarOpcode::Max: return "max";
    case ScalarOpcode::Clamp: return "clamp";
    default: return nullptr;
  }
}

std::string instructionExpression(const ScalarInstruction& instruction,
                                  const std::vector<std::string>& parameters) {
  const auto reg = [&](uint8_t index) { return "r" + std::to_string(instruction.operands[index]); };
  switch (instruction.opcode) {
    case ScalarOpcode::Constant: return number(instruction.immediate);
    case ScalarOpcode::CoordinateX: return "x";
    case ScalarOpcode::CoordinateY: return "y";
    case ScalarOpcode::CoordinateZ: return "z";
    case ScalarOpcode::Time: return "parameters.t";
    case ScalarOpcode::Parameter: return "parameters." + parameters.at(instruction.parameterIndex);
    case ScalarOpcode::Add: return "(" + reg(0) + " + " + reg(1) + ")";
    case ScalarOpcode::Subtract: return "(" + reg(0) + " - " + reg(1) + ")";
    case ScalarOpcode::Multiply: return "(" + reg(0) + " * " + reg(1) + ")";
    case ScalarOpcode::Divide: return "(" + reg(0) + " / " + reg(1) + ")";
    case ScalarOpcode::Negate: return "(-" + reg(0) + ")";
    default: {
      const char* name = functionName(instruction.opcode);
      if (name == nullptr) throw std::runtime_error("opcode cannot be emitted");
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

ComputeLoweringResult lowerScalarFieldProgram(
    const PhysicsModel& model, const std::string& outputField,
    const equation::ScalarExpression& expression, std::vector<std::string> parameterNames) {
  ComputeLoweringResult result{};
  const ValidationResult validation = validate(model);
  if (!validation.valid()) {
    result.issues = validation.issues;
    return result;
  }
  const auto field = std::find_if(model.fields.begin(), model.fields.end(), [&](const FieldDeclaration& declaration) {
    return declaration.name == outputField;
  });
  if (field == model.fields.end()) {
    result.issues.push_back({"compute output references unknown field '" + outputField + "'"});
    return result;
  }
  if (field->valueType != ValueType::Scalar || field->placement != FieldPlacement::CellCenter) {
    result.issues.push_back({"scalar compute output must be a cell-centred scalar field"});
    return result;
  }
  std::set<std::string> uniqueParameters;
  for (const std::string& parameter : parameterNames) {
    if (parameter.empty() || !uniqueParameters.insert(parameter).second) {
      result.issues.push_back({"compute parameter names must be non-empty and unique"});
      return result;
    }
  }
  LoweringContext context{};
  context.program.outputField = outputField;
  context.program.domain = model.domain;
  context.program.parameterNames = std::move(parameterNames);
  context.program.outputRegister = context.lower(expression.root());
  if (!context.issues.empty()) {
    result.issues = std::move(context.issues);
    return result;
  }
  context.program.canonicalHash = programHash(context.program);
  result.program = std::move(context.program);
  return result;
}

double executeScalarProgram(
    const ScalarComputeProgram& program, const std::array<double, 3>& position,
    double timeSeconds, const std::map<std::string, double>& parameterValues) {
  std::vector<double> parameters;
  parameters.reserve(program.parameterNames.size());
  for (const std::string& name : program.parameterNames) {
    const auto found = parameterValues.find(name);
    if (found == parameterValues.end()) throw std::invalid_argument("missing compute parameter '" + name + "'");
    parameters.push_back(found->second);
  }
  std::vector<double> registers;
  registers.reserve(program.instructions.size());
  for (const ScalarInstruction& instruction : program.instructions) {
    registers.push_back(apply(instruction, registers, position, timeSeconds, parameters));
  }
  return registers.at(program.outputRegister);
}

std::vector<float> executeScalarField2D(
    const ScalarComputeProgram& program, uint32_t width, uint32_t height,
    double timeSeconds, const std::map<std::string, double>& parameters) {
  if (width == 0 || height == 0) throw std::invalid_argument("field extent must be positive");
  std::vector<float> values(static_cast<size_t>(width) * height);
  const double z = 0.5 * (program.domain.minimum[2] + program.domain.maximum[2]);
  for (uint32_t y = 0; y < height; ++y) {
    const double fy = static_cast<double>(y) / std::max(1u, height - 1);
    const double py = std::lerp(program.domain.minimum[1], program.domain.maximum[1], fy);
    for (uint32_t x = 0; x < width; ++x) {
      const double fx = static_cast<double>(x) / std::max(1u, width - 1);
      const double px = std::lerp(program.domain.minimum[0], program.domain.maximum[0], fx);
      values[static_cast<size_t>(y) * width + x] = static_cast<float>(
          executeScalarProgram(program, {px, py, z}, timeSeconds, parameters));
    }
  }
  return values;
}

std::string emitScalarProgramGlsl(const ScalarComputeProgram& program) {
  std::ostringstream shader;
  shader << "#version 450\n"
            "layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;\n"
            "layout(std430, binding = 0) writeonly buffer FieldOutput { float values[]; } outputField;\n"
            "layout(std140, binding = 1) uniform FieldParameters {\n"
            "  uint width;\n  uint height;\n  float t;\n";
  for (const std::string& parameter : program.parameterNames) shader << "  float " << parameter << ";\n";
  shader << "} parameters;\nvoid main() {\n"
            "  uvec2 pixel = gl_GlobalInvocationID.xy;\n"
            "  if (pixel.x >= parameters.width || pixel.y >= parameters.height) return;\n"
         << "  float x = mix(" << number(program.domain.minimum[0]) << ", "
         << number(program.domain.maximum[0]) << ", float(pixel.x) / max(1.0, float(parameters.width - 1u)));\n"
         << "  float y = mix(" << number(program.domain.minimum[1]) << ", "
         << number(program.domain.maximum[1]) << ", float(pixel.y) / max(1.0, float(parameters.height - 1u)));\n"
         << "  float z = " << number(0.5 * (program.domain.minimum[2] + program.domain.maximum[2])) << ";\n";
  for (size_t index = 0; index < program.instructions.size(); ++index) {
    shader << "  float r" << index << " = "
           << instructionExpression(program.instructions[index], program.parameterNames) << ";\n";
  }
  shader << "  outputField.values[pixel.y * parameters.width + pixel.x] = r"
         << program.outputRegister << ";\n}\n";
  return shader.str();
}

std::string emitScalarProgramMsl(const ScalarComputeProgram& program) {
  std::ostringstream shader;
  shader << "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct FieldParameters { uint width; uint height; float t;";
  for (const std::string& parameter : program.parameterNames) shader << " float " << parameter << ";";
  shader << " };\nkernel void executeScalarField(\n"
            "    device float* outputField [[buffer(0)]],\n"
            "    constant FieldParameters& parameters [[buffer(1)]],\n"
            "    uint2 pixel [[thread_position_in_grid]]) {\n"
            "  if (pixel.x >= parameters.width || pixel.y >= parameters.height) return;\n"
         << "  float x = mix(" << number(program.domain.minimum[0]) << "f, "
         << number(program.domain.maximum[0]) << "f, float(pixel.x) / max(1.0f, float(parameters.width - 1u)));\n"
         << "  float y = mix(" << number(program.domain.minimum[1]) << "f, "
         << number(program.domain.maximum[1]) << "f, float(pixel.y) / max(1.0f, float(parameters.height - 1u)));\n"
         << "  float z = " << number(0.5 * (program.domain.minimum[2] + program.domain.maximum[2])) << "f;\n";
  for (size_t index = 0; index < program.instructions.size(); ++index) {
    shader << "  float r" << index << " = "
           << instructionExpression(program.instructions[index], program.parameterNames) << ";\n";
  }
  shader << "  outputField[pixel.y * parameters.width + pixel.x] = r"
         << program.outputRegister << ";\n}\n";
  return shader.str();
}

}  // namespace vulkax::physics
