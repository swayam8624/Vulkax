#include "vulkax/equation/glsl_compiler.hpp"
#include "vulkax/physics/compute_ir.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  try {
    std::string presetId;
    std::string backend = "glsl";
    std::filesystem::path output;
    for (int index = 1; index < argc; ++index) {
      const std::string argument{argv[index]};
      if (argument == "--preset" && index + 1 < argc) presetId = argv[++index];
      else if (argument == "--backend" && index + 1 < argc) backend = argv[++index];
      else if (argument == "--output" && index + 1 < argc) output = argv[++index];
      else throw std::invalid_argument(
          "usage: vulkax-equation-shader --preset ID [--backend glsl|msl] --output PATH");
    }
    if (backend != "glsl" && backend != "msl") throw std::invalid_argument("backend must be glsl or msl");
    const auto preset = vulkax::equation::findPreset(presetId);
    if (!preset || output.empty()) throw std::invalid_argument("known preset and output are required");
    if (preset->expressions.size() != 1) throw std::runtime_error("scalar compute requires one expression");
    vulkax::physics::PhysicsModel model{};
    model.name = preset->id;
    model.domain.minimum = {-4.0, -4.0, -1.0};
    model.domain.maximum = {4.0, 4.0, 1.0};
    model.domain.resolution = {128, 72, 2};
    model.fields = {{"result", vulkax::physics::ValueType::Scalar,
                     vulkax::physics::Dimension::dimensionless(),
                     vulkax::physics::FieldPlacement::CellCenter}};
    std::vector<std::string> parameters;
    parameters.reserve(preset->parameters.size());
    for (const auto& parameter : preset->parameters) parameters.push_back(parameter.name);
    const auto expression = vulkax::equation::parseScalarExpression(preset->expressions.front());
    const auto lowered = vulkax::physics::lowerScalarFieldProgram(
        model, "result", expression, std::move(parameters));
    if (!lowered.valid()) {
      throw std::runtime_error(lowered.issues.empty() ? "Physics IR lowering failed" : lowered.issues.front().message);
    }
    std::filesystem::create_directories(output.parent_path());
    std::ofstream stream{output};
    if (!stream) throw std::runtime_error("could not write generated shader");
    stream << (backend == "glsl" ? vulkax::physics::emitScalarProgramGlsl(*lowered.program)
                                 : vulkax::physics::emitScalarProgramMsl(*lowered.program));
    std::cout << "Generated " << preset->id << " Physics IR " << backend << " at " << output
              << " (hash " << lowered.program->canonicalHash << ")\n";
  } catch (const std::exception& error) {
    std::cerr << "vulkax-equation-shader: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
