#include "vulkax/cli/captured_influence.hpp"

#include "vulkax/capture/deformable_dataset.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/research/adaptive_material_influence.hpp"
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

[[nodiscard]] std::size_t parsePositiveSize(std::string_view text, const char* label) {
    const std::string owned(text);
    std::size_t consumed = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(owned, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " must be a positive integer");
    }
    if (consumed != owned.size() || value == 0U ||
        value > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
        throw std::invalid_argument(std::string(label) + " must be a positive integer");
    return static_cast<std::size_t>(value);
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

[[nodiscard]] double maximumCounterfactualRelativeError(
    const research::CapturedMaterialInfluenceResult& result) {
    double maximum = 0.0;
    for (const auto& verification : result.verification)
        maximum = std::max(maximum, verification.relativeLinearizationError);
    return maximum;
}

[[nodiscard]] double maximumDerivativeAbsoluteError(
    const std::vector<research::CapturedMaterialInfluenceDerivativeComparison>& comparison) {
    double maximum = 0.0;
    for (const auto& sample : comparison) maximum = std::max(maximum, sample.absoluteError);
    return maximum;
}

[[nodiscard]] double maximumDerivativeRelativeError(
    const std::vector<research::CapturedMaterialInfluenceDerivativeComparison>& comparison) {
    double maximum = 0.0;
    for (const auto& sample : comparison) maximum = std::max(maximum, sample.relativeError);
    return maximum;
}

} // namespace

int capturedInfluenceCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "captured-material-influence") return -1;
    if (argc < 11)
        throw std::invalid_argument(
            "usage: vulkax captured-material-influence <object.ply> <particles.csv> <observations.csv> "
            "<output-dir> <marker-id> <time> <dir-x> <dir-y> <dir-z> "
            "[dt] [young-modulus] [poisson-ratio] [cell-size] [fd-scale-step] [verification-scale-delta] "
            "[adaptive-gradient-fraction] [adaptive-relative-threshold] "
            "[adaptive-adjacency-multiplier] [adaptive-max-regions]");

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

    research::CapturedMaterialAdaptiveRegionSettings adaptiveSettings;
    adaptiveSettings.cumulativeAbsoluteGradientFraction = argc >= 18
        ? parsePositiveDouble(argv[17], "adaptive gradient fraction") : 0.90;
    adaptiveSettings.relativeParticleGradientThreshold = argc >= 19
        ? parseFiniteDouble(argv[18], "adaptive relative gradient threshold") : 0.05;
    adaptiveSettings.adjacencyRadiusMultiplier = argc >= 20
        ? parsePositiveDouble(argv[19], "adaptive adjacency multiplier") : 1.05;
    adaptiveSettings.maximumRegions = argc >= 21
        ? parsePositiveSize(argv[20], "adaptive maximum region count") : 8U;

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

    const auto octantRegions = makeOctants(dataset);
    const auto grid = makeGrid(dataset, cellSize);
    const auto result = research::computeCapturedMaterialInfluenceReference(
        cloud, activeIndices, dataset, grid, settings, octantRegions, influenceSettings);
    const auto adjoint = research::computeCapturedMaterialInfluenceAdjoint(
        cloud, activeIndices, dataset, grid, settings, octantRegions, influenceSettings);
    const auto derivativeComparison = research::compareCapturedMaterialInfluenceDerivatives(
        result, adjoint);

    const auto adaptiveProposal = research::proposeCapturedMaterialInfluenceRegions(
        dataset, adjoint, adaptiveSettings);
    const auto adaptiveAdjoint = research::aggregateCapturedMaterialInfluenceAdjoint(
        dataset, adjoint, adaptiveProposal.regions);
    const auto adaptiveReference = research::computeCapturedMaterialInfluenceReference(
        cloud, activeIndices, dataset, grid, settings, adaptiveProposal.regions, influenceSettings);
    const auto adaptiveComparison = research::compareCapturedMaterialInfluenceDerivatives(
        adaptiveReference, adaptiveAdjoint);

    std::filesystem::create_directories(outputDirectory);
    research::writeCapturedMaterialInfluenceCsv(result, outputDirectory / "influence.csv");
    research::writeCapturedMaterialCounterfactualCsv(result, outputDirectory / "counterfactual.csv");
    research::writeCapturedMaterialAdjointInfluenceCsv(
        adjoint, outputDirectory / "adjoint_influence.csv");
    research::writeCapturedMaterialParticleAdjointCsv(
        adjoint, outputDirectory / "particle_adjoint.csv");
    research::writeCapturedMaterialInfluenceDerivativeComparisonCsv(
        derivativeComparison, outputDirectory / "derivative_comparison.csv");
    research::writeCapturedMaterialAdaptiveRegionProposalSummaryCsv(
        adaptiveProposal, outputDirectory / "adaptive_proposal_summary.csv");
    research::writeCapturedMaterialInfluenceCsv(
        adaptiveReference, outputDirectory / "adaptive_influence.csv");
    research::writeCapturedMaterialCounterfactualCsv(
        adaptiveReference, outputDirectory / "adaptive_counterfactual.csv");
    research::writeCapturedMaterialAdjointInfluenceCsv(
        adaptiveAdjoint, outputDirectory / "adaptive_adjoint_influence.csv");
    research::writeCapturedMaterialInfluenceDerivativeComparisonCsv(
        adaptiveComparison, outputDirectory / "adaptive_derivative_comparison.csv");
    research::writeCapturedReplaySamplesCsv(result.baselineReplay, outputDirectory / "baseline_samples.csv");
    research::writeCapturedReplaySummaryCsv(result.baselineReplay, outputDirectory / "baseline_summary.csv");
    research::writeNonlinearDeformableWorldEvidenceCsv(
        result.baselineReplay.simulation, outputDirectory / "baseline_evidence.csv");

    double maximumAbsoluteDerivative = 0.0;
    double maximumParticleAdjointDerivative = 0.0;
    for (const auto& field : result.field)
        maximumAbsoluteDerivative = std::max(maximumAbsoluteDerivative, std::abs(field.derivative));
    for (const double derivative : adjoint.particleScaleGradient)
        maximumParticleAdjointDerivative = std::max(maximumParticleAdjointDerivative, std::abs(derivative));

    std::cout << std::setprecision(10)
              << "Captured material Operator Influence reference + discrete adjoint + adaptive proposals\n"
              << "  appearance_gaussians: " << cloud.size() << '\n'
              << "  physical_particles: " << dataset.particles.size() << '\n'
              << "  reference_regions: " << result.field.size() << '\n'
              << "  reference_region_scheme: rest-space octants\n"
              << "  adaptive_regions: " << adaptiveProposal.regions.size() << '\n'
              << "  adaptive_proposed_particles: " << adaptiveProposal.proposedParticleCount << '\n'
              << "  adaptive_proposed_abs_gradient_fraction: "
              << adaptiveProposal.proposedAbsoluteGradientFraction << '\n'
              << "  adaptive_characteristic_spacing: " << adaptiveProposal.characteristicSpacing << '\n'
              << "  adaptive_adjacency_radius: " << adaptiveProposal.adjacencyRadius << '\n'
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
              << "  max_abs_reference_derivative: " << maximumAbsoluteDerivative << '\n'
              << "  max_relative_counterfactual_error: "
              << maximumCounterfactualRelativeError(result) << '\n'
              << "  adjoint_min_stencil_knot_margin: " << adjoint.minimumStencilKnotMargin << '\n'
              << "  max_abs_particle_adjoint_derivative: " << maximumParticleAdjointDerivative << '\n'
              << "  max_adjoint_absolute_derivative_error: "
              << maximumDerivativeAbsoluteError(derivativeComparison) << '\n'
              << "  max_adjoint_relative_derivative_error: "
              << maximumDerivativeRelativeError(derivativeComparison) << '\n'
              << "  adaptive_max_relative_counterfactual_error: "
              << maximumCounterfactualRelativeError(adaptiveReference) << '\n'
              << "  adaptive_max_adjoint_absolute_derivative_error: "
              << maximumDerivativeAbsoluteError(adaptiveComparison) << '\n'
              << "  adaptive_max_adjoint_relative_derivative_error: "
              << maximumDerivativeRelativeError(adaptiveComparison) << '\n'
              << "  outputs: " << outputDirectory.string() << '\n';
    return 0;
}

} // namespace vulkax::cli
