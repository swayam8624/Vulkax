#include "vulkax/cli/captured_world_run.hpp"

#include "vulkax/backend/backend.hpp"
#include "vulkax/research/captured_world_run.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vulkax::cli {
namespace {

[[nodiscard]] double parseFiniteDouble(std::string_view text, const char* label) {
    const std::string owned(text);
    std::size_t consumed = 0U;
    double value = 0.0;
    try {
        value = std::stod(owned, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " must be numeric");
    }
    if (consumed != owned.size() || !std::isfinite(value))
        throw std::invalid_argument(std::string(label) + " must be finite");
    return value;
}

[[nodiscard]] double parsePositiveDouble(std::string_view text, const char* label) {
    const double value = parseFiniteDouble(text, label);
    if (!(value > 0.0)) throw std::invalid_argument(std::string(label) + " must be positive");
    return value;
}

[[nodiscard]] std::uint64_t parseSeed(std::string_view text) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char character) {
            return character >= '0' && character <= '9';
        }))
        throw std::invalid_argument("robustness seed must be an unsigned integer");
    const std::string owned(text);
    std::size_t consumed = 0U;
    unsigned long long value = 0U;
    try {
        value = std::stoull(owned, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument("robustness seed is outside the supported integer range");
    }
    if (consumed != owned.size()) throw std::invalid_argument("robustness seed must be an unsigned integer");
    return static_cast<std::uint64_t>(value);
}

[[nodiscard]] std::optional<backend::BackendKind> parseBackend(std::string_view text) {
    if (text == "Vulkan" || text == "vulkan") return backend::BackendKind::Vulkan;
    if (text == "Metal" || text == "metal") return backend::BackendKind::Metal;
    if (text == "OpenGL" || text == "opengl") return backend::BackendKind::OpenGL;
    return std::nullopt;
}

} // namespace

int capturedWorldRunCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "captured-world-run") return -1;
    if (argc < 9) {
        throw std::invalid_argument(
            "usage: vulkax captured-world-run <capture.vkcap> <output-dir> <marker-id> <time> "
            "<dir-x> <dir-y> <dir-z> [auto|none|Vulkan|Metal|OpenGL] "
            "[cell-size] [fd-scale-step] [rewrite-scale-delta] [robustness-seed]");
    }

    research::CapturedWorldRunSettings settings;
    settings.objectiveMarkerId = argv[4];
    settings.objectiveTime = parsePositiveDouble(argv[5], "objective time");
    settings.objectiveDirection = {
        parseFiniteDouble(argv[6], "direction x"),
        parseFiniteDouble(argv[7], "direction y"),
        parseFiniteDouble(argv[8], "direction z"),
    };

    if (argc >= 10) {
        const std::string_view backendName(argv[9]);
        if (backendName == "none") {
            settings.render = false;
        } else if (backendName != "auto") {
            settings.renderBackend = parseBackend(backendName);
            if (!settings.renderBackend) throw std::invalid_argument("unknown captured-world-run render backend");
        }
    }
    if (argc >= 11) settings.cellSize = parsePositiveDouble(argv[10], "cell size");
    if (argc >= 12) settings.finiteDifferenceScaleStep = parsePositiveDouble(argv[11], "finite-difference scale step");
    if (argc >= 13) settings.rewriteScaleDelta = parseFiniteDouble(argv[12], "rewrite scale delta");
    if (argc >= 14) settings.robustnessSeed = parseSeed(argv[13]);
    if (argc > 14) throw std::invalid_argument("captured-world-run received too many arguments");

    const auto summary = research::runCapturedWorldResearchDemo(
        std::filesystem::path(argv[2]), std::filesystem::path(argv[3]), settings);

    std::cout << std::setprecision(12)
              << "Vulkax captured-world-run\n"
              << "  bundle: " << summary.bundleId << '\n'
              << "  source_kind: " << capture::toString(summary.sourceKind) << '\n'
              << "  selected_young_modulus: " << summary.selectedYoungModulus << '\n'
              << "  selected_poisson_ratio: " << summary.selectedPoissonRatio << '\n'
              << "  validation_dynamic_rms: " << summary.validationDynamicRms << '\n'
              << "  robustness_scenarios: " << summary.robustnessScenarioCount << '\n'
              << "  adaptive_regions: " << summary.adaptiveRegionCount << '\n'
              << "  adaptive_particles: " << summary.adaptiveParticleCount << '\n'
              << "  adaptive_abs_gradient_fraction: " << summary.adaptiveAbsoluteGradientFraction << '\n'
              << "  rewrite_region: " << summary.rewriteRegionId << '\n'
              << "  rewrite_particles: " << summary.rewriteParticleCount << '\n'
              << "  rewrite_status: " << world::toString(summary.rewriteStatus) << '\n'
              << "  physical_error: " << summary.physicalObservableError << '\n'
              << "  physical_tolerance: " << summary.physicalObservableTolerance << '\n'
              << "  rollback_performed: " << (summary.rollbackPerformed ? "yes" : "no") << '\n'
              << "  render_produced: " << (summary.renderProduced ? "yes" : "no") << '\n';
    if (summary.renderBackend)
        std::cout << "  render_backend: " << backend::toString(*summary.renderBackend) << '\n'
                  << "  render_rmse: " << summary.renderComparison.rootMeanSquareError << '\n';
    std::cout << "  artifacts_indexed: " << summary.artifactCount << '\n'
              << "  certificate: " << (std::filesystem::path(argv[3]) / "certificate.json").string() << '\n';
    return 0;
}

} // namespace vulkax::cli
