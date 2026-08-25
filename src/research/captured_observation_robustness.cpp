#include "vulkax/research/captured_observation_robustness.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vulkax::research {
namespace {

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] double deterministicUnitVarianceNoise(
    std::uint64_t seed,
    std::size_t observationIndex,
    std::size_t axis) noexcept {
    std::uint64_t key = seed;
    key ^= 0x9e3779b97f4a7c15ULL * static_cast<std::uint64_t>(observationIndex + 1U);
    key ^= 0xbf58476d1ce4e5b9ULL * static_cast<std::uint64_t>(axis + 1U);
    const std::uint64_t hash = splitmix64(key);
    // Convert the top 53 bits exactly to [0, 1), then map to a uniform
    // distribution on [-sqrt(3), sqrt(3)], whose population variance is 1.
    constexpr double inverse53 = 1.0 / 9007199254740992.0;
    const double uniform01 = static_cast<double>(hash >> 11U) * inverse53;
    return (2.0 * uniform01 - 1.0) * 1.7320508075688772935;
}

void validateScenario(const CapturedObservationNoiseScenario& scenario) {
    if (scenario.id.empty())
        throw std::invalid_argument("captured observation robustness scenario ID is empty");
    if (!std::isfinite(scenario.initialPositionNoiseRms) || scenario.initialPositionNoiseRms < 0.0 ||
        !std::isfinite(scenario.dynamicPositionNoiseRms) || scenario.dynamicPositionNoiseRms < 0.0)
        throw std::invalid_argument("captured observation robustness noise scales must be finite and non-negative");
}

[[nodiscard]] CapturedMaterialInfluenceRegion allParticleRegion(
    const capture::CapturedDeformableDataset& dataset) {
    CapturedMaterialInfluenceRegion region;
    region.id = "all_particles";
    region.particleIds.reserve(dataset.particles.size());
    for (const auto& particle : dataset.particles) region.particleIds.push_back(particle.particleId);
    return region;
}

[[nodiscard]] std::uint64_t strongestParticleId(
    const CapturedMaterialAdjointInfluenceResult& adjoint) {
    if (adjoint.particleIds.empty() || adjoint.particleIds.size() != adjoint.particleScaleGradient.size())
        throw std::invalid_argument("captured observation robustness adjoint field is malformed");
    std::size_t best = 0U;
    for (std::size_t index = 1; index < adjoint.particleIds.size(); ++index) {
        const double candidate = std::abs(adjoint.particleScaleGradient[index]);
        const double current = std::abs(adjoint.particleScaleGradient[best]);
        if (candidate > current ||
            (candidate == current && adjoint.particleIds[index] < adjoint.particleIds[best]))
            best = index;
    }
    return adjoint.particleIds[best];
}

[[nodiscard]] std::unordered_set<std::uint64_t> proposedParticleIds(
    const CapturedMaterialAdaptiveRegionProposal& proposal) {
    std::unordered_set<std::uint64_t> result;
    for (const auto& region : proposal.regions)
        for (const auto id : region.particleIds)
            if (!result.insert(id).second)
                throw std::runtime_error("captured observation robustness adaptive proposal overlaps itself");
    return result;
}

[[nodiscard]] double jaccard(
    const std::unordered_set<std::uint64_t>& lhs,
    const std::unordered_set<std::uint64_t>& rhs) noexcept {
    if (lhs.empty() && rhs.empty()) return 1.0;
    std::size_t intersection = 0U;
    for (const auto id : lhs)
        if (rhs.contains(id)) ++intersection;
    const std::size_t unionCount = lhs.size() + rhs.size() - intersection;
    return unionCount == 0U ? 1.0 : static_cast<double>(intersection) / static_cast<double>(unionCount);
}

struct EvaluatedScenario {
    CapturedObservationRobustnessSample sample;
    CapturedMaterialAdjointInfluenceResult adjoint;
    CapturedMaterialAdaptiveRegionProposal proposal;
};

