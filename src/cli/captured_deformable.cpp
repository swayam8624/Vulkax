#include "vulkax/cli/captured_deformable.hpp"

#include "vulkax/capture/deformable_dataset.hpp"
#include "vulkax/cli/captured_example.hpp"
#include "vulkax/cli/captured_influence.hpp"
#include "vulkax/cli/captured_observation_robustness.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/research/captured_deformable.hpp"
#include "vulkax/research/captured_material_calibration.hpp"

#include <algorithm>
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

[[nodiscard]] research::NonlinearDeformableWorldSettings baseSettings(
    const capture::CapturedDeformableDataset& dataset,
    double dt) {
    research::NonlinearDeformableWorldSettings settings;
    settings.dt = dt;
    settings.material.density = 1000.0;
    settings.couplingNeighborCount = std::min<std::size_t>(20U, dataset.particles.size());
    settings.transferScheme = solvers::MpmTransferScheme::APIC;
    settings.flipBlend = 0.0;
    return settings;
}

[[nodiscard]] int materialCalibrationCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "captured-material-calibrate") return -1;
    if (argc < 6)
        throw std::invalid_argument(
            "usage: vulkax captured-material-calibrate <object.ply> <particles.csv> "
            "<observations.csv> <output-dir> [dt] [cell-size]");

    const auto cloud = gaussian::load3dgsPly(argv[2]);
    const auto dataset = capture::loadCapturedDeformableDataset(argv[3], argv[4]);
    const std::filesystem::path outputDirectory(argv[5]);
    const double dt = argc >= 7 ? parsePositiveDouble(argv[6], "timestep") : 5.0e-5;
    const double cellSize = argc >= 8
        ? parsePositiveDouble(argv[7], "cell size")
        : characteristicParticleSpacing(dataset.particles) * (2.0 / 3.0);

    std::vector<std::size_t> activeIndices(cloud.size());
    std::iota(activeIndices.begin(), activeIndices.end(), 0U);
    const std::vector<double> youngCandidates{
        5.0e3, 7.5e3, 1.0e4, 1.5e4, 2.2e4, 3.3e4, 5.0e4,
    };
    const std::vector<double> poissonCandidates{0.20, 0.30, 0.40, 0.45};

    const auto result = research::calibrateCapturedMaterialGrid(
        cloud, activeIndices, dataset, makeGrid(dataset, cellSize), baseSettings(dataset, dt),
        youngCandidates, poissonCandidates);
    const auto& selected = result.candidates.at(result.selectedIndex);

    std::filesystem::create_directories(outputDirectory);
    research::writeCapturedMaterialCalibrationCsv(result, outputDirectory / "material_grid.csv");
    research::writeCapturedReplaySamplesCsv(result.selectedReplay, outputDirectory / "selected_samples.csv");
    research::writeCapturedReplaySummaryCsv(result.selectedReplay, outputDirectory / "selected_summary.csv");
    research::writeNonlinearDeformableWorldEvidenceCsv(
        result.selectedReplay.simulation, outputDirectory / "selected_evidence.csv");

    std::cout << std::setprecision(10)
              << "Captured deformable material calibration\n"
              << "  appearance_gaussians: " << cloud.size() << '\n'
              << "  physical_particles: " << dataset.particles.size() << '\n'
              << "  observations: " << dataset.observations.size() << '\n'
              << "  candidates: " << result.candidates.size() << '\n'
              << "  selection_data: fit split, t > 0 only\n"
              << "  transfer: APIC\n"
              << "  dt: " << dt << '\n'
              << "  grid_cell_size: " << cellSize << '\n'
              << "  selected_young_modulus: " << selected.youngModulus << '\n'
              << "  selected_poisson_ratio: " << selected.poissonRatio << '\n'
              << "  fit_dynamic_samples: " << selected.fitDynamicSamples << '\n'
              << "  fit_dynamic_rms: " << selected.fitDynamicRms << '\n'
              << "  validation_dynamic_samples: " << selected.validationDynamicSamples << '\n'
              << "  validation_dynamic_rms: " << selected.validationDynamicRms << '\n'
              << "  initialization_fit_rms: " << selected.initializationFitRms << '\n'
              << "  appearance_roundtrip_rms: " << selected.appearanceRoundtripRms << '\n'
              << "  outputs: " << outputDirectory.string() << '\n';
    return 0;
}

} // namespace

