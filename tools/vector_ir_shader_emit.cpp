#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "vulkax/equation/equation.hpp"
#include "vulkax/physics/vector_compute_ir.hpp"

int main(int argc, char** argv) {
  try {
    std::filesystem::path glslOutput;
    std::filesystem::path mslOutput;
    for (int index = 1; index < argc; ++index) {
      const std::string option{argv[index]};
      if (option == "--glsl" && index + 1 < argc) glslOutput = argv[++index];
      else if (option == "--msl" && index + 1 < argc) mslOutput = argv[++index];
      else throw std::invalid_argument("usage: vulkax-vector-shader --glsl PATH --msl PATH");
    }
    if (glslOutput.empty() || mslOutput.empty()) {
      throw std::invalid_argument("both --glsl and --msl are required");
    }

    vulkax::physics::PhysicsModel model{};
    model.name = "vector-ir-emission";
    model.domain.minimum = {-2.0, -2.0, -2.0};
    model.domain.maximum = {2.0, 2.0, 2.0};
    model.domain.resolution = {64, 64, 1};
    model.fields.push_back({
        "velocity",
        vulkax::physics::ValueType::Vector3,
        vulkax::physics::Dimension::dimensionless(),
        vulkax::physics::FieldPlacement::CellCenter});
    const std::vector<vulkax::equation::ScalarExpression> components{
        vulkax::equation::parseScalarExpression("-y"),
        vulkax::equation::parseScalarExpression("x"),
        vulkax::equation::parseScalarExpression("gain*sin(t)")};
    const auto lowered = vulkax::physics::lowerVectorFieldProgram(
        model, "velocity", components, {"gain"});
    if (!lowered.valid()) throw std::runtime_error("vector IR lowering failed");

    std::filesystem::create_directories(glslOutput.parent_path());
    std::filesystem::create_directories(mslOutput.parent_path());
    std::ofstream(glslOutput) << vulkax::physics::emitVectorProgramGlsl(*lowered.program);
    std::ofstream(mslOutput) << vulkax::physics::emitVectorProgramMsl(*lowered.program);
    std::cout << "vector IR hash=" << lowered.program->canonicalHash << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-vector-shader: " << error.what() << '\n';
    return 1;
  }
}