[[nodiscard]] EvaluatedScenario evaluateScenario(
    const gaussian::GaussianCloud& world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& dataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    const std::vector<double>& youngModulusCandidates,
    const std::vector<double>& poissonRatioCandidates,
    const CapturedMaterialInfluenceSettings& influenceSettings,
    const CapturedMaterialAdaptiveRegionSettings& adaptiveSettings,
    const std::string& scenarioId,
    double initialNoise,
    double dynamicNoise,
    std::uint64_t seed) {
    const auto calibration = calibrateCapturedMaterialGrid(
        world,
        activeGaussianIndices,
        dataset,
        grid,
        settings,
        youngModulusCandidates,
        poissonRatioCandidates);
    if (calibration.selectedIndex >= calibration.candidates.size())
        throw std::runtime_error("captured observation robustness calibration selected index is invalid");
    const auto& selected = calibration.candidates[calibration.selectedIndex];

    settings.material.youngModulus = selected.youngModulus;
    settings.material.poissonRatio = selected.poissonRatio;
    const std::vector<CapturedMaterialInfluenceRegion> regions{allParticleRegion(dataset)};
    auto adjoint = computeCapturedMaterialInfluenceAdjoint(
        world,
        activeGaussianIndices,
        dataset,
        grid,
        settings,
        regions,
        influenceSettings);
    auto proposal = proposeCapturedMaterialInfluenceRegions(dataset, adjoint, adaptiveSettings);

    CapturedObservationRobustnessSample sample;
    sample.scenarioId = scenarioId;
    sample.initialPositionNoiseRms = initialNoise;
    sample.dynamicPositionNoiseRms = dynamicNoise;
    sample.seed = seed;
    sample.selectedYoungModulus = selected.youngModulus;
    sample.selectedPoissonRatio = selected.poissonRatio;
    sample.fitDynamicRms = selected.fitDynamicRms;
    sample.validationDynamicRms = selected.validationDynamicRms;
    sample.initializationFitRms = selected.initializationFitRms;
    sample.appearanceRoundtripRms = selected.appearanceRoundtripRms;
    sample.strongestParticleId = strongestParticleId(adjoint);
    sample.minimumStencilKnotMargin = adjoint.minimumStencilKnotMargin;
    sample.adaptiveRegionCount = proposal.regions.size();
    sample.adaptiveParticleCount = proposal.proposedParticleCount;
    sample.adaptiveAbsoluteGradientFraction = proposal.proposedAbsoluteGradientFraction;
    return {std::move(sample), std::move(adjoint), std::move(proposal)};
}

void compareInfluence(
    CapturedObservationRobustnessSample& sample,
    const CapturedMaterialAdjointInfluenceResult& baseline,
    const CapturedMaterialAdjointInfluenceResult& candidate) {
    if (baseline.particleIds != candidate.particleIds ||
        baseline.particleScaleGradient.size() != candidate.particleScaleGradient.size())
        throw std::invalid_argument("captured observation robustness particle influence fields are not comparable");
    double baselineNormSquared = 0.0;
    double candidateNormSquared = 0.0;
    double differenceNormSquared = 0.0;
    double dot = 0.0;
    for (std::size_t index = 0; index < baseline.particleScaleGradient.size(); ++index) {
        const double lhs = baseline.particleScaleGradient[index];
        const double rhs = candidate.particleScaleGradient[index];
        baselineNormSquared += lhs * lhs;
        candidateNormSquared += rhs * rhs;
        const double difference = rhs - lhs;
        differenceNormSquared += difference * difference;
        dot += lhs * rhs;
    }
    const double baselineNorm = std::sqrt(baselineNormSquared);
    const double candidateNorm = std::sqrt(candidateNormSquared);
    if (baselineNorm <= 1.0e-18 || candidateNorm <= 1.0e-18)
        throw std::runtime_error("captured observation robustness influence field is numerically empty");
    sample.particleInfluenceCosineSimilarity = dot / (baselineNorm * candidateNorm);
    sample.particleInfluenceCosineSimilarity = std::clamp(
        sample.particleInfluenceCosineSimilarity, -1.0, 1.0);
    sample.particleInfluenceRelativeL2Error = std::sqrt(differenceNormSquared) / baselineNorm;
}

void writeSample(std::ostream& stream, const CapturedObservationRobustnessSample& sample) {
    stream << sample.scenarioId << ','
           << std::setprecision(17)
           << sample.initialPositionNoiseRms << ','
           << sample.dynamicPositionNoiseRms << ','
           << sample.seed << ','
           << sample.selectedYoungModulus << ','
           << sample.selectedPoissonRatio << ','
           << sample.youngModulusRelativeDeltaFromBaseline << ','
           << sample.poissonRatioAbsoluteDeltaFromBaseline << ','
           << sample.fitDynamicRms << ','
           << sample.validationDynamicRms << ','
           << sample.initializationFitRms << ','
           << sample.appearanceRoundtripRms << ','
           << sample.particleInfluenceCosineSimilarity << ','
           << sample.particleInfluenceRelativeL2Error << ','
           << sample.strongestParticleId << ','
           << (sample.strongestParticleMatchesBaseline ? 1 : 0) << ','
           << sample.minimumStencilKnotMargin << ','
           << sample.adaptiveRegionCount << ','
           << sample.adaptiveParticleCount << ','
           << sample.adaptiveAbsoluteGradientFraction << ','
           << sample.adaptiveParticleJaccardWithBaseline << '\n';
}

} // namespace

