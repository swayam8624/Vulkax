#include "vulkax/cli/captured_influence.hpp"

#include "vulkax/capture/deformable_dataset.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/research/captured_operator_influence.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
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
    std::size_t consumed = 0;
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

[[nodiscard]] double characteristicParticleSpacing(
    const std::vector<capture::CapturedParticleSpec>& particles) {
    if (particles.size() < 2U) throw std::invalid_argument("captured body needs multiple particles");
    std::vector<double> nearest;
    nearest.reserve(particles.size());
    for (std::size_t i = 0; i < particles.size(); ++i) {
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < particles.size(); ++j) {
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

    constexpr std::size_t paddingCells = 8;
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
    grid.boundaryCells = 0;
    return grid;
}

[[nodiscard]] std::vector<research::CapturedMaterialInfluenceRegion> makeOctants(
    const capture::CapturedDeformableDataset& dataset) {
    if (dataset.particles.empty())
        throw std::invalid_argument("captured material influence needs particles");

    math::Vec3 minimum = dataset.particles.front().restPosition;
    math::Vec3 maximum = minimum;
    for (const auto& particle : dataset.particles) {
        minimum.x = std::min(minimum.x, particle.restPosition.x);
        minimum.y = std::min(minimum.y, particle.restPosition.y);
        minimum.z = std::min(minimum.z, particle.restPosition.z);
        maximum.x = std::max(maximum.x, particle.restPosition.x);
        maximum.y = std::max(maximum.y, particle.restPosition.y);
        maximum.z = std::max(maximum.z, particle.restPosition.z);
    }
    const math::Vec3 midpoint = (minimum + maximum) * 0.5;

    std::array<research::CapturedMaterialInfluenceRegion, 8> octants;
    for (std::size_t index = 0; index < octants.size(); ++index)
        octants[index].id = "octant_" + std::to_string(index);
    for (const auto& particle : dataset.particles) {
        std::size_t index = 0;
        if (particle.restPosition.x >= midpoint.x) index |= 1U;
        if (particle.restPosition.y >= midpoint.y) index |= 2U;
        if (particle.restPosition.z >= midpoint.z) index |= 4U;
        octants[index].particleIds.push_back(particle.particleId);
    }

    std::vector<research::CapturedMaterialInfluenceRegion> result;
    result.reserve(octants.size());
    for (auto& octant : octants) {
        if (!octant.particleIds.empty()) result.push_back(std::move(octant));
    }
    return result;
}

} // namespace

int capturedInfluenceCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "captured-material-influence") return -1;
    if (argc < 11)
        throw std::invalid_argument(
            "usage: vulkax captured-material-influence <object.ply> <particles.csv> <observations.csv> "
            "<output-dir> <marker-id> <time> <dir-x> <dir-y> <dir-z> "
            "[dt] [young-modulus] [poisson-ratio] [cell-size] [fd-scale-step] [verification-scale-delta]");

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
    const double dt = argc >= 12 ? parsePositiveDouble(argv[11], "timestep") : 5.0e-5;
    const double young = argc >= 13 ? parsePositiveDouble(argv[12], "Young's modulus") : 1.5e4;
    const double poisson = argc >= 14 ? parseFiniteDouble(argv[13], "Poisson ratio") : 0.30;
    if (!(poisson > -1.0 && poisson < 0.5))
        throw std::invalid_argument("Poisson ratio must lie in (-1, 0.5)");
    const double cellSize = argc >= 15
        ? parsePositiveDouble(argv[14], "cell size")
        : characteristicParticleSpacing(dataset.particles) * (2.0 / 3.0);
    const double finiteDifferenceStep = argc >= 16
        ? parsePositiveDouble(argv[15], "finite-difference scale step") : 0.025;
    const double verificationDelta = argc >= 17
        ? parseFiniteDouble(argv[16], "verification scale delta") : 0.05;

    std::vector<std::size_t> activeIndices(cloud.size());
    std::iota(activeIndices.begin(), activeIndices.end(), 0U);

    research::NonlinearDeformableWorldSettings settings;
    settings.dt = dt;
    settings.material = {1000.0, young, poisson};
    settings.couplingNeighborCount = std::min<std::size_t>(20U, dataset.particles.size());
    settings.transferScheme = solvers::MpmTransferScheme::APIC;
    settings.flipBlend = 0.0;

    research::CapturedMaterialInfluenceSettings influenceSettings;
    influenceSettings.objectiveMarkerId = markerId;
    influenceSettings.objectiveTime = objectiveTime;
    influenceSettings.objectiveDirection = direction;
    influenceSettings.finiteDifferenceScaleStep = finiteDifferenceStep;
    influenceSettings.verificationScaleDelta = verificationDelta;

    const auto regions = makeOctants(dataset);
    const auto result = research::computeCapturedMaterialInfluenceReference(
        cloud, activeIndices, dataset, makeGrid(dataset, cellSize), settings,
        regions, influenceSettings);

    std::filesystem::create_directories(outputDirectory);
    research::writeCapturedMaterialInfluenceCsv(result, outputDirectory / "influence.csv");
    research::writeCapturedMaterialCounterfactualCsv(result, outputDirectory / "counterfactual.csv");
    research::writeCapturedReplaySamplesCsv(result.baselineReplay, outputDirectory / "baseline_samples.csv");
    research::writeCapturedReplaySummaryCsv(result.baselineReplay, outputDirectory / "baseline_summary.csv");
    research::writeNonlinearDeformableWorldEvidenceCsv(
        result.baselineReplay.simulation, outputDirectory / "baseline_evidence.csv");

    double maximumAbsoluteDerivative = 0.0;
    double maximumRelativeVerificationError = 0.0;
    for (const auto& field : result.field)
        maximumAbsoluteDerivative = std::max(maximumAbsoluteDerivative, std::abs(field.derivative));
    for (const auto& verification : result.verification)
        maximumRelativeVerificationError = std::max(
            maximumRelativeVerificationError, verification.relativeLinearizationError);

    std::cout << std::setprecision(10)
              << "Captured material Operator Influence reference\n"
              << "  appearance_gaussians: " << cloud.size() << '\n'
              << "  physical_particles: " << dataset.particles.size() << '\n'
              << "  regions: " << result.field.size() << '\n'
              << "  region_scheme: rest-space octants\n"
              << "  objective_marker: " << markerId << '\n'
              << "  objective_time: " << objectiveTime << '\n'
              << "  objective_direction: " << result.objectiveDirection.x << ','
              << result.objectiveDirection.y << ',' << result.objectiveDirection.z << '\n'
              << "  baseline_observable: " << result.baselineObservable << '\n'
              << "  dt: " << dt << '\n'
              << "  young_modulus: " << young << '\n'
              << "  poisson_ratio: " << poisson << '\n'
              << "  grid_cell_size: " << cellSize << '\n'
              << "  finite_difference_scale_step: " << finiteDifferenceStep << '\n'
              << "  verification_scale_delta: " << verificationDelta << '\n'
              << "  max_abs_derivative: " << maximumAbsoluteDerivative << '\n'
              << "  max_relative_verification_error: " << maximumRelativeVerificationError << '\n'
              << "  outputs: " << outputDirectory.string() << '\n';
    return 0;
}

} // namespace vulkax::cli
