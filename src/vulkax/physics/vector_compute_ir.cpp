#include "vulkax/physics/vector_compute_ir.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace vulkax::physics {
namespace {

uint64_t hashCombine(uint64_t hash, uint64_t value) {
  constexpr uint64_t prime = 1099511628211ull;
  for (uint32_t shift = 0; shift < 64; shift += 8) {
    hash ^= static_cast<uint8_t>((value >> shift) & 0xffu);
    hash *= prime;
  }
  return hash;
}

std::string number(double value) {
  std::ostringstream stream;
  stream << std::setprecision(17) << value;
  std::string result = stream.str();
  if (result.find_first_of(".eE") == std::string::npos) result += ".0";
  return result;
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

std::string expression(
    const ScalarInstruction& instruction,
    const std::vector<std::string>& parameterNames,
    const std::string& registerPrefix,
    const std::string& parameterPrefix,
    const std::string& timeName) {
  const auto reg = [&](uint8_t index) {
    return registerPrefix + std::to_string(instruction.operands[index]);
  };
  switch (instruction.opcode) {
    case ScalarOpcode::Constant: return number(instruction.immediate);
    case ScalarOpcode::CoordinateX: return "x";
    case ScalarOpcode::CoordinateY: return "y";
    case ScalarOpcode::CoordinateZ: return "z";
    case ScalarOpcode::Time: return timeName;
    case ScalarOpcode::Parameter:
      return parameterPrefix + parameterNames.at(instruction.parameterIndex);
    case ScalarOpcode::Add: return "(" + reg(0) + " + " + reg(1) + ")";
    case ScalarOpcode::Subtract: return "(" + reg(0) + " - " + reg(1) + ")";
    case ScalarOpcode::Multiply: return "(" + reg(0) + " * " + reg(1) + ")";
    case ScalarOpcode::Divide: return "(" + reg(0) + " / " + reg(1) + ")";
    case ScalarOpcode::Negate: return "(-" + reg(0) + ")";
    default: {
      const char* function = functionName(instruction.opcode);
      if (function == nullptr) throw std::runtime_error("unsupported vector shader opcode");
      std::string result = std::string{function} + "(";
      for (uint8_t index = 0; index < instruction.operandCount; ++index) {
        if (index != 0) result += ", ";
        result += reg(index);
      }
      return result + ")";
    }
  }
}

uint32_t componentCount(ValueType type) {
  if (type == ValueType::Vector2) return 2;
  if (type == ValueType::Vector3) return 3;
  return 0;
}

void emitComponent(
    std::ostringstream& shader,
    const ScalarComputeProgram& component,
    size_t componentIndex,
    const std::string& parameterPrefix,
    const std::string& timeName,
    const std::string& indentation) {
  const std::string prefix = "c" + std::to_string(componentIndex) + "r";
  for (size_t index = 0; index < component.instructions.size(); ++index) {
    shader << indentation << "float " << prefix << index << " = "
           << expression(
                  component.instructions[index], component.parameterNames,
                  prefix, parameterPrefix, timeName)
           << ";\n";
  }
}

std::string outputConstructor(
    const VectorComputeProgram& program,
    const char* vectorType) {
  std::string result = std::string{vectorType} + "(";
  for (size_t component = 0; component < 3; ++component) {
    if (component != 0) result += ", ";
    if (component < program.components.size()) {
      result += "c" + std::to_string(component) + "r" +
                std::to_string(program.components[component].outputRegister);
    } else {
      result += "0.0";
    }
  }
  result += ", 0.0)";
  return result;
}

}  // namespace

VectorComputeLoweringResult lowerVectorFieldProgram(
    const PhysicsModel& model,
    const std::string& outputField,
    const std::vector<equation::ScalarExpression>& componentExpressions,
    std::vector<std::string> parameterNames) {
  VectorComputeLoweringResult result{};
  const auto field = std::find_if(
      model.fields.begin(), model.fields.end(), [&](const FieldDeclaration& declaration) {
        return declaration.name == outputField;
      });
  if (field == model.fields.end()) {
    result.issues.push_back({"vector compute output references unknown field '" + outputField + "'"});
    return result;
  }
  const uint32_t expected = componentCount(field->valueType);
  if (expected == 0 || field->placement != FieldPlacement::CellCenter) {
    result.issues.push_back({"vector compute output must be a cell-centred Vector2 or Vector3 field"});
    return result;
  }
  if (componentExpressions.size() != expected) {
    result.issues.push_back({"vector compute expression count does not match output field dimension"});
    return result;
  }

  PhysicsModel scalarModel = model;
  auto scalarField = std::find_if(
      scalarModel.fields.begin(), scalarModel.fields.end(), [&](const FieldDeclaration& declaration) {
        return declaration.name == outputField;
      });
  scalarField->valueType = ValueType::Scalar;

  VectorComputeProgram program{};
  program.outputField = outputField;
  program.valueType = field->valueType;
  uint64_t hash = 1469598103934665603ull;
  for (const auto& componentExpression : componentExpressions) {
    auto lowered = lowerScalarFieldProgram(
        scalarModel, outputField, componentExpression, parameterNames);
    if (!lowered.valid()) {
      result.issues.insert(
          result.issues.end(), lowered.issues.begin(), lowered.issues.end());
      return result;
    }
    hash = hashCombine(hash, lowered.program->canonicalHash);
    program.components.push_back(std::move(*lowered.program));
  }
  program.canonicalHash = hash;
  result.program = std::move(program);
  return result;
}

std::array<double, 3> executeVectorProgram(
    const VectorComputeProgram& program,
    const std::array<double, 3>& position,
    double timeSeconds,
    const std::map<std::string, double>& parameters) {
  std::array<double, 3> value{};
  for (size_t component = 0; component < program.components.size(); ++component) {
    value[component] = executeScalarProgram(
        program.components[component], position, timeSeconds, parameters);
  }
  return value;
}

std::string emitVectorProgramGlsl(const VectorComputeProgram& program) {
  if (program.components.empty()) throw std::invalid_argument("vector compute program has no components");
  const auto& first = program.components.front();
  std::ostringstream shader;
  shader << "#version 450\n"
            "layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;\n"
            "layout(set = 0, binding = 0) buffer OutputField { vec4 values[]; } outputField;\n"
            "layout(set = 0, binding = 1) uniform Parameters {\n"
            "  uint width; uint height; float t; float _padding;\n";
  for (const auto& name : first.parameterNames) shader << "  float " << name << ";\n";
  shader << "} parameters;\nvoid main() {\n"
            "  uvec2 pixel = gl_GlobalInvocationID.xy;\n"
            "  if (pixel.x >= parameters.width || pixel.y >= parameters.height) return;\n"
            "  vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(parameters.width, parameters.height);\n"
         << "  float x = mix(" << number(first.domain.minimum[0]) << ", "
         << number(first.domain.maximum[0]) << ", uv.x);\n"
         << "  float y = mix(" << number(first.domain.minimum[1]) << ", "
         << number(first.domain.maximum[1]) << ", uv.y);\n"
         << "  float z = " << number(0.5 * (first.domain.minimum[2] + first.domain.maximum[2])) << ";\n";
  for (size_t component = 0; component < program.components.size(); ++component) {
    emitComponent(shader, program.components[component], component, "parameters.", "parameters.t", "  ");
  }
  shader << "  uint index = pixel.y * parameters.width + pixel.x;\n"
         << "  outputField.values[index] = " << outputConstructor(program, "vec4") << ";\n"
            "}\n"
         << "// vector Physics IR hash: " << program.canonicalHash << "\n";
  return shader.str();
}

std::string emitVectorProgramMsl(const VectorComputeProgram& program) {
  if (program.components.empty()) throw std::invalid_argument("vector compute program has no components");
  const auto& first = program.components.front();
  std::ostringstream shader;
  shader << "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct Parameters { uint width; uint height; float t; float _padding;\n";
  for (const auto& name : first.parameterNames) shader << "  float " << name << ";\n";
  shader << "};\n"
            "kernel void vectorField(\n"
            "    device float4* outputField [[buffer(0)]],\n"
            "    constant Parameters& parameters [[buffer(1)]],\n"
            "    uint2 pixel [[thread_position_in_grid]]) {\n"
            "  if (pixel.x >= parameters.width || pixel.y >= parameters.height) return;\n"
            "  float2 uv = (float2(pixel) + 0.5f) / float2(parameters.width, parameters.height);\n"
         << "  float x = mix(float(" << number(first.domain.minimum[0]) << "), float("
         << number(first.domain.maximum[0]) << "), uv.x);\n"
         << "  float y = mix(float(" << number(first.domain.minimum[1]) << "), float("
         << number(first.domain.maximum[1]) << "), uv.y);\n"
         << "  float z = float(" << number(0.5 * (first.domain.minimum[2] + first.domain.maximum[2])) << ");\n";
  for (size_t component = 0; component < program.components.size(); ++component) {
    emitComponent(shader, program.components[component], component, "parameters.", "parameters.t", "  ");
  }
  shader << "  uint index = pixel.y * parameters.width + pixel.x;\n"
         << "  outputField[index] = " << outputConstructor(program, "float4") << ";\n"
            "}\n"
         << "// vector Physics IR hash: " << program.canonicalHash << "\n";
  return shader.str();
}

}  // namespace vulkax::physics