capture::CapturedDeformableDataset perturbCapturedObservations(
    const capture::CapturedDeformableDataset& dataset,
    const CapturedObservationNoiseScenario& scenario) {
    validateScenario(scenario);
    auto result = dataset;
    for (std::size_t observationIndex = 0; observationIndex < result.observations.size(); ++observationIndex) {
        auto& observation = result.observations[observationIndex];
        const double sigma = observation.time == 0.0
            ? scenario.initialPositionNoiseRms
            : scenario.dynamicPositionNoiseRms;
        if (sigma == 0.0) continue;
        observation.position.x += sigma * deterministicUnitVarianceNoise(scenario.seed, observationIndex, 0U);
        observation.position.y += sigma * deterministicUnitVarianceNoise(scenario.seed, observationIndex, 1U);
        observation.position.z += sigma * deterministicUnitVarianceNoise(scenario.seed, observationIndex, 2U);
    }
    return result;
}

CapturedObservationRobustnessResult evaluateCapturedObservationRobustness(
    const gaussian::GaussianCloud& world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& cleanDataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    const std::vector<double>& youngModulusCandidates,
    const std::vector<double>& poissonRatioCandidates,
    const CapturedMaterialInfluenceSettings& influenceSettings,
    const CapturedMaterialAdaptiveRegionSettings& adaptiveSettings,
    const std::vector<CapturedObservationNoiseScenario>& scenarios) {
    if (youngModulusCandidates.empty() || poissonRatioCandidates.empty())
        throw std::invalid_argument("captured observation robustness requires material candidates");
    std::unordered_set<std::string> scenarioIds;
    for (const auto& scenario : scenarios) {
        validateScenario(scenario);
        if (!scenarioIds.insert(scenario.id).second || scenario.id == "baseline")
            throw std::invalid_argument("captured observation robustness scenario IDs must be unique and not 'baseline'");
    }

    auto baseline = evaluateScenario(
        world,
        activeGaussianIndices,
        cleanDataset,
        grid,
        settings,
        youngModulusCandidates,
        poissonRatioCandidates,
        influenceSettings,
        adaptiveSettings,
        "baseline",
        0.0,
        0.0,
        0U);
    baseline.sample.youngModulusRelativeDeltaFromBaseline = 0.0;
    baseline.sample.poissonRatioAbsoluteDeltaFromBaseline = 0.0;
    baseline.sample.particleInfluenceCosineSimilarity = 1.0;
    baseline.sample.particleInfluenceRelativeL2Error = 0.0;
    baseline.sample.strongestParticleMatchesBaseline = true;
    baseline.sample.adaptiveParticleJaccardWithBaseline = 1.0;

    const auto baselineProposalIds = proposedParticleIds(baseline.proposal);
    CapturedObservationRobustnessResult result;
    result.baseline = baseline.sample;
    result.scenarios.reserve(scenarios.size());

    for (const auto& scenario : scenarios) {
        const auto perturbed = perturbCapturedObservations(cleanDataset, scenario);
        auto evaluated = evaluateScenario(
            world,
            activeGaussianIndices,
            perturbed,
            grid,
            settings,
            youngModulusCandidates,
            poissonRatioCandidates,
            influenceSettings,
            adaptiveSettings,
            scenario.id,
            scenario.initialPositionNoiseRms,
            scenario.dynamicPositionNoiseRms,
            scenario.seed);
        evaluated.sample.youngModulusRelativeDeltaFromBaseline =
            std::abs(evaluated.sample.selectedYoungModulus - baseline.sample.selectedYoungModulus) /
            std::max(1.0e-18, std::abs(baseline.sample.selectedYoungModulus));
        evaluated.sample.poissonRatioAbsoluteDeltaFromBaseline =
            std::abs(evaluated.sample.selectedPoissonRatio - baseline.sample.selectedPoissonRatio);
        compareInfluence(evaluated.sample, baseline.adjoint, evaluated.adjoint);
        evaluated.sample.strongestParticleMatchesBaseline =
            evaluated.sample.strongestParticleId == baseline.sample.strongestParticleId;
        evaluated.sample.adaptiveParticleJaccardWithBaseline = jaccard(
            baselineProposalIds, proposedParticleIds(evaluated.proposal));
        result.scenarios.push_back(std::move(evaluated.sample));
    }
    return result;
}

void writeCapturedObservationRobustnessCsv(
    const CapturedObservationRobustnessResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured observation robustness CSV");
    stream << "scenario,initial_position_noise_rms,dynamic_position_noise_rms,seed,"
              "selected_young_modulus,selected_poisson_ratio,young_relative_delta_from_baseline,"
              "poisson_absolute_delta_from_baseline,fit_dynamic_rms,validation_dynamic_rms,"
              "initialization_fit_rms,appearance_roundtrip_rms,particle_influence_cosine_similarity,"
              "particle_influence_relative_l2_error,strongest_particle_id,strongest_matches_baseline,"
              "minimum_stencil_knot_margin,adaptive_region_count,adaptive_particle_count,"
              "adaptive_absolute_gradient_fraction,adaptive_particle_jaccard_with_baseline\n";
    writeSample(stream, result.baseline);
    for (const auto& sample : result.scenarios) writeSample(stream, sample);
    if (!stream) throw std::runtime_error("failed while writing captured observation robustness CSV");
}

} // namespace vulkax::research
