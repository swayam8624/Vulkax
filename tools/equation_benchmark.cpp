#include "vulkax/equation/equation.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

struct Options {
  std::filesystem::path output = "docs/results/physics_studio_current";
  uint32_t frames = 180;
  uint32_t samples = 512;
  double timestep = 1.0 / 60.0;
};

Options parseOptions(int argc, char** argv) {
  Options options{};
  for (int index = 1; index < argc; ++index) {
    const std::string argument{argv[index]};
    auto value = [&]() -> std::string {
      if (index + 1 >= argc) throw std::invalid_argument("missing value for " + argument);
      return argv[++index];
    };
    if (argument == "--output") options.output = value();
    else if (argument == "--frames") options.frames = static_cast<uint32_t>(std::stoul(value()));
    else if (argument == "--samples") options.samples = static_cast<uint32_t>(std::stoul(value()));
    else if (argument == "--timestep") options.timestep = std::stod(value());
    else if (argument == "--help") {
      std::cout << "vulkax-equations [--output PATH] [--frames N] [--samples N] [--timestep SECONDS]\n";
      std::exit(0);
    } else throw std::invalid_argument("unknown option: " + argument);
  }
  return options;
}

void writeJsonString(std::ostream& stream, const std::string& value) {
  stream << '"';
  for (const char character : value) {
    if (character == '"' || character == '\\') stream << '\\';
    stream << character;
  }
  stream << '"';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parseOptions(argc, argv);
    std::filesystem::create_directories(options.output);
    std::ofstream csv{options.output / "preset_runs.csv"};
    std::ofstream json{options.output / "summary.json"};
    if (!csv || !json) throw std::runtime_error("could not open physics result files");
    csv << "preset_id,frames,samples_per_frame,min_value,max_value,mean_absolute_value,energy_proxy\n";
    json << "{\n  \"measurement_class\": \"cpu_analytic_reference\",\n  \"frames\": " << options.frames
         << ",\n  \"samples_per_frame\": " << options.samples << ",\n  \"timestep_seconds\": "
         << std::setprecision(17) << options.timestep << ",\n  \"presets\": [\n";
    const auto presets = vulkax::equation::builtInPresets();
    for (size_t index = 0; index < presets.size(); ++index) {
      const auto& preset = presets[index];
      const auto summary = vulkax::equation::runPreset(
          preset, {options.frames, options.samples, options.timestep});
      csv << summary.presetId << ',' << summary.frames << ',' << summary.samplesPerFrame << ','
          << std::setprecision(17) << summary.minimumValue << ',' << summary.maximumValue << ','
          << summary.meanAbsoluteValue << ',' << summary.energyProxy << '\n';
      json << "    {\"id\": ";
      writeJsonString(json, preset.id);
      json << ", \"display_name\": ";
      writeJsonString(json, preset.displayName);
      json << ", \"minimum_value\": " << summary.minimumValue
           << ", \"maximum_value\": " << summary.maximumValue
           << ", \"mean_absolute_value\": " << summary.meanAbsoluteValue
           << ", \"energy_proxy\": " << summary.energyProxy << '}';
      if (index + 1 != presets.size()) json << ',';
      json << '\n';
      std::cout << "[" << (index + 1) << '/' << presets.size() << "] " << preset.displayName
                << "  energy=" << std::fixed << std::setprecision(6) << summary.energyProxy << '\n';
    }
    json << "  ]\n}\n";
    std::cout << "Wrote " << (options.output / "preset_runs.csv") << " and " << (options.output / "summary.json") << '\n';
  } catch (const std::exception& error) {
    std::cerr << "vulkax-equations: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
