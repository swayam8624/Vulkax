#include "VulkaxEquationBridge.h"
#include "vulkax/equation/equation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct CompileResult {
  bool success = false;
  std::string metalSource;
  std::string parameterNames;
  std::string diagnostic;
  uint64_t canonicalHash = 0;
};

std::string number(double value) {
  std::ostringstream stream;
  stream << std::setprecision(17) << value;
  std::string result = stream.str();
  if (result.find_first_of(".eE") == std::string::npos) result += ".0";
  return result;
}

uint64_t mixHash(uint64_t hash, const void* data, size_t size) {
  constexpr uint64_t prime = 1099511628211ull;
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= prime;
  }
  return hash;
}

uint64_t canonicalHashNode(const vulkax::equation::EquationNode& node, uint64_t hash) {
  const uint32_t kind = static_cast<uint32_t>(node.kind);
  hash = mixHash(hash, &kind, sizeof(kind));
  const uint64_t bits = std::bit_cast<uint64_t>(node.value);
  hash = mixHash(hash, &bits, sizeof(bits));
  hash = mixHash(hash, node.symbol.data(), node.symbol.size());
  const uint64_t count = node.children.size();
  hash = mixHash(hash, &count, sizeof(count));
  for (const auto& child : node.children) hash = canonicalHashNode(child, hash);
  return hash;
}

std::string emitNode(
    const vulkax::equation::EquationNode& node,
    const std::unordered_map<std::string, size_t>& parameterIndices) {
  using vulkax::equation::NodeKind;
  const auto child = [&](size_t index) -> std::string {
    if (index >= node.children.size()) throw std::invalid_argument("equation node is missing an operand");
    return emitNode(node.children[index], parameterIndices);
  };
  switch (node.kind) {
    case NodeKind::Constant: return "float(" + number(node.value) + ")";
    case NodeKind::Variable: {
      if (node.symbol == "x" || node.symbol == "y" || node.symbol == "z" || node.symbol == "t") return node.symbol;
      const auto found = parameterIndices.find(node.symbol);
      if (found == parameterIndices.end()) throw std::invalid_argument("unbound equation symbol '" + node.symbol + "'");
      return "parameters[" + std::to_string(found->second) + "]";
    }
    case NodeKind::Add: return "(" + child(0) + " + " + child(1) + ")";
    case NodeKind::Subtract: return "(" + child(0) + " - " + child(1) + ")";
    case NodeKind::Multiply: return "(" + child(0) + " * " + child(1) + ")";
    case NodeKind::Divide: return "(" + child(0) + " / " + child(1) + ")";
    case NodeKind::Power: return "pow(" + child(0) + ", " + child(1) + ")";
    case NodeKind::Negate: return "(-" + child(0) + ")";
    case NodeKind::Function: break;
  }
  const std::unordered_map<std::string, size_t> arity{
      {"sin", 1}, {"cos", 1}, {"tan", 1}, {"exp", 1}, {"sqrt", 1},
      {"abs", 1}, {"log", 1}, {"min", 2}, {"max", 2}, {"clamp", 3}};
  const auto expected = arity.find(node.symbol);
  if (expected == arity.end()) throw std::invalid_argument("unsupported equation function '" + node.symbol + "'");
  if (node.children.size() != expected->second) {
    throw std::invalid_argument("function '" + node.symbol + "' expects " + std::to_string(expected->second) + " arguments");
  }
  std::string result = node.symbol + "(";
  for (size_t index = 0; index < node.children.size(); ++index) {
    if (index != 0) result += ", ";
    result += emitNode(node.children[index], parameterIndices);
  }
  return result + ")";
}

