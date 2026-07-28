#pragma once

#include "vulkax/equation/equation.hpp"

#include <string>
#include <vector>

namespace vulkax::equation {

struct GlslCompileResult {
  bool succeeded = false;
  std::string scalarExpression;
  std::string computeShader;
  std::vector<std::string> diagnostics;
};

// Emits a self-contained storage-buffer compute shader for a scalar field.
// The caller owns Vulkan pipeline creation; this layer only establishes an
// inspectable CPU/GLSL semantic contract from the canonical AST.
[[nodiscard]] GlslCompileResult compilePresetToGlsl(const EquationPreset& preset);

}  // namespace vulkax::equation
