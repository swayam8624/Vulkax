#include "vulkax/equation/glsl_compiler.hpp"

#include <iomanip>
#include <set>
#include <sstream>

namespace vulkax::equation {
namespace {

std::string emitNode(
    const EquationNode& node,
    const std::set<std::string>& allowed,
    const std::set<std::string>& parameters,
    std::vector<std::string>& diagnostics) {
  auto child = [&](size_t index) {
    if (index >= node.children.size()) {
      diagnostics.push_back("missing function/operator argument");
      return std::string{"0.0"};
    }
    return emitNode(node.children[index], allowed, parameters, diagnostics);
  };
  switch (node.kind) {
    case NodeKind::Constant: {
      std::ostringstream stream;
      stream << std::setprecision(17) << node.value;
      const std::string value = stream.str();
      return value.find_first_of(".eE") == std::string::npos ? value + ".0" : value;
    }
    case NodeKind::Variable:
      if (!allowed.contains(node.symbol)) {
        diagnostics.push_back("unbound GLSL variable '" + node.symbol + "'");
      }
      return parameters.contains(node.symbol) ? "parameters." + node.symbol : node.symbol;
    case NodeKind::Add: return "(" + child(0) + " + " + child(1) + ")";
    case NodeKind::Subtract: return "(" + child(0) + " - " + child(1) + ")";
    case NodeKind::Multiply: return "(" + child(0) + " * " + child(1) + ")";
    case NodeKind::Divide: return "(" + child(0) + " / " + child(1) + ")";
    case NodeKind::Power: return "pow(" + child(0) + ", " + child(1) + ")";
    case NodeKind::Negate: return "(-" + child(0) + ")";
    case NodeKind::Function:
      if (node.symbol != "sin" && node.symbol != "cos" && node.symbol != "tan" &&
          node.symbol != "exp" && node.symbol != "sqrt" && node.symbol != "abs" &&
          node.symbol != "log" && node.symbol != "min" && node.symbol != "max" &&
          node.symbol != "clamp") {
        diagnostics.push_back("unsupported GLSL function '" + node.symbol + "'");
      }
      {
        std::string result = node.symbol + "(";
        for (size_t index = 0; index < node.children.size(); ++index) {
          if (index != 0) result += ", ";
          result += child(index);
        }
        return result + ")";
      }
  }
  diagnostics.push_back("unsupported AST node");
  return "0.0";
}

}  // namespace

GlslCompileResult compilePresetToGlsl(const EquationPreset& preset) {
  GlslCompileResult result{};
  if (preset.expressions.size() != 1) {
    result.diagnostics.push_back("scalar field compute emission requires exactly one expression");
    return result;
  }
  try {
    const ScalarExpression expression = parseScalarExpression(preset.expressions.front());
    std::set<std::string> allowed{"x", "y", "z", "t"};
    std::set<std::string> parameters;
    for (const auto& parameter : preset.parameters) {
      allowed.insert(parameter.name);
      parameters.insert(parameter.name);
    }
    result.scalarExpression = emitNode(expression.root(), allowed, parameters, result.diagnostics);
    if (!result.diagnostics.empty()) return result;

    std::ostringstream uniforms;
    for (const auto& parameter : preset.parameters) {
      uniforms << "  float " << parameter.name << ";\n";
    }
    result.computeShader =
        "#version 450\n"
        "layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;\n"
        "layout(std430, binding = 0) writeonly buffer FieldOutput { float values[]; } outputField;\n"
        "layout(std140, binding = 1) uniform FieldParameters {\n"
        "  uint width;\n  uint height;\n  float t;\n" + uniforms.str() +
        "} parameters;\n"
        "void main() {\n"
        "  uvec2 pixel = gl_GlobalInvocationID.xy;\n"
        "  if (pixel.x >= parameters.width || pixel.y >= parameters.height) return;\n"
        "  float x = (float(pixel.x) / max(1.0, float(parameters.width - 1u))) * 8.0 - 4.0;\n"
        "  float y = (float(pixel.y) / max(1.0, float(parameters.height - 1u))) * 8.0 - 4.0;\n"
        "  float z = 0.0;\n"
        "  float t = parameters.t;\n"
        "  outputField.values[pixel.y * parameters.width + pixel.x] = " + result.scalarExpression + ";\n"
        "}\n";
    result.succeeded = true;
  } catch (const std::exception& error) {
    result.diagnostics.push_back(error.what());
  }
  return result;
}

}  // namespace vulkax::equation
