#include "vulkax/equation/glsl_compiler.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  try {
    std::string presetId;
    std::filesystem::path output;
    for (int index = 1; index < argc; ++index) {
      const std::string argument{argv[index]};
      if (argument == "--preset" && index + 1 < argc) presetId = argv[++index];
      else if (argument == "--output" && index + 1 < argc) output = argv[++index];
      else throw std::invalid_argument("usage: vulkax-equation-shader --preset ID --output PATH");
    }
    const auto preset = vulkax::equation::findPreset(presetId);
    if (!preset || output.empty()) throw std::invalid_argument("known preset and output are required");
    const auto generated = vulkax::equation::compilePresetToGlsl(*preset);
    if (!generated.succeeded) throw std::runtime_error(generated.diagnostics.empty() ? "GLSL emission failed" : generated.diagnostics.front());
    std::filesystem::create_directories(output.parent_path());
    std::ofstream stream{output};
    if (!stream) throw std::runtime_error("could not write generated shader");
    stream << generated.computeShader;
    std::cout << "Generated " << preset->id << " GLSL at " << output << '\n';
  } catch (const std::exception& error) {
    std::cerr << "vulkax-equation-shader: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
