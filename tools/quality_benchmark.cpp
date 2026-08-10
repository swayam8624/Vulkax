#include "vulkax/equation/equation.hpp"
#include "vulkax/research/quality_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Options {
  uint32_t frames = 120;
  uint32_t width = 320;
  uint32_t height = 180;
  double targetMilliseconds = 4.0;
  std::filesystem::path output = "docs/results/physics_studio_current/quality_controller";
};

Options parseOptions(int argc, char** argv) {
  Options options{};
  for (int index = 1; index < argc; ++index) {
    const std::string argument(argv[index]);
    if (argument == "--frames" && index + 1 < argc) options.frames = std::stoul(argv[++index]);
    else if (argument == "--width" && index + 1 < argc) options.width = std::stoul(argv[++index]);
    else if (argument == "--height" && index + 1 < argc) options.height = std::stoul(argv[++index]);
    else if (argument == "--target-ms" && index + 1 < argc) options.targetMilliseconds = std::stod(argv[++index]);
    else if (argument == "--output" && index + 1 < argc) options.output = argv[++index];
    else throw std::invalid_argument("usage: vulkax-quality-benchmark [--frames N --width N --height N --target-ms N --output PATH]");
  }
  if (options.frames == 0 || options.width < 8 || options.height < 8 || options.targetMilliseconds <= 0.0) {
    throw std::invalid_argument("quality benchmark options must be positive");
  }
  return options;
}

std::vector<double> evaluateField(
    const vulkax::equation::ScalarExpression& expression,
    const std::map<std::string, double>& parameters,
    uint32_t width,
    uint32_t height,
    double timeSeconds) {
  std::unordered_map<std::string, double> variables(parameters.begin(), parameters.end());
  variables["t"] = timeSeconds;
  variables["z"] = 0.0;
  std::vector<double> result(static_cast<size_t>(width) * height);
  for (uint32_t row = 0; row < height; ++row) {
    variables["y"] = (static_cast<double>(row) / (height - 1)) * 8.0 - 4.0;
    for (uint32_t column = 0; column < width; ++column) {
      variables["x"] = (static_cast<double>(column) / (width - 1)) * 8.0 - 4.0;
      result[static_cast<size_t>(row) * width + column] = expression.evaluate(variables);
    }
  }
  return result;
}

double resampleMse(
    const std::vector<double>& low,
    uint32_t lowWidth,
    uint32_t lowHeight,
    const std::vector<double>& reference,
    uint32_t referenceWidth,
    uint32_t referenceHeight) {
  double mse = 0.0;
  for (uint32_t row = 0; row < referenceHeight; ++row) {
    const uint32_t sampleY = std::min(lowHeight - 1, static_cast<uint32_t>(
        (static_cast<uint64_t>(row) * lowHeight) / referenceHeight));
    for (uint32_t column = 0; column < referenceWidth; ++column) {
      const uint32_t sampleX = std::min(lowWidth - 1, static_cast<uint32_t>(
          (static_cast<uint64_t>(column) * lowWidth) / referenceWidth));
      const double error = low[static_cast<size_t>(sampleY) * lowWidth + sampleX] -
          reference[static_cast<size_t>(row) * referenceWidth + column];
      mse += error * error;
    }
  }
  return mse / static_cast<double>(reference.size());
}

double percentile(std::vector<double> values, double fraction) {
  std::sort(values.begin(), values.end());
  const size_t index = std::min(values.size() - 1, static_cast<size_t>(
      std::floor(fraction * static_cast<double>(values.size() - 1))));
  return values[index];
}

struct PolicySummary {
  std::vector<double> frameMilliseconds;
  std::vector<double> numericalErrors;
  std::vector<double> visualErrors;
  uint32_t changes = 0;
};

