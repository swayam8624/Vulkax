#include "vulkax/equation/glsl_compiler.hpp"

#include <cassert>
#include <iostream>

int main() {
  using namespace vulkax::equation;
  const auto wave = findPreset("wave-field");
  assert(wave.has_value());
  const auto compiled = compilePresetToGlsl(*wave);
  assert(compiled.succeeded);
  assert(compiled.diagnostics.empty());
  assert(compiled.scalarExpression.find("sin") != std::string::npos);
  assert(compiled.computeShader.find("layout(local_size_x = 16") != std::string::npos);
  assert(compiled.computeShader.find("float amplitude") != std::string::npos);

  EquationPreset invalid{};
  invalid.id = "invalid";
  invalid.expressions = {"unknown + 1"};
  const auto rejected = compilePresetToGlsl(invalid);
  assert(!rejected.succeeded);
  assert(!rejected.diagnostics.empty());
  std::cout << "Vulkax GLSL compiler tests passed\n";
  return 0;
}
