#include "vulkax/physics/stencil_ir.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  try {
    std::string backend = "glsl";
    std::string system = "diffusion";
    std::filesystem::path output;
    for (int index = 1; index < argc; ++index) {
      const std::string argument{argv[index]};
      if (argument == "--backend" && index + 1 < argc) backend = argv[++index];
      else if (argument == "--system" && index + 1 < argc) system = argv[++index];
      else if (argument == "--output" && index + 1 < argc) output = argv[++index];
      else throw std::invalid_argument(
          "usage: vulkax-stencil-shader [--backend glsl|msl] "
          "[--system diffusion|gray-scott] --output PATH");
    }
    if (output.empty() || (backend != "glsl" && backend != "msl") ||
        (system != "diffusion" && system != "gray-scott")) {
      throw std::invalid_argument("a valid system, GLSL/MSL backend, and output path are required");
    }

    vulkax::physics::PhysicsModel model{};
    model.name = "generated-" + system;
    model.domain.minimum = {0.0, 0.0, 0.0};
    model.domain.maximum = {1.0, 1.0, 1.0};
    model.domain.resolution = {16, 16, 16};
    model.solver.timestepSeconds = 0.00025;
    std::string source;
    uint64_t hash = 0;
    if (system == "diffusion") {
      model.fields = {{"u", vulkax::physics::ValueType::Scalar,
                       vulkax::physics::Dimension::dimensionless(),
                       vulkax::physics::FieldPlacement::CellCenter}};
      model.boundaries = {{"u", vulkax::physics::BoundaryKind::Periodic, std::nullopt}};
      const auto lowered = vulkax::physics::lowerScalarEvolutionProgram(
          model, "u", vulkax::equation::parseScalarExpression(
              "diffusivity * laplacian(u) - decay * u"),
          {"diffusivity", "decay"});
      if (!lowered.valid()) {
        throw std::runtime_error(lowered.issues.empty() ?
            "stencil lowering failed" : lowered.issues.front().message);
      }
      source = backend == "glsl" ?
          vulkax::physics::emitScalarEvolutionGlsl(*lowered.program) :
          vulkax::physics::emitScalarEvolutionMsl(*lowered.program);
      hash = lowered.program->canonicalHash;
    } else {
      model.fields = {
          {"a", vulkax::physics::ValueType::Scalar,
           vulkax::physics::Dimension::dimensionless(),
           vulkax::physics::FieldPlacement::CellCenter},
          {"b", vulkax::physics::ValueType::Scalar,
           vulkax::physics::Dimension::dimensionless(),
           vulkax::physics::FieldPlacement::CellCenter}};
      model.boundaries = {
          {"a", vulkax::physics::BoundaryKind::Periodic, std::nullopt},
          {"b", vulkax::physics::BoundaryKind::Periodic, std::nullopt}};
      const auto lowered = vulkax::physics::lowerCoupledScalarEvolutionProgram(
          model,
          {
              {"a", vulkax::equation::parseScalarExpression(
                        "diffusion_a * laplacian(a) - a*b*b + feed*(1-a)")},
              {"b", vulkax::equation::parseScalarExpression(
                        "diffusion_b * laplacian(b) + a*b*b - (feed+kill)*b")},
          },
          {"diffusion_a", "diffusion_b", "feed", "kill"});
      if (!lowered.valid()) {
        throw std::runtime_error(lowered.issues.empty() ?
            "coupled stencil lowering failed" : lowered.issues.front().message);
      }
      source = backend == "glsl" ?
          vulkax::physics::emitCoupledScalarEvolutionGlsl(*lowered.program) :
          vulkax::physics::emitCoupledScalarEvolutionMsl(*lowered.program);
      hash = lowered.program->canonicalHash;
    }
    std::filesystem::create_directories(output.parent_path());
    std::ofstream stream{output};
    if (!stream) throw std::runtime_error("could not write generated stencil shader");
    stream << source;
    std::cout << "Generated " << system << " stencil " << backend << " at " << output
              << " (hash " << hash << ")\n";
  } catch (const std::exception& error) {
    std::cerr << "vulkax-stencil-shader: " << error.what() << '\n';
    return 1;
  }
}