PolicySummary runPolicy(
    const std::string& policyName,
    vulkax::research::QualityPolicy policy,
    const Options& options,
    const vulkax::equation::ScalarExpression& expression,
    const std::map<std::string, double>& parameters,
    std::ofstream& csv) {
  using Clock = std::chrono::steady_clock;
  vulkax::research::QualityBudget budget{
      options.targetMilliseconds, 0.05, 0.35, 1.0, 1, 8};
  budget.targetNumericalError = 0.005;
  vulkax::research::QualityController controller{budget, policy};
  controller.reset({1.0, 1, 1});
  PolicySummary summary{};
  for (uint32_t frame = 0; frame < options.frames; ++frame) {
    const auto state = controller.state();
    const bool fixed = policy == vulkax::research::QualityPolicy::Fixed;
    const double scale = fixed ? 1.0 : state.resolutionScale;
    const uint32_t width = std::max(
        8u, static_cast<uint32_t>(std::lround(options.width * scale)));
    const uint32_t height = std::max(
        8u, static_cast<uint32_t>(std::lround(options.height * scale)));
    const double timeSeconds = static_cast<double>(frame) / 60.0;
    const auto started = Clock::now();
    const auto field = evaluateField(expression, parameters, width, height, timeSeconds);
    const double frameMilliseconds =
        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    const auto reference =
        evaluateField(expression, parameters, options.width, options.height, timeSeconds);
    const double numericalMse =
        resampleMse(field, width, height, reference, options.width, options.height);
    // This CPU benchmark has no rendered perceptual image. RMS field error is
    // therefore recorded explicitly as a screen-space surrogate, never as a
    // perceptual metric.
    const double screenSpaceRms = std::sqrt(numericalMse);

    using vulkax::research::QualityMeasurements;
    using vulkax::research::QualityPolicy;
    QualityMeasurements measurements{};
    measurements.frameMilliseconds = frameMilliseconds;
    if (policy == QualityPolicy::NumericalOnly || policy == QualityPolicy::EquationAware)
      measurements.numericalError = numericalMse;
    if (policy == QualityPolicy::ScreenSpaceOnly || policy == QualityPolicy::EquationAware)
      measurements.visualError = screenSpaceRms;
    controller.update(measurements);

    csv << policyName << ',' << frame << ',' << std::setprecision(17)
        << frameMilliseconds << ',' << numericalMse << ',' << screenSpaceRms << ','
        << scale << ',' << state.samplesPerPixel << ',' << state.simulationSubsteps << ','
        << width << ',' << height << ',' << controller.frameTimeEwma() << ','
        << controller.numericalErrorEwma().value_or(
               std::numeric_limits<double>::quiet_NaN()) << ','
        << controller.visualErrorEwma().value_or(
               std::numeric_limits<double>::quiet_NaN()) << '\n';
    summary.frameMilliseconds.push_back(frameMilliseconds);
    summary.numericalErrors.push_back(numericalMse);
    summary.visualErrors.push_back(screenSpaceRms);
  }
  summary.changes = controller.changeCount();
  return summary;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parseOptions(argc, argv);
    const auto preset = vulkax::equation::findPreset("quantum-wavepacket");
    if (!preset) throw std::runtime_error("quantum-wavepacket preset unavailable");
    const auto expression = vulkax::equation::parseScalarExpression(preset->expressions.front());
    std::map<std::string, double> parameters;
    for (const auto& parameter : preset->parameters) parameters[parameter.name] = parameter.value;
    std::filesystem::create_directories(options.output);
    std::ofstream csv(options.output / "quality_frames.csv");
    if (!csv) throw std::runtime_error("could not write quality CSV");
    csv << "policy,frame,frame_ms,numerical_mse,screen_space_rms,resolution_scale,"
           "samples_per_pixel,simulation_substeps,width,height,frame_ewma_ms,"
           "numerical_ewma,visual_ewma\n";
    using vulkax::research::QualityPolicy;
    const auto fixed = runPolicy(
        "fixed", QualityPolicy::Fixed, options, expression, parameters, csv);
    const auto screen = runPolicy(
        "screen_space_only", QualityPolicy::ScreenSpaceOnly,
        options, expression, parameters, csv);
    const auto numerical = runPolicy(
        "numerical_only", QualityPolicy::NumericalOnly,
        options, expression, parameters, csv);
    const auto equationAware = runPolicy(
        "equation_aware", QualityPolicy::EquationAware,
        options, expression, parameters, csv);

    const auto mean = [](const std::vector<double>& values) {
      return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    };
    const auto writePolicy = [&](std::ofstream& out, const char* name,
                                 const PolicySummary& value, bool trailingComma) {
      out << "  \"" << name << "\": {"
          << "\"p50_frame_ms\": " << percentile(value.frameMilliseconds, 0.50)
          << ", \"p95_frame_ms\": " << percentile(value.frameMilliseconds, 0.95)
          << ", \"mean_numerical_mse\": " << mean(value.numericalErrors)
          << ", \"mean_screen_space_rms\": " << mean(value.visualErrors)
          << ", \"changes\": " << value.changes << "}"
          << (trailingComma ? ",\n" : "\n");
    };
    std::ofstream summary(options.output / "quality_summary.json");
    summary << "{\n"
            << "  \"measurement_class\": "
               "\"cpu_analytical_preview_policy_comparison\",\n"
            << "  \"visual_metric_note\": "
               "\"screen_space_rms is a field-space surrogate, not perceptual image error\",\n"
            << "  \"frames\": " << options.frames << ",\n"
            << "  \"target_frame_ms\": " << options.targetMilliseconds << ",\n";
    writePolicy(summary, "fixed", fixed, true);
    writePolicy(summary, "screen_space_only", screen, true);
    writePolicy(summary, "numerical_only", numerical, true);
    writePolicy(summary, "equation_aware", equationAware, false);
    summary << "}\n";
    std::cout << "Wrote " << (options.output / "quality_frames.csv") << " and "
              << (options.output / "quality_summary.json") << '\n';
  } catch (const std::exception& error) {
    std::cerr << "vulkax-quality-benchmark: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
