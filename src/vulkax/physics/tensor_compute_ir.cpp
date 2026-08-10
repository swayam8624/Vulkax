#include "vulkax/physics/tensor_compute_ir.hpp"

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
  const auto reg = [&](uint32_t operandIndex) {
    return registerPrefix + std::to_string(instruction.operands[operandIndex]);
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
      if (function == nullptr) throw std::runtime_error("unsupported tensor shader opcode");
      std::string result = std::string{function} + "(";
      for (uint8_t index = 0; index < instruction.operandCount; ++index) {
        if (index != 0) result += ", ";
        result += reg(index);
      }
      return result + ")";
    }
  }
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

std::string componentResult(const Tensor3ComputeProgram& program, size_t component) {
  return "c" + std::to_string(component) + "r" +
         std::to_string(program.components[component].outputRegister);
}

}  // namespace

TensorComputeLoweringResult lowerTensor3Program(
    const PhysicsModel& model,
    std::string outputName,
    const std::array<equation::ScalarExpression, 9>& componentExpressions,
    std::vector<std::string> parameterNames) {
  TensorComputeLoweringResult result{};
  if (outputName.empty()) {
    result.issues.push_back({"tensor compute output name cannot be empty"});
    return result;
  }

  PhysicsModel scalarModel = model;
  auto field = std::find_if(
      scalarModel.fields.begin(), scalarModel.fields.end(), [&](const FieldDeclaration& declaration) {
        return declaration.name == outputName;
      });
  if (field == scalarModel.fields.end()) {
    scalarModel.fields.push_back({
        outputName,
        ValueType::Scalar,
        Dimension::dimensionless(),
        FieldPlacement::CellCenter});
  } else {
    field->valueType = ValueType::Scalar;
    field->placement = FieldPlacement::CellCenter;
  }

  Tensor3ComputeProgram program{};
  program.outputName = std::move(outputName);
  uint64_t hash = 1469598103934665603ull;
  for (size_t component = 0; component < componentExpressions.size(); ++component) {
    auto lowered = lowerScalarFieldProgram(
        scalarModel, program.outputName, componentExpressions[component], parameterNames);
    if (!lowered.valid()) {
      result.issues.insert(result.issues.end(), lowered.issues.begin(), lowered.issues.end());
      return result;
    }
    hash = hashCombine(hash, lowered.program->canonicalHash);
    program.components[component] = std::move(*lowered.program);
  }
  program.canonicalHash = hash;
  result.program = std::move(program);
  return result;
}

std::array<double, 9> executeTensor3Program(
    const Tensor3ComputeProgram& program,
    const std::array<double, 3>& coordinates,
    double timeSeconds,
    const std::map<std::string, double>& parameters) {
  std::array<double, 9> value{};
  for (size_t component = 0; component < program.components.size(); ++component) {
    value[component] = executeScalarProgram(
        program.components[component], coordinates, timeSeconds, parameters);
  }
  return value;
}

std::string emitTensor3ProgramGlsl(const Tensor3ComputeProgram& program) {
  const auto& first = program.components.front();
  std::ostringstream shader;
  shader << "#version 450\n"
            "layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;\n"
            "layout(set = 0, binding = 0) buffer TensorField { vec4 rows[]; } outputField;\n"
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
  shader << "  uint base = (pixel.y * parameters.width + pixel.x) * 3u;\n"
         << "  outputField.rows[base + 0u] = vec4("
         << componentResult(program, 0) << ", " << componentResult(program, 1) << ", "
         << componentResult(program, 2) << ", 0.0);\n"
         << "  outputField.rows[base + 1u] = vec4("
         << componentResult(program, 3) << ", " << componentResult(program, 4) << ", "
         << componentResult(program, 5) << ", 0.0);\n"
         << "  outputField.rows[base + 2u] = vec4("
         << componentResult(program, 6) << ", " << componentResult(program, 7) << ", "
         << componentResult(program, 8) << ", 0.0);\n"
            "}\n"
         << "// tensor3 Physics IR hash: " << program.canonicalHash << "\n";
  return shader.str();
}

std::string emitTensor3ProgramMsl(const Tensor3ComputeProgram& program) {
  const auto& first = program.components.front();
  std::ostringstream shader;
  shader << "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct Parameters { uint width; uint height; float t; float _padding;\n";
  for (const auto& name : first.parameterNames) shader << "  float " << name << ";\n";
  shader << "};\n"
            "kernel void tensor3Field(\n"
            "    device float4* outputRows [[buffer(0)]],\n"
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
  shader << "  uint base = (pixel.y * parameters.width + pixel.x) * 3u;\n"
         << "  outputRows[base + 0u] = float4("
         << componentResult(program, 0) << ", " << componentResult(program, 1) << ", "
         << componentResult(program, 2) << ", 0.0f);\n"
         << "  outputRows[base + 1u] = float4("
         << componentResult(program, 3) << ", " << componentResult(program, 4) << ", "
         << componentResult(program, 5) << ", 0.0f);\n"
         << "  outputRows[base + 2u] = float4("
         << componentResult(program, 6) << ", " << componentResult(program, 7) << ", "
         << componentResult(program, 8) << ", 0.0f);\n"
            "}\n"
         << "// tensor3 Physics IR hash: " << program.canonicalHash << "\n";
  return shader.str();
}

}  // namespace vulkax::physics
