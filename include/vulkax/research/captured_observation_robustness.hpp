#pragma once

#include "vulkax/research/adaptive_material_influence.hpp"
#include "vulkax/research/captured_material_calibration.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vulkax::research {

struct CapturedObservationNoiseScenario {
    std::string id;
    // Component-wise RMS scale of deterministic bounded zero-mean noise applied
    // to observations at t=0. These rows drive captured-pose initialization.
    double initialPositionNoiseRms{};
    // Component-wise RMS scale applied to all observations with t>0. Fit rows
    // affect material selection; validation rows remain held out from ranking.
    double dynamicPositionNoiseRms{};
    std::uint64_t seed{1U};
};

struct CapturedObservationRobustnessSample {
    std::string scenarioId;
    double initialPositionNoiseRms{};
    double dynamicPositionNoiseRms{};
    std::uint64_t seed{};

    double selectedYoungModulus{};
    double selectedPoissonRatio{};
    double youngModulusRelativeDeltaFromBaseline{};
    double poissonRatioAbsoluteDeltaFromBaseline{};

    double fitDynamicRms{};
    double validationDynamicRms{};
    double initializationFitRms{};
    double appearanceRoundtripRms{};

    double particleInfluenceCosineSimilarity{1.0};
    double particleInfluenceRelativeL2Error{};
    std::uint64_t strongestParticleId{};
    bool strongestParticleMatchesBaseline{true};
    double minimumStencilKnotMargin{};

    std::size_t adaptiveRegionCount{};
    std::size_t adaptiveParticleCount{};
    double adaptiveAbsoluteGradientFraction{};
    double adaptiveParticleJaccardWithBaseline{1.0};
};

struct CapturedObservationRobustnessResult {
    CapturedObservationRobustnessSample baseline;
    std::vector<CapturedObservationRobustnessSample> scenarios;
};

// Apply deterministic, platform-stable bounded zero-mean component noise. The
// mapping is hash based rather than std::normal_distribution based so the same
// seed/scenario is reproducible across standard-library implementations.
[[nodiscard]] capture::CapturedDeformableDataset perturbCapturedObservations(
    const capture::CapturedDeformableDataset& dataset,
    const CapturedObservationNoiseScenario& scenario);

// Stress material identification and the downstream particle/adaptive influence
// field against observation uncertainty. The clean dataset is always evaluated
// first and serves only as a controlled regression baseline. Candidate ranking
// still uses nonzero-time fit rows only; validation rows remain held out.
[[nodiscard]] CapturedObservationRobustnessResult evaluateCapturedObservationRobustness(
    const gaussian::GaussianCloud& world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& cleanDataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    const std::vector<double>& youngModulusCandidates,
    const std::vector<double>& poissonRatioCandidates,
    const CapturedMaterialInfluenceSettings& influenceSettings,
    const CapturedMaterialAdaptiveRegionSettings& adaptiveSettings,
    const std::vector<CapturedObservationNoiseScenario>& scenarios);

void writeCapturedObservationRobustnessCsv(
    const CapturedObservationRobustnessResult& result,
    const std::filesystem::path& path);

} // namespace vulkax::research
