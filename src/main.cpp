
#include "first_app.hpp"
#include "beacon/benchmark_runner.hpp"
#include "runtime_paths.hpp"

// std
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

bool usesHeadlessResearchRunner(lve::beacon::RenderTechnique technique) {
  using lve::beacon::RenderTechnique;
  return technique == RenderTechnique::CpuClusteredFixed ||
         technique == RenderTechnique::FixedClusterCostModel;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    auto config = lve::beacon::parseCommandLine(argc, argv);
    const auto executableName = std::filesystem::path{argv[0]}.stem();
    lve::beacon::applyExecutableDefaults(config, executableName.string());
    if (config.geoEnabled) {
      config.geoManifest =
          lve::resolveRuntimeResource(config.geoManifest);
      config.geoNavigationData =
          lve::resolveRuntimeResource(config.geoNavigationData);
      config.geoCityRegistry =
          lve::resolveRuntimeResource(config.geoCityRegistry);
    }
    if (config.atlasEnabled) {
      config.atlasManifest =
          lve::resolveRuntimeResource(config.atlasManifest);
      config.atlasPack = lve::resolveRuntimeResource(config.atlasPack);
      config.atlasNavigationReplay =
          lve::resolveRuntimeResource(config.atlasNavigationReplay);
    }
    if (config.benchmark && usesHeadlessResearchRunner(config.technique)) {
      return lve::beacon::runResearchBenchmark(config);
    }
    lve::FirstApp app{config};
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
