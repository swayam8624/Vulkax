#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/capture/deformable_bundle.hpp"
#include "vulkax/render/image_metrics.hpp"
#include "vulkax/research/adaptive_material_influence.hpp"
#include "vulkax/world/verified_rewrite.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vulkax::research {

struct CapturedWorldRunSettings {
    std::string objectiveMarkerId;
    double objectiveTime{};
    math::Vec3 objectiveDirection{1.0, 1.0, 1.0};

    std::optional<double> cellSize;
    double finiteDifferenceScaleStep{0.01};
    double rewriteScaleDelta{0.02};
    std::uint64_t robustnessSeed{12345U};

    std::vector<double> youngModulusCandidates{
        5.0e3, 7.5e3, 1.0e4, 1.5e4, 2.2e4, 3.3e4, 5.0e4,
    };
    std::vector<double> poissonRatioCandidates{0.20, 0.30, 0.40, 0.45};
    CapturedMaterialAdaptiveRegionSettings adaptiveSettings{};

    bool render{true};
    std::optional<backend::BackendKind> renderBackend;
    std::uint32_t renderWidth{1280U};
    std::uint32_t renderHeight{720U};
};

struct CapturedWorldRunSummary {
    std::string bundleId;
    capture::CapturedSourceKind sourceKind{capture::CapturedSourceKind::Synthetic};
    std::size_t appearanceGaussians{};
    std::size_t physicalParticles{};
    std::size_t observations{};

    double selectedYoungModulus{};
    double selectedPoissonRatio{};
    double fitDynamicRms{};
    double validationDynamicRms{};

    std::size_t robustnessScenarioCount{};
    std::size_t adaptiveRegionCount{};
    std::size_t adaptiveParticleCount{};
    double adaptiveAbsoluteGradientFraction{};

    std::string rewriteRegionId;
    std::size_t rewriteParticleCount{};
    world::RewriteVerificationStatus rewriteStatus{world::RewriteVerificationStatus::Rejected};
    bool rollbackPerformed{};
    double physicalObservableError{};
    double physicalObservableTolerance{};

    bool renderProduced{};
    std::optional<backend::BackendKind> renderBackend;
    render::ImageComparison renderComparison{};
    std::size_t artifactCount{};
};

// Executes the complete controlled captured-world research path through library
// APIs rather than shelling out to existing CLI commands. The output directory
// must be absent or empty so the result certificate can index exactly the
// artifacts produced by this run.
[[nodiscard]] CapturedWorldRunSummary runCapturedWorldResearchDemo(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& outputDirectory,
    const CapturedWorldRunSettings& settings);

} // namespace vulkax::research