std::string metalKernel(const std::string& expression, uint64_t hash) {
  std::ostringstream source;
  source << R"MSL(#include <metal_stdlib>
using namespace metal;
struct Uniforms {
    float time; float amplitude; float wavenumber; float angularFrequency;
    float width; float height; float4 padding; float4 control; float4 renderParameters;
    float4 cameraPositionExposure; float4 cameraTarget; float4 cameraUpFov;
};
float3 equationPalette(float value) {
    float shadow = pow(clamp(value, 0.0f, 1.0f), 0.72f);
    return float3(0.025f + 0.91f * pow(shadow, 1.55f),
                  0.055f + 0.54f * sin(shadow * 1.47f),
                  0.13f + 0.70f * (1.0f - shadow) * (1.0f - shadow));
}
kernel void renderCompiledEquation(
    texture2d<half, access::write> outputRadiance [[texture(0)]],
    constant Uniforms& u [[buffer(0)]],
    device const float* parameters [[buffer(1)]],
    uint2 pixel [[thread_position_in_grid]]) {
    if (pixel.x >= uint(u.width) || pixel.y >= uint(u.height)) return;
    float2 uv = (float2(pixel) + 0.5f) / float2(u.width, u.height);
    float aspect = u.width / max(1.0f, u.height);
    float x = (uv.x - 0.5f) * 8.0f * aspect;
    float y = (0.5f - uv.y) * 8.0f;
    float z = 0.0f;
    float t = u.time;
    float field = )MSL" << expression << R"MSL(;
    if (!isfinite(field)) field = 0.0f;
    float3 radiance = equationPalette(0.5f + 0.5f * tanh(field));
    outputRadiance.write(half4(half3(radiance * 2.25f), 1.0h), pixel);
}
)MSL";
  source << "// canonical C++ equation AST hash: " << hash << "\n";
  return source.str();
}

CompileResult compile(const char* source) {
  CompileResult result{};
  if (source == nullptr) { result.diagnostic = "equation source is null"; return result; }
  try {
    const auto parsed = vulkax::equation::parseScalarExpression(source);
    auto parameters = vulkax::equation::variableNames(parsed);
    parameters.erase(std::remove_if(parameters.begin(), parameters.end(), [](const std::string& name) {
      return name == "x" || name == "y" || name == "z" || name == "t";
    }), parameters.end());
    if (parameters.size() > 16) {
      result.diagnostic = "The GPU runtime supports at most 16 parameters; this equation uses " + std::to_string(parameters.size());
      return result;
    }
    std::unordered_map<std::string, size_t> indices;
    for (size_t index = 0; index < parameters.size(); ++index) indices.emplace(parameters[index], index);
    const std::string expression = emitNode(parsed.root(), indices);
    result.canonicalHash = canonicalHashNode(parsed.root(), 1469598103934665603ull);
    result.metalSource = metalKernel(expression, result.canonicalHash);
    for (size_t index = 0; index < parameters.size(); ++index) {
      if (index != 0) result.parameterNames.push_back('\n');
      result.parameterNames += parameters[index];
    }
    result.success = true;
  } catch (const std::exception& error) {
    result.diagnostic = error.what();
  }
  return result;
}

CompileResult* unwrap(VulkaxCompiledEquationHandle handle) {
  return static_cast<CompileResult*>(handle);
}
}  // namespace

extern "C" VulkaxCompiledEquationHandle vulkax_compile_scalar_equation(const char* source) {
  try { return new CompileResult(compile(source)); } catch (...) { return nullptr; }
}
extern "C" int32_t vulkax_compiled_equation_success(VulkaxCompiledEquationHandle handle) {
  const auto* result = unwrap(handle); return result != nullptr && result->success ? 1 : 0;
}
extern "C" const char* vulkax_compiled_equation_metal_source(VulkaxCompiledEquationHandle handle) {
  const auto* result = unwrap(handle); return result != nullptr ? result->metalSource.c_str() : "";
}
extern "C" const char* vulkax_compiled_equation_parameter_names(VulkaxCompiledEquationHandle handle) {
  const auto* result = unwrap(handle); return result != nullptr ? result->parameterNames.c_str() : "";
}
extern "C" const char* vulkax_compiled_equation_diagnostic(VulkaxCompiledEquationHandle handle) {
  const auto* result = unwrap(handle); return result != nullptr ? result->diagnostic.c_str() : "equation compiler allocation failed";
}
extern "C" uint64_t vulkax_compiled_equation_canonical_hash(VulkaxCompiledEquationHandle handle) {
  const auto* result = unwrap(handle); return result != nullptr ? result->canonicalHash : 0;
}
extern "C" void vulkax_destroy_compiled_equation(VulkaxCompiledEquationHandle handle) { delete unwrap(handle); }
