#include "vulkax/cli/captured_world_run.hpp"

#include "vulkax/backend/backend.hpp"
#include "vulkax/research/captured_world_run.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vulkax::cli {
namespace {

constexpr std::string_view kCapturedWorldRunUsage =
    "usage: vulkax captured-world-run <capture.vkcap> <output-dir> <marker-id> <time> "
    "<dir-x> <dir-y> <dir-z> [auto|none|Vulkan|Metal|OpenGL] "
    "[cell-size] [fd-scale-step] [rewrite-scale-delta] [robustness-seed] "
    "[--showcase studio_pedestal|cloth_showcase] [--showcase-assets <dir>] "
    "[--showcase-asset-lock <file>] [--showcase-resolution WIDTHxHEIGHT] "
    "[--turntable N] [--no-closeup] [--no-summary-card]";

void printCapturedWorldRunHelp() {
    std::cout << kCapturedWorldRunUsage << "\n\n"
              << "Runs the complete captured-world research path and writes a versioned evidence bundle.\n"
              << "A completed run may contain either a verified rewrite or a rejected rewrite with rollback.\n"
              << "Use backend 'none' for research evidence without native rendering. Showcase output requires rendering.\n";
}

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

[[nodiscard]] std::uint64_t parseUnsigned(std::string_view text, const char* label) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char character) {
            return character >= '0' && character <= '9';
        }))
        throw std::invalid_argument(std::string(label) + " must be an unsigned integer");
    const std::string owned(text);
    std::size_t consumed = 0U;
    unsigned long long value = 0U;
    try {
        value = std::stoull(owned, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " is outside the supported integer range");
    }
    if (consumed != owned.size())
        throw std::invalid_argument(std::string(label) + " must be an unsigned integer");
    return static_cast<std::uint64_t>(value);
}

[[nodiscard]] std::optional<backend::BackendKind> parseBackend(std::string_view text) {
    if (text == "Vulkan" || text == "vulkan") return backend::BackendKind::Vulkan;
    if (text == "Metal" || text == "metal") return backend::BackendKind::Metal;
    if (text == "OpenGL" || text == "opengl") return backend::BackendKind::OpenGL;
    return std::nullopt;
}

[[nodiscard]] bool isFlag(std::string_view text) {
    return text.starts_with("--");
}

void parseResolution(
    std::string_view text,
    std::uint32_t& width,
    std::uint32_t& height) {
    const auto separator = text.find('x');
    if (separator == std::string_view::npos || text.find('x', separator + 1U) != std::string_view::npos)
        throw std::invalid_argument("showcase resolution must be WIDTHxHEIGHT");
    const auto widthValue = parseUnsigned(text.substr(0, separator), "showcase width");
    const auto heightValue = parseUnsigned(text.substr(separator + 1U), "showcase height");
    if (widthValue == 0U || heightValue == 0U || widthValue > 8192U || heightValue > 8192U)
        throw std::invalid_argument("showcase dimensions must lie in [1,8192]");
    width = static_cast<std::uint32_t>(widthValue);
    height = static_cast<std::uint32_t>(heightValue);
}

} // namespace

int capturedWorldRunCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "captured-world-run") return -1;
    if (argc == 3 && (std::string_view(argv[2]) == "--help" || std::string_view(argv[2]) == "-h")) {
        printCapturedWorldRunHelp();
        return 0;
    }
    if (argc < 9) throw std::invalid_argument(std::string(kCapturedWorldRunUsage));

    research::CapturedWorldRunSettings settings;
    settings.objectiveMarkerId = argv[4];
    settings.objectiveTime = parsePositiveDouble(argv[5], "objective time");
    settings.objectiveDirection = {
        parseFiniteDouble(argv[6], "direction x"),
        parseFiniteDouble(argv[7], "direction y"),
        parseFiniteDouble(argv[8], "direction z"),
    };
    const double directionNormSquared =
        settings.objectiveDirection.x * settings.objectiveDirection.x +
        settings.objectiveDirection.y * settings.objectiveDirection.y +
        settings.objectiveDirection.z * settings.objectiveDirection.z;
    if (!(directionNormSquared > 0.0) || !std::isfinite(directionNormSquared))
        throw std::invalid_argument("objective direction must be finite and non-zero");

    int cursor = 9;
    if (cursor < argc && !isFlag(argv[cursor])) {
        const std::string_view backendName(argv[cursor++]);
        if (backendName == "none") {
            settings.render = false;
        } else if (backendName != "auto") {
            settings.renderBackend = parseBackend(backendName);
            if (!settings.renderBackend) throw std::invalid_argument("unknown captured-world-run render backend");
        }
    }
    if (cursor < argc && !isFlag(argv[cursor]))
        settings.cellSize = parsePositiveDouble(argv[cursor++], "cell size");
    if (cursor < argc && !isFlag(argv[cursor]))
        settings.finiteDifferenceScaleStep = parsePositiveDouble(argv[cursor++], "finite-difference scale step");
    if (cursor < argc && !isFlag(argv[cursor]))
        settings.rewriteScaleDelta = parseFiniteDouble(argv[cursor++], "rewrite scale delta");
    if (cursor < argc && !isFlag(argv[cursor]))
        settings.robustnessSeed = parseUnsigned(argv[cursor++], "robustness seed");

    bool showcaseSpecificOptionSeen = false;
    while (cursor < argc) {
        const std::string_view flag(argv[cursor++]);
        const auto requireValue = [&](const char* label) -> std::string_view {
            if (cursor >= argc) throw std::invalid_argument(std::string(flag) + " requires " + label);
            return argv[cursor++];
        };
        if (flag == "--showcase") {
            settings.showcase.enabled = true;
            settings.showcase.scenePreset = std::string(requireValue("a scene preset"));
            if (settings.showcase.scenePreset != "studio_pedestal" && settings.showcase.scenePreset != "cloth_showcase")
                throw std::invalid_argument("showcase scene preset must be studio_pedestal or cloth_showcase");
        } else if (flag == "--showcase-assets") {
            showcaseSpecificOptionSeen = true;
            settings.showcase.assetRoot = std::filesystem::path(requireValue("an asset directory"));
        } else if (flag == "--showcase-asset-lock") {
            showcaseSpecificOptionSeen = true;
            settings.showcase.assetLock = std::filesystem::path(requireValue("an asset lock file"));
        } else if (flag == "--showcase-resolution") {
            showcaseSpecificOptionSeen = true;
            parseResolution(
                requireValue("WIDTHxHEIGHT"),
                settings.showcase.width,
                settings.showcase.height);
        } else if (flag == "--turntable") {
            showcaseSpecificOptionSeen = true;
            const auto frames = parseUnsigned(requireValue("a frame count"), "turntable frame count");
            if (frames == 0U || frames > 72U)
                throw std::invalid_argument("turntable frame count must lie in [1,72]");
            settings.showcase.turntableFrames = static_cast<std::uint32_t>(frames);
        } else if (flag == "--no-closeup") {
            showcaseSpecificOptionSeen = true;
            settings.showcase.closeup = false;
        } else if (flag == "--no-summary-card") {
            showcaseSpecificOptionSeen = true;
            settings.showcase.summaryCard = false;
        } else {
            throw std::invalid_argument("unknown captured-world-run option: " + std::string(flag));
        }
    }

    if (showcaseSpecificOptionSeen && !settings.showcase.enabled)
        throw std::invalid_argument("showcase-specific options require --showcase <preset>");
    if (settings.showcase.enabled && !settings.render)
        throw std::invalid_argument("showcase output requires a native render backend; backend 'none' is incompatible with --showcase");

    const auto summary = research::runCapturedWorldResearchDemo(
        std::filesystem::path(argv[2]), std::filesystem::path(argv[3]), settings);

    std::cout << std::setprecision(12)
              << "Vulkax captured-world-run\n"
              << "  run_status: completed\n"
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
    std::cout << "  showcase_produced: " << (summary.showcaseProduced ? "yes" : "no") << '\n';
    if (summary.showcaseProduced)
        std::cout << "  showcase_scene: " << summary.showcaseScenePreset << '\n'
                  << "  showcase_turntable_frames: " << summary.showcaseTurntableFrames << '\n';
    std::cout << "  artifacts_indexed: " << summary.artifactCount << '\n'
              << "  certificate: " << (std::filesystem::path(argv[3]) / "certificate.json").string() << '\n';
    return 0;
}

} // namespace vulkax::cli
