#include "vulkax/cli/captured_observation_robustness.hpp"

#include "vulkax/capture/deformable_dataset.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/research/captured_observation_robustness.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

[[nodiscard]] double parseNonNegativeDouble(std::string_view text, const char* label) {
    const double value = parseFiniteDouble(text, label);
    if (value < 0.0) throw std::invalid_argument(std::string(label) + " must be non-negative");
    return value;
}

[[nodiscard]] std::uint64_t parseUnsigned64(std::string_view text, const char* label) {
    const std::string owned(text);
    if (owned.empty() || !std::all_of(owned.begin(), owned.end(), [](char character) {
            return character >= '0' && character <= '9';
        }))
        throw std::invalid_argument(std::string(label) + " must be an unsigned integer");
    std::size_t consumed = 0U;
    unsigned long long value = 0U;
    try {
        value = std::stoull(owned, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " must be an unsigned integer");
    }
    if (consumed != owned.size() ||
        value > static_cast<unsigned long long>(std::numeric_limits<std::uint64_t>::max()))
        throw std::invalid_argument(std::string(label) + " is out of range");
    return static_cast<std::uint64_t>(value);
}

[[nodiscard]] double characteristicParticleSpacing(
    const std::vector<capture::CapturedParticleSpec>& particles) {
    if (particles.size() < 2U) throw std::invalid_argument("captured body needs multiple particles");
    std::vector<double> nearest;
    nearest.reserve(particles.size());
    for (std::size_t i = 0U; i < particles.size(); ++i) {
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0U; j < particles.size(); ++j) {
            if (i == j) continue;
            best = std::min(best, math::length(particles[i].restPosition - particles[j].restPosition));
        }
        if (std::isfinite(best) && best > 0.0) nearest.push_back(best);
    }
    if (nearest.empty()) throw std::invalid_argument("captured particle rest positions are coincident");
    std::sort(nearest.begin(), nearest.end());
    return nearest[nearest.size() / 2U];
}

[[nodiscard]] solvers::MpmGridSettings makeGrid(
    const capture::CapturedDeformableDataset& dataset,
    double cellSize) {
    if (dataset.particles.empty()) throw std::invalid_argument("captured robustness requires particles");
    math::Vec3 minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    math::Vec3 maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    const auto include = [&](math::Vec3 point) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    };
    for (const auto& particle : dataset.particles) include(particle.restPosition);
    for (const auto& observation : dataset.observations) include(observation.position);

    constexpr std::size_t paddingCells = 8U;
    const double padding = static_cast<double>(paddingCells) * cellSize;
    solvers::MpmGridSettings grid;
    grid.origin = {minimum.x - padding, minimum.y - padding, minimum.z - padding};
    const auto nodesFor = [&](double minValue, double maxValue) {
        const double extent = (maxValue - minValue) + 2.0 * padding;
        return std::max<std::size_t>(
            8U,
            static_cast<std::size_t>(std::ceil(extent / cellSize)) + 3U);
    };
    grid.nx = nodesFor(minimum.x, maximum.x);
    grid.ny = nodesFor(minimum.y, maximum.y);
    grid.nz = nodesFor(minimum.z, maximum.z);
    grid.cellSize = cellSize;
    grid.boundaryCells = 0U;
    return grid;
}

[[nodiscard]] std::vector<research::CapturedObservationNoiseScenario> makeScenarios(
    double initialNoise,
    double dynamicNoise,
    std::uint64_t baseSeed) {
    return {
        {"pose_half", 0.5 * initialNoise, 0.0, baseSeed + 1U},
        {"pose_full", initialNoise, 0.0, baseSeed + 2U},
        {"dynamic_half", 0.0, 0.5 * dynamicNoise, baseSeed + 3U},
        {"dynamic_full", 0.0, dynamicNoise, baseSeed + 4U},
        {"combined", initialNoise, dynamicNoise, baseSeed + 5U},
    };
}

void writeScenarioManifest(
    const std::vector<research::CapturedObservationNoiseScenario>& scenarios,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured robustness scenario manifest");
    stream << "scenario,initial_position_noise_rms,dynamic_position_noise_rms,seed\n";
    stream << "baseline,0,0,0\n";
    stream << std::setprecision(17);
    for (const auto& scenario : scenarios)
        stream << scenario.id << ',' << scenario.initialPositionNoiseRms << ','
               << scenario.dynamicPositionNoiseRms << ',' << scenario.seed << '\n';
    if (!stream) throw std::runtime_error("failed while writing captured robustness scenario manifest");
}

} // namespace

int capturedObservationRobustnessCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "captured-observation-robustness") return -1;
    if (argc < 13)
        throw std::invalid_argument(
            "usage: vulkax captured-observation-robustness <object.ply> <particles.csv> <observations.csv> "
            "<output-dir> <marker-id> <time> <dir-x> <dir-y> <dir-z> "
            "<initial-noise-rms> <dynamic-noise-rms> [seed] [dt] [cell-size]");

    const auto cloud = gaussian::load3dgsPly(argv[2]);
    const auto dataset = capture::loadCapturedDeformableDataset(argv[3], argv[4]);
    const std::filesystem::path outputDirectory(argv[5]);
    const std::string markerId(argv[6]);
    const double objectiveTime = parsePositiveDouble(argv[7], "objective time");
    const math::Vec3 direction{
        parseFiniteDouble(argv[8], "direction x"),
        parseFiniteDouble(argv[9], "direction y"),
        parseFiniteDouble(argv[10], "direction z"),
    };
    if (!(math::length(direction) > 0.0))
        throw std::invalid_argument("objective direction must be non-zero");

    const double initialNoise = parseNonNegativeDouble(argv[11], "initial-position noise RMS");
    const double dynamicNoise = parseNonNegativeDouble(argv[12], "dynamic-position noise RMS");
    const std::uint64_t seed = argc >= 14
        ? parseUnsigned64(argv[13], "seed") : 20260826ULL;
    const double dt = argc >= 15 ? parsePositiveDouble(argv[14], "timestep") : 5.0e-5;
    const double cellSize = argc >= 16
        ? parsePositiveDouble(argv[15], "cell size")
        : characteristicParticleSpacing(dataset.particles) * (2.0 / 3.0);

    std::vector<std::size_t> activeIndices(cloud.size());
    std::iota(activeIndices.begin(), activeIndices.end(), 0U);

    research::NonlinearDeformableWorldSettings settings;
    settings.dt = dt;
    settings.material.density = 1000.0;
    settings.couplingNeighborCount = std::min<std::size_t>(20U, dataset.particles.size());
    settings.transferScheme = solvers::MpmTransferScheme::APIC;
    settings.flipBlend = 0.0;

    research::CapturedMaterialInfluenceSettings influenceSettings;
    influenceSettings.objectiveMarkerId = markerId;
    influenceSettings.objectiveTime = objectiveTime;
    influenceSettings.objectiveDirection = direction;

    research::CapturedMaterialAdaptiveRegionSettings adaptiveSettings;
    const std::vector<double> youngCandidates{
        5.0e3, 7.5e3, 1.0e4, 1.5e4, 2.2e4, 3.3e4, 5.0e4,
    };
    const std::vector<double> poissonCandidates{0.20, 0.30, 0.40, 0.45};
    const auto scenarios = makeScenarios(initialNoise, dynamicNoise, seed);

    // Keep the physical grid fixed from the clean observations. Otherwise a
    // noise scenario could change both the measurements and the discretization,
    // confounding the robustness result.
    const auto grid = makeGrid(dataset, cellSize);
    const auto result = research::evaluateCapturedObservationRobustness(
        cloud,
        activeIndices,
        dataset,
        grid,
        settings,
        youngCandidates,
        poissonCandidates,
        influenceSettings,
        adaptiveSettings,
        scenarios);

    std::filesystem::create_directories(outputDirectory);
    research::writeCapturedObservationRobustnessCsv(result, outputDirectory / "robustness.csv");
    writeScenarioManifest(scenarios, outputDirectory / "scenarios.csv");

    double maximumYoungDelta = 0.0;
    double maximumPoissonDelta = 0.0;
    double minimumCosineSimilarity = 1.0;
    double maximumRelativeL2Error = 0.0;
    double minimumAdaptiveJaccard = 1.0;
    std::size_t strongestParticleMatches = 0U;
    for (const auto& sample : result.scenarios) {
        maximumYoungDelta = std::max(maximumYoungDelta, sample.youngModulusRelativeDeltaFromBaseline);
        maximumPoissonDelta = std::max(maximumPoissonDelta, sample.poissonRatioAbsoluteDeltaFromBaseline);
        minimumCosineSimilarity = std::min(minimumCosineSimilarity, sample.particleInfluenceCosineSimilarity);
        maximumRelativeL2Error = std::max(maximumRelativeL2Error, sample.particleInfluenceRelativeL2Error);
        minimumAdaptiveJaccard = std::min(minimumAdaptiveJaccard, sample.adaptiveParticleJaccardWithBaseline);
        if (sample.strongestParticleMatchesBaseline) ++strongestParticleMatches;
    }

    std::cout << std::setprecision(10)
              << "Captured observation robustness sweep\n"
              << "  appearance_gaussians: " << cloud.size() << '\n'
              << "  physical_particles: " << dataset.particles.size() << '\n'
              << "  observations: " << dataset.observations.size() << '\n'
              << "  scenarios: " << result.scenarios.size() << " + clean baseline\n"
              << "  objective_marker: " << markerId << '\n'
              << "  objective_time: " << objectiveTime << '\n'
              << "  maximum_initial_noise_rms: " << initialNoise << '\n'
              << "  maximum_dynamic_noise_rms: " << dynamicNoise << '\n'
              << "  base_seed: " << seed << '\n'
              << "  dt: " << dt << '\n'
              << "  grid_cell_size: " << cellSize << '\n'
              << "  baseline_young_modulus: " << result.baseline.selectedYoungModulus << '\n'
              << "  baseline_poisson_ratio: " << result.baseline.selectedPoissonRatio << '\n'
              << "  max_young_relative_delta: " << maximumYoungDelta << '\n'
              << "  max_poisson_absolute_delta: " << maximumPoissonDelta << '\n'
              << "  min_particle_influence_cosine: " << minimumCosineSimilarity << '\n'
              << "  max_particle_influence_relative_l2: " << maximumRelativeL2Error << '\n'
              << "  strongest_particle_matches: " << strongestParticleMatches << '/'
              << result.scenarios.size() << '\n'
              << "  min_adaptive_particle_jaccard: " << minimumAdaptiveJaccard << '\n'
              << "  interpretation: synthetic stress evidence; no real-capture robustness claim\n"
              << "  outputs: " << outputDirectory.string() << '\n';
    return 0;
}

} // namespace vulkax::cli