int capturedDeformableCommand(int argc, char** argv) {
    const int example = capturedExampleCommand(argc, argv);
    if (example >= 0) return example;

    const int influence = capturedInfluenceCommand(argc, argv);
    if (influence >= 0) return influence;

    const int robustness = capturedObservationRobustnessCommand(argc, argv);
    if (robustness >= 0) return robustness;

    const int calibration = materialCalibrationCommand(argc, argv);
    if (calibration >= 0) return calibration;

    if (argc < 2 || std::string_view(argv[1]) != "captured-deformable-evaluate") return -1;
    if (argc < 6)
        throw std::invalid_argument(
            "usage: vulkax captured-deformable-evaluate <object.ply> <particles.csv> "
            "<observations.csv> <output-dir> [dt] [young-modulus] [poisson-ratio] [cell-size]");

    const auto cloud = gaussian::load3dgsPly(argv[2]);
    const auto dataset = capture::loadCapturedDeformableDataset(argv[3], argv[4]);
    const std::filesystem::path outputDirectory(argv[5]);
    const double dt = argc >= 7 ? parsePositiveDouble(argv[6], "timestep") : 5.0e-5;
    const double young = argc >= 8 ? parsePositiveDouble(argv[7], "Young's modulus") : 1.5e4;
    const double poisson = argc >= 9 ? parseFiniteDouble(argv[8], "Poisson ratio") : 0.30;
    if (!(poisson > -1.0 && poisson < 0.5))
        throw std::invalid_argument("Poisson ratio must lie in (-1, 0.5)");
    const double cellSize = argc >= 10
        ? parsePositiveDouble(argv[9], "cell size")
        : characteristicParticleSpacing(dataset.particles) * (2.0 / 3.0);

    std::vector<std::size_t> activeIndices(cloud.size());
    std::iota(activeIndices.begin(), activeIndices.end(), 0U);

    auto settings = baseSettings(dataset, dt);
    settings.material.youngModulus = young;
    settings.material.poissonRatio = poisson;

    const auto grid = makeGrid(dataset, cellSize);
    const auto result = research::runCapturedFreeRelaxationBenchmark(
        cloud, activeIndices, dataset, grid, settings);

    std::filesystem::create_directories(outputDirectory);
    research::writeCapturedReplaySamplesCsv(result, outputDirectory / "samples.csv");
    research::writeCapturedReplaySummaryCsv(result, outputDirectory / "summary.csv");
    research::writeNonlinearDeformableWorldEvidenceCsv(result.simulation, outputDirectory / "evidence.csv");

    std::cout << std::setprecision(10)
              << "Captured deformable free-relaxation replay\n"
              << "  appearance_gaussians: " << cloud.size() << '\n'
              << "  physical_particles: " << dataset.particles.size() << '\n'
              << "  observations: " << dataset.observations.size() << '\n'
              << "  transfer: APIC\n"
              << "  dt: " << dt << '\n'
              << "  young_modulus: " << young << '\n'
              << "  poisson_ratio: " << poisson << '\n'
              << "  grid_cell_size: " << cellSize << '\n'
              << "  initialization_fit_rms: " << result.initializationFitRms << '\n'
              << "  appearance_roundtrip_rms: " << result.appearanceRoundtripRms << '\n'
              << "  appearance_roundtrip_max: " << result.appearanceRoundtripMaximum << '\n'
              << "  fit_samples: " << result.fit.sampleCount << '\n'
              << "  fit_rms: " << result.fit.rmsPositionError << '\n'
              << "  fit_max: " << result.fit.maximumPositionError << '\n'
              << "  validation_samples: " << result.validation.sampleCount << '\n'
              << "  validation_rms: " << result.validation.rmsPositionError << '\n'
              << "  validation_max: " << result.validation.maximumPositionError << '\n'
              << "  max_energy_drift: " << result.simulation.maximumRelativeMechanicalEnergyDrift << '\n'
              << "  min_J: " << result.simulation.minimumDeformationDeterminant << '\n'
              << "  max_J: " << result.simulation.maximumDeformationDeterminant << '\n'
              << "  max_mls_rms_residual: " << result.simulation.maximumMlsRmsResidual << '\n'
              << "  outputs: " << outputDirectory.string() << '\n';
    return 0;
}

} // namespace vulkax::cli