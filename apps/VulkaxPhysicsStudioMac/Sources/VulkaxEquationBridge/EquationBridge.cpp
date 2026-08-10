#include "VulkaxEquationBridge.h"

#include "vulkax/equation/equation.hpp"
#include "vulkax/physics/compute_ir.hpp"
#include "vulkax/physics/physics_ir.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct CompileResult {
  bool success = false;
  std::string metalSource;
  std::string parameterNames;
  std::string diagnostic;
  uint64_t canonicalHash = 0;
};

CompileResult compile(const char* source) {
  CompileResult result{};
  if (source == nullptr) {
    result.diagnostic = "equation source is null";
    return result;
  }
  try {
    const auto expression = vulkax::equation::parseScalarExpression(source);
    std::vector<std::string> parameters = vulkax::equation::variableNames(expression);
    parameters.erase(
        std::remove_if(parameters.begin(), parameters.end(), [](const std::string& name) {
          return name == "x" || name == "y" || name == "z" || name == "t";
        }),
        parameters.end());
    if (parameters.size() > 16) {
      result.diagnostic = "The GPU runtime supports at most 16 parameters; this equation uses " +
                          std::to_string(parameters.size());
      return result;
    }

    vulkax::physics::PhysicsModel model{};
    model.name = "native-scalar-equation";
    model.domain.minimum = {-4.0, -4.0, 0.0};
    model.domain.maximum = {4.0, 4.0, 0.0};
    model.domain.resolution = {128, 72, 1};
    model.fields.push_back({
        "value",
        vulkax::physics::ValueType::Scalar,
        vulkax::physics::Dimension::dimensionless(),
        vulkax::physics::FieldPlacement::CellCenter});

    auto lowered = vulkax::physics::lowerScalarFieldProgram(
        model, "value", expression, parameters);
    if (!lowered.valid()) {
      std::ostringstream message;
      for (size_t index = 0; index < lowered.issues.size(); ++index) {
        if (index != 0) message << "; ";
        message << lowered.issues[index].message;
      }
      result.diagnostic = message.str().empty() ? "equation lowering failed" : message.str();
      return result;
    }

    const auto& program = *lowered.program;
    result.metalSource = vulkax::physics::emitScalarProgramNativeMetalTexture(program);
    result.canonicalHash = program.canonicalHash;
    for (size_t index = 0; index < program.parameterNames.size(); ++index) {
      if (index != 0) result.parameterNames.push_back('\n');
      result.parameterNames += program.parameterNames[index];
    }
    result.success = true;
    return result;
  } catch (const std::exception& error) {
    result.diagnostic = error.what();
    return result;
  }
}

CompileResult* unwrap(VulkaxCompiledEquationHandle handle) {
  return static_cast<CompileResult*>(handle);
}

}  // namespace

extern "C" VulkaxCompiledEquationHandle vulkax_compile_scalar_equation(const char* source) {
  try {
    return new CompileResult(compile(source));
  } catch (...) {
    return nullptr;
  }
}

extern "C" int32_t vulkax_compiled_equation_success(VulkaxCompiledEquationHandle handle) {
  const CompileResult* result = unwrap(handle);
  return result != nullptr && result->success ? 1 : 0;
}

extern "C" const char* vulkax_compiled_equation_metal_source(VulkaxCompiledEquationHandle handle) {
  const CompileResult* result = unwrap(handle);
  return result == nullptr ? "" : result->metalSource.c_str();
}

extern "C" const char* vulkax_compiled_equation_parameter_names(VulkaxCompiledEquationHandle handle) {
  const CompileResult* result = unwrap(handle);
  return result == nullptr ? "" : result->parameterNames.c_str();
}

extern "C" const char* vulkax_compiled_equation_diagnostic(VulkaxCompiledEquationHandle handle) {
  const CompileResult* result = unwrap(handle);
  return result == nullptr ? "equation compiler allocation failed" : result->diagnostic.c_str();
}

extern "C" uint64_t vulkax_compiled_equation_canonical_hash(VulkaxCompiledEquationHandle handle) {
  const CompileResult* result = unwrap(handle);
  return result == nullptr ? 0 : result->canonicalHash;
}

extern "C" void vulkax_destroy_compiled_equation(VulkaxCompiledEquationHandle handle) {
  delete unwrap(handle);
}
