#include "vulkax/research/captured_world_run.hpp"

#include "vulkax/core/sha256.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/render/gaussian.hpp"
#include "vulkax/render/headless.hpp"
#include "vulkax/research/captured_material_calibration.hpp"
#include "vulkax/research/captured_observation_robustness.hpp"
#include "vulkax/research/captured_operator_influence.hpp"
#include "vulkax/research/captured_verified_rewrite.hpp"
#include "vulkax/world/correspondence_graph.hpp"
#include "vulkax/world/world_ir.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vulkax::research {
namespace {

struct GaussianBounds {
    math::Vec3 minimum{};
    math::Vec3 maximum{};
};

struct ArtifactRecord {
    std::string path;
    std::uintmax_t bytes{};
    std::string sha256;
};

[[nodiscard]] double characteristicParticleSpacing(
    const std::vector<capture::CapturedParticleSpec>& particles) {
    if (particles.size() < 2U) throw std::invalid_argument("captured-world-run needs multiple particles");
    std::vector<double> nearest;
    nearest.reserve(particles.size());
    for (std::size_t i = 0; i < particles.size(); ++i) {
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < particles.size(); ++j) {
            if (i == j) continue;
            const double distance = math::length(particles[i].restPosition - particles[j].restPosition);
            if (distance > 0.0) best = std::min(best, distance);
        }
        if (std::isfinite(best)) nearest.push_back(best);
    }
    if (nearest.empty()) throw std::invalid_argument("captured-world-run particle rest positions are coincident");
    std::sort(nearest.begin(), nearest.end());
    const std::size_t middle = nearest.size() / 2U;
    if (nearest.size() % 2U == 1U) return nearest[middle];
    return 0.5 * (nearest[middle - 1U] + nearest[middle]);
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

    constexpr std::size_t paddingCells = 8U;
    const double padding = static_cast<double>(paddingCells) * cellSize;
    solvers::MpmGridSettings grid;
    grid.origin = {minimum.x - padding, minimum.y - padding, minimum.z - padding};
    const auto nodesFor = [&](double minValue, double maxValue) {
        const double extent = (maxValue - minValue) + 2.0 * padding;
        return std::max<std::size_t>(
            8U, static_cast<std::size_t>(std::ceil(extent / cellSize)) + 3U);
    };
    grid.nx = nodesFor(minimum.x, maximum.x);
    grid.ny = nodesFor(minimum.y, maximum.y);
    grid.nz = nodesFor(minimum.z, maximum.z);
    grid.cellSize = cellSize;
    grid.boundaryCells = 0U;
    return grid;
}

[[nodiscard]] NonlinearDeformableWorldSettings makeWorldSettings(
    const capture::CapturedDeformableDataset& dataset,
    double dt,
    double young,
    double poisson) {
    NonlinearDeformableWorldSettings settings;
    settings.dt = dt;
    settings.material = {1000.0, young, poisson};
    settings.couplingNeighborCount = std::min<std::size_t>(20U, dataset.particles.size());
    settings.transferScheme = solvers::MpmTransferScheme::APIC;
    settings.flipBlend = 0.0;
    return settings;
}

[[nodiscard]] std::vector<CapturedMaterialInfluenceRegion> makeOctants(
    const capture::CapturedDeformableDataset& dataset) {
    if (dataset.particles.empty()) throw std::invalid_argument("captured-world-run needs particles");
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
    std::array<CapturedMaterialInfluenceRegion, 8> octants;
    for (std::size_t index = 0; index < octants.size(); ++index)
        octants[index].id = "octant_" + std::to_string(index);
    for (const auto& particle : dataset.particles) {
        std::size_t index = 0U;
        if (particle.restPosition.x >= midpoint.x) index |= 1U;
        if (particle.restPosition.y >= midpoint.y) index |= 2U;
        if (particle.restPosition.z >= midpoint.z) index |= 4U;
        octants[index].particleIds.push_back(particle.particleId);
    }
    std::vector<CapturedMaterialInfluenceRegion> result;
    for (auto& region : octants)
        if (!region.particleIds.empty()) result.push_back(std::move(region));
    return result;
}

[[nodiscard]] std::vector<CapturedObservationNoiseScenario> makeRobustnessScenarios(
    const capture::CapturedDeformableBundle& bundle,
    std::uint64_t baseSeed) {
    if (baseSeed > std::numeric_limits<std::uint64_t>::max() - 2U)
        throw std::invalid_argument("captured-world-run robustness seed is too large");
    double initialSigma = 0.0;
    double dynamicSigma = 0.0;
    for (const auto& sample : bundle.uncertainty) {
        const double sigma = std::max({
            sample.positionSigma.x, sample.positionSigma.y, sample.positionSigma.z,
        });
        if (std::abs(sample.time) <= 1.0e-15) initialSigma = std::max(initialSigma, sigma);
        else dynamicSigma = std::max(dynamicSigma, sigma);
    }
    std::vector<CapturedObservationNoiseScenario> scenarios;
    scenarios.push_back({"zero_clone", 0.0, 0.0, baseSeed});
    if (initialSigma > 0.0 || dynamicSigma > 0.0) {
        scenarios.push_back({"declared_half", 0.5 * initialSigma, 0.5 * dynamicSigma, baseSeed + 1U});
        scenarios.push_back({"declared_full", initialSigma, dynamicSigma, baseSeed + 2U});
    }
    return scenarios;
}

void validateSettings(const CapturedWorldRunSettings& settings) {
    if (settings.objectiveMarkerId.empty())
        throw std::invalid_argument("captured-world-run objective marker ID must not be empty");
    if (!std::isfinite(settings.objectiveTime) || !(settings.objectiveTime > 0.0))
        throw std::invalid_argument("captured-world-run objective time must be finite and positive");
    if (math::length(settings.objectiveDirection) <= 1.0e-15)
        throw std::invalid_argument("captured-world-run objective direction must be non-zero");
    if (settings.cellSize && (!std::isfinite(*settings.cellSize) || !(*settings.cellSize > 0.0)))
        throw std::invalid_argument("captured-world-run cell size must be finite and positive");
    if (!std::isfinite(settings.finiteDifferenceScaleStep) || !(settings.finiteDifferenceScaleStep > 0.0))
        throw std::invalid_argument("captured-world-run finite-difference step must be finite and positive");
    if (!std::isfinite(settings.rewriteScaleDelta) || std::abs(settings.rewriteScaleDelta) <= 1.0e-15 ||
        !(1.0 + settings.rewriteScaleDelta > 0.0))
        throw std::invalid_argument("captured-world-run rewrite delta must be non-zero and keep Young's modulus positive");
    if (settings.youngModulusCandidates.empty() || settings.poissonRatioCandidates.empty())
        throw std::invalid_argument("captured-world-run calibration candidate grids must not be empty");
    for (const double young : settings.youngModulusCandidates)
        if (!std::isfinite(young) || !(young > 0.0))
            throw std::invalid_argument("captured-world-run Young's-modulus candidates must be finite and positive");
    for (const double poisson : settings.poissonRatioCandidates)
        if (!std::isfinite(poisson) || !(poisson > -1.0 && poisson < 0.5))
            throw std::invalid_argument("captured-world-run Poisson-ratio candidates must lie in (-1, 0.5)");
    if (settings.render && (settings.renderWidth == 0U || settings.renderHeight == 0U))
        throw std::invalid_argument("captured-world-run render dimensions must be positive");
}

void prepareOutputDirectory(const std::filesystem::path& outputDirectory) {
    if (std::filesystem::exists(outputDirectory)) {
        if (!std::filesystem::is_directory(outputDirectory))
            throw std::invalid_argument("captured-world-run output path exists and is not a directory");
        if (std::filesystem::directory_iterator(outputDirectory) != std::filesystem::directory_iterator{})
            throw std::invalid_argument("captured-world-run output directory must be empty or absent");
    }
    std::filesystem::create_directories(outputDirectory);
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write captured-world-run artifact: " + path.string());
    output << text;
}

void writeRobustnessScenarios(
    const std::vector<CapturedObservationNoiseScenario>& scenarios,
    const std::filesystem::path& path) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write captured-world-run robustness scenarios");
    output << "scenario,initial_position_noise_rms,dynamic_position_noise_rms,seed\n";
    output << std::setprecision(17);
    for (const auto& scenario : scenarios)
        output << scenario.id << ',' << scenario.initialPositionNoiseRms << ','
               << scenario.dynamicPositionNoiseRms << ',' << scenario.seed << '\n';
}

void writeSelectedRegion(
    const CapturedMaterialInfluenceRegion& region,
    const std::filesystem::path& path) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write captured-world-run selected region");
    output << "region_id,particle_id\n";
    for (const auto id : region.particleIds) output << region.id << ',' << id << '\n';
}

void writeTransactionSummary(
    const std::filesystem::path& path,
    const world::VerifiedRewriteResult& result,
    double youngBefore,
    double youngAfter,
    const CapturedMaterialInfluenceRegion& region) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write captured-world-run transaction summary");
    output << "status,region_id,revision_before,revision_after,particle_count,young_modulus_before,"
              "young_modulus_after,unaffected_position_drift,physical_observable_error,"
              "physical_observable_tolerance,rollback_performed\n";
    output << std::setprecision(17)
           << world::toString(result.status) << ',' << region.id << ','
           << result.receipt.revisionBefore << ',' << result.receipt.revisionAfter << ','
           << region.particleIds.size() << ',' << youngBefore << ',' << youngAfter << ','
           << result.evidence.unaffectedPositionDrift << ','
           << result.evidence.physicalObservableError << ','
           << result.evidence.physicalObservableTolerance << ','
           << (result.rollbackPerformed ? 1 : 0) << '\n';
}

void writeProvenance(const world::WorldIR& worldState, const std::filesystem::path& path) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write captured-world-run provenance");
    output << "revision,transaction_id,author,summary\n";
    for (const auto& record : worldState.provenance)
        output << record.revision << ',' << record.transactionId << ',' << record.author << ','
               << record.summary << '\n';
}

[[nodiscard]] GaussianBounds gaussianBounds(const gaussian::GaussianCloud& cloud) {
    if (cloud.empty()) throw std::invalid_argument("captured-world-run appearance is empty");
    GaussianBounds result{
        {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
         std::numeric_limits<double>::infinity()},
        {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
         -std::numeric_limits<double>::infinity()},
    };
    for (const auto& splat : cloud.splats) {
        result.minimum.x = std::min(result.minimum.x, splat.position.x);
        result.minimum.y = std::min(result.minimum.y, splat.position.y);
        result.minimum.z = std::min(result.minimum.z, splat.position.z);
        result.maximum.x = std::max(result.maximum.x, splat.position.x);
        result.maximum.y = std::max(result.maximum.y, splat.position.y);
        result.maximum.z = std::max(result.maximum.z, splat.position.z);
    }
    return result;
}

[[nodiscard]] render::GaussianRenderSettings makeRenderSettings(
    const gaussian::GaussianCloud& cloud,
    std::uint32_t width,
    std::uint32_t height) {
    const auto bounds = gaussianBounds(cloud);
    const math::Vec3 center = (bounds.minimum + bounds.maximum) * 0.5;
    const math::Vec3 extent = bounds.maximum - bounds.minimum;
    const double sceneSpan = std::max({extent.x, extent.y, extent.z, 1.0e-3});
    render::GaussianRenderSettings settings;
    settings.image.width = width;
    settings.image.height = height;
    settings.camera.target = center;
    settings.camera.position = {center.x, center.y, center.z + 2.5 * sceneSpan};
    settings.camera.up = {0.0, 1.0, 0.0};
    settings.camera.verticalFovDegrees = 50.0;
    settings.nearPlane = std::max(1.0e-5, sceneSpan * 1.0e-5);
    return settings;
}

[[nodiscard]] backend::BackendKind selectRenderBackend(
    const std::optional<backend::BackendKind>& requested) {
    const auto available = render::availableHeadlessRenderBackends();
    if (available.empty())
        throw std::runtime_error("captured-world-run requires a native headless render backend; use render=false only for non-render regression paths");
    if (requested) {
        if (std::find(available.begin(), available.end(), *requested) == available.end())
            throw std::runtime_error("captured-world-run requested render backend is unavailable");
        return *requested;
    }
    const auto metal = std::find(available.begin(), available.end(), backend::BackendKind::Metal);
    const auto vulkan = std::find(available.begin(), available.end(), backend::BackendKind::Vulkan);
    if (backend::currentPlatform() == backend::PlatformKind::MacOS && metal != available.end()) return *metal;
    if (vulkan != available.end()) return *vulkan;
    return available.front();
}

void writeRenderComparison(
    const std::filesystem::path& path,
    backend::BackendKind backendKind,
    const render::GaussianRenderResult& before,
    const render::GaussianRenderResult& after,
    const render::ImageComparison& comparison) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write captured-world-run render comparison");
    output << "backend,before_visible_splats,after_visible_splats,pixel_count,max_channel_difference,"
              "mean_absolute_difference,rmse,psnr_db,changed_pixel_fraction\n";
    output << std::setprecision(17) << backend::toString(backendKind) << ','
           << before.stats.visibleSplats << ',' << after.stats.visibleSplats << ','
           << comparison.pixelCount << ',' << static_cast<unsigned>(comparison.maximumChannelDifference) << ','
           << comparison.meanAbsoluteDifference << ',' << comparison.rootMeanSquareError << ',';
    if (std::isfinite(comparison.psnrDb)) output << comparison.psnrDb;
    else output << "inf";
    output << ',' << comparison.changedPixelFraction << '\n';
}

[[nodiscard]] std::string jsonEscape(const std::string& text) {
    std::ostringstream output;
    for (const char character : text) {
        switch (character) {
            case '\\': output << "\\\\"; break;
            case '"': output << "\\\""; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default: output << character; break;
        }
    }
    return output.str();
}

[[nodiscard]] std::vector<ArtifactRecord> collectArtifacts(
    const std::filesystem::path& outputDirectory) {
    std::vector<ArtifactRecord> artifacts;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(outputDirectory)) {
        if (!entry.is_regular_file()) continue;
        const auto relative = std::filesystem::relative(entry.path(), outputDirectory).generic_string();
        if (relative == "certificate.json") continue;
        artifacts.push_back({relative, entry.file_size(), core::sha256FileHex(entry.path())});
    }
    std::sort(artifacts.begin(), artifacts.end(), [](const ArtifactRecord& lhs, const ArtifactRecord& rhs) {
        return lhs.path < rhs.path;
    });
    return artifacts;
}

void writeRunSummary(const std::filesystem::path& path, const CapturedWorldRunSummary& summary) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write captured-world-run summary");
    output << "bundle_id,source_kind,appearance_gaussians,physical_particles,observations,"
              "selected_young_modulus,selected_poisson_ratio,fit_dynamic_rms,validation_dynamic_rms,"
              "robustness_scenarios,adaptive_regions,adaptive_particles,adaptive_abs_gradient_fraction,"
              "rewrite_region,rewrite_particles,rewrite_status,rollback_performed,physical_error,"
              "physical_tolerance,render_produced,render_backend\n";
    output << std::setprecision(17)
           << summary.bundleId << ',' << capture::toString(summary.sourceKind) << ','
           << summary.appearanceGaussians << ',' << summary.physicalParticles << ',' << summary.observations << ','
           << summary.selectedYoungModulus << ',' << summary.selectedPoissonRatio << ','
           << summary.fitDynamicRms << ',' << summary.validationDynamicRms << ','
           << summary.robustnessScenarioCount << ',' << summary.adaptiveRegionCount << ','
           << summary.adaptiveParticleCount << ',' << summary.adaptiveAbsoluteGradientFraction << ','
           << summary.rewriteRegionId << ',' << summary.rewriteParticleCount << ','
           << world::toString(summary.rewriteStatus) << ',' << (summary.rollbackPerformed ? 1 : 0) << ','
           << summary.physicalObservableError << ',' << summary.physicalObservableTolerance << ','
           << (summary.renderProduced ? 1 : 0) << ',';
    if (summary.renderBackend) output << backend::toString(*summary.renderBackend);
    else output << "none";
    output << '\n';
}

void writeCertificate(
    const std::filesystem::path& path,
    const std::filesystem::path& manifestPath,
    const capture::CapturedDeformableBundle& bundle,
    const CapturedWorldRunSettings& settings,
    const CapturedWorldRunSummary& summary,
    const std::vector<ArtifactRecord>& artifacts) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write captured-world-run certificate");
    output << std::setprecision(17);
    output << "{\n"
           << "  \"schema\": \"vulkax_captured_world_run\",\n"
           << "  \"schema_version\": 2,\n"
           << "  \"run_status\": \"completed\",\n"
           << "  \"bundle_id\": \"" << jsonEscape(summary.bundleId) << "\",\n"
           << "  \"source_kind\": \"" << capture::toString(summary.sourceKind) << "\",\n"
           << "  \"source_manifest_sha256\": \"" << core::sha256FileHex(manifestPath) << "\",\n"
           << "  \"input_payload_hashes\": {\n"
           << "    \"appearance\": \"" << bundle.manifest.appearanceSha256 << "\",\n"
           << "    \"particles\": \"" << bundle.manifest.particlesSha256 << "\",\n"
           << "    \"observations\": \"" << bundle.manifest.observationsSha256 << "\",\n"
           << "    \"uncertainty\": \"" << bundle.manifest.uncertaintySha256 << "\"\n"
           << "  },\n"
           << "  \"objective\": {\"marker_id\": \"" << jsonEscape(settings.objectiveMarkerId)
           << "\", \"time\": " << settings.objectiveTime
           << ", \"direction\": [" << settings.objectiveDirection.x << ','
           << settings.objectiveDirection.y << ',' << settings.objectiveDirection.z << "]},\n"
           << "  \"calibration\": {\"young_modulus\": " << summary.selectedYoungModulus
           << ", \"poisson_ratio\": " << summary.selectedPoissonRatio
           << ", \"fit_dynamic_rms\": " << summary.fitDynamicRms
           << ", \"validation_dynamic_rms\": " << summary.validationDynamicRms << "},\n"
           << "  \"robustness_scenario_count\": " << summary.robustnessScenarioCount << ",\n"
           << "  \"adaptive_proposal\": {\"region_count\": " << summary.adaptiveRegionCount
           << ", \"particle_count\": " << summary.adaptiveParticleCount
           << ", \"absolute_gradient_fraction\": " << summary.adaptiveAbsoluteGradientFraction << "},\n"
           << "  \"rewrite\": {\"status\": \"" << world::toString(summary.rewriteStatus)
           << "\", \"region_id\": \"" << jsonEscape(summary.rewriteRegionId)
           << "\", \"particle_count\": " << summary.rewriteParticleCount
           << ", \"rollback_performed\": " << (summary.rollbackPerformed ? "true" : "false")
           << ", \"physical_error\": " << summary.physicalObservableError
           << ", \"physical_tolerance\": " << summary.physicalObservableTolerance << "},\n"
           << "  \"render\": {\"produced\": " << (summary.renderProduced ? "true" : "false");
    if (summary.renderBackend)
        output << ", \"backend\": \"" << backend::toString(*summary.renderBackend) << "\""
               << ", \"max_channel_difference\": "
               << static_cast<unsigned>(summary.renderComparison.maximumChannelDifference)
               << ", \"rmse\": " << summary.renderComparison.rootMeanSquareError
               << ", \"changed_pixel_fraction\": " << summary.renderComparison.changedPixelFraction;
    output << "},\n"
           << "  \"research_integrity\": \"run completion is independent of rewrite acceptance; source kind is manifest-declared; synthetic data do not become measured evidence\",\n"
           << "  \"artifacts\": [\n";
    for (std::size_t index = 0; index < artifacts.size(); ++index) {
        const auto& artifact = artifacts[index];
        output << "    {\"path\": \"" << jsonEscape(artifact.path) << "\", \"bytes\": "
               << artifact.bytes << ", \"sha256\": \"" << artifact.sha256 << "\"}";
        if (index + 1U != artifacts.size()) output << ',';
        output << '\n';
    }
    output << "  ]\n}\n";
}

} // namespace

CapturedWorldRunSummary runCapturedWorldResearchDemo(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& outputDirectory,
    const CapturedWorldRunSettings& settings) {
    validateSettings(settings);
    const auto bundle = capture::loadAndValidateCapturedDeformableBundle(manifestPath);
    capture::validateCapturedObservationTrajectoryContract(bundle.dataset);
    prepareOutputDirectory(outputDirectory);

    const auto inputDirectory = outputDirectory / "input";
    const auto calibrationDirectory = outputDirectory / "calibration";
    const auto robustnessDirectory = outputDirectory / "robustness";
    const auto influenceDirectory = outputDirectory / "influence";
    const auto rewriteDirectory = outputDirectory / "rewrite";
    const auto appearanceDirectory = outputDirectory / "appearance";
    std::filesystem::create_directories(inputDirectory);
    std::filesystem::create_directories(calibrationDirectory);
    std::filesystem::create_directories(robustnessDirectory);
    std::filesystem::create_directories(influenceDirectory);
    std::filesystem::create_directories(rewriteDirectory);
    std::filesystem::create_directories(appearanceDirectory);

    writeText(inputDirectory / "validated_manifest.txt",
              capture::writeCapturedDeformableBundleManifest(bundle.manifest));
    writeText(inputDirectory / "source_manifest_sha256.txt", core::sha256FileHex(manifestPath) + "\n");
    gaussian::write3dgsPly(bundle.appearance, appearanceDirectory / "before.ply");

    std::vector<std::size_t> activeIndices(bundle.appearance.size());
    std::iota(activeIndices.begin(), activeIndices.end(), 0U);
    const double cellSize = settings.cellSize.value_or(
        characteristicParticleSpacing(bundle.dataset.particles) * (2.0 / 3.0));
    const auto grid = makeGrid(bundle.dataset, cellSize);

    const auto calibration = calibrateCapturedMaterialGrid(
        bundle.appearance,
        activeIndices,
        bundle.dataset,
        grid,
        makeWorldSettings(bundle.dataset, bundle.manifest.timeStep, 1.5e4, 0.30),
        settings.youngModulusCandidates,
        settings.poissonRatioCandidates);
    const auto& selected = calibration.candidates.at(calibration.selectedIndex);
    writeCapturedMaterialCalibrationCsv(calibration, calibrationDirectory / "material_grid.csv");
    writeCapturedReplaySamplesCsv(calibration.selectedReplay, calibrationDirectory / "selected_samples.csv");
    writeCapturedReplaySummaryCsv(calibration.selectedReplay, calibrationDirectory / "selected_summary.csv");
    writeNonlinearDeformableWorldEvidenceCsv(
        calibration.selectedReplay.simulation, calibrationDirectory / "selected_evidence.csv");

    const auto selectedWorldSettings = makeWorldSettings(
        bundle.dataset, bundle.manifest.timeStep, selected.youngModulus, selected.poissonRatio);
    CapturedMaterialInfluenceSettings influenceSettings;
    influenceSettings.objectiveMarkerId = settings.objectiveMarkerId;
    influenceSettings.objectiveTime = settings.objectiveTime;
    influenceSettings.objectiveDirection = settings.objectiveDirection;
    influenceSettings.finiteDifferenceScaleStep = settings.finiteDifferenceScaleStep;
    influenceSettings.verificationScaleDelta = settings.rewriteScaleDelta;

    const auto robustnessScenarios = makeRobustnessScenarios(bundle, settings.robustnessSeed);
    const auto robustness = evaluateCapturedObservationRobustness(
        bundle.appearance,
        activeIndices,
        bundle.dataset,
        grid,
        makeWorldSettings(bundle.dataset, bundle.manifest.timeStep, selected.youngModulus, selected.poissonRatio),
        settings.youngModulusCandidates,
        settings.poissonRatioCandidates,
        influenceSettings,
        settings.adaptiveSettings,
        robustnessScenarios);
    writeCapturedObservationRobustnessCsv(robustness, robustnessDirectory / "robustness.csv");
    writeRobustnessScenarios(robustnessScenarios, robustnessDirectory / "scenarios.csv");

    const auto referenceRegions = makeOctants(bundle.dataset);
    const auto reference = computeCapturedMaterialInfluenceReference(
        bundle.appearance, activeIndices, bundle.dataset, grid, selectedWorldSettings,
        referenceRegions, influenceSettings);
    const auto adjoint = computeCapturedMaterialInfluenceAdjoint(
        bundle.appearance, activeIndices, bundle.dataset, grid, selectedWorldSettings,
        referenceRegions, influenceSettings);
    const auto comparison = compareCapturedMaterialInfluenceDerivatives(reference, adjoint);
    const auto adaptiveProposal = proposeCapturedMaterialInfluenceRegions(
        bundle.dataset, adjoint, settings.adaptiveSettings);
    const auto adaptiveAdjoint = aggregateCapturedMaterialInfluenceAdjoint(
        bundle.dataset, adjoint, adaptiveProposal.regions);
    const auto adaptiveReference = computeCapturedMaterialInfluenceReference(
        bundle.appearance, activeIndices, bundle.dataset, grid, selectedWorldSettings,
        adaptiveProposal.regions, influenceSettings);
    const auto adaptiveComparison = compareCapturedMaterialInfluenceDerivatives(
        adaptiveReference, adaptiveAdjoint);

    writeCapturedMaterialInfluenceCsv(reference, influenceDirectory / "reference.csv");
    writeCapturedMaterialCounterfactualCsv(reference, influenceDirectory / "counterfactual.csv");
    writeCapturedMaterialAdjointInfluenceCsv(adjoint, influenceDirectory / "adjoint_influence.csv");
    writeCapturedMaterialParticleAdjointCsv(adjoint, influenceDirectory / "particle_adjoint.csv");
    writeCapturedMaterialInfluenceDerivativeComparisonCsv(
        comparison, influenceDirectory / "derivative_comparison.csv");
    writeCapturedMaterialAdaptiveRegionProposalSummaryCsv(
        adaptiveProposal, influenceDirectory / "adaptive_proposal_summary.csv");
    writeCapturedMaterialInfluenceCsv(adaptiveReference, influenceDirectory / "adaptive_reference.csv");
    writeCapturedMaterialCounterfactualCsv(
        adaptiveReference, influenceDirectory / "adaptive_counterfactual.csv");
    writeCapturedMaterialAdjointInfluenceCsv(
        adaptiveAdjoint, influenceDirectory / "adaptive_adjoint.csv");
    writeCapturedMaterialInfluenceDerivativeComparisonCsv(
        adaptiveComparison, influenceDirectory / "adaptive_derivative_comparison.csv");

    const auto& rewriteRegion = adaptiveProposal.regions.front();
    writeSelectedRegion(rewriteRegion, influenceDirectory / "selected_rewrite_region.csv");

    constexpr world::EntityId rewriteEntity = 1U;
    world::WorldIR worldState;
    worldState.id = bundle.manifest.id + "-captured-world-run";
    worldState.appearance = bundle.appearance;
    worldState.entities.push_back({
        rewriteEntity,
        "adaptive-material-rewrite-region",
        std::nullopt,
        {{"young_modulus", selected.youngModulus}},
        {},
    });
    world::WorldCorrespondenceGraph graph;
    for (const auto id : rewriteRegion.particleIds)
        graph.bindPhysical(rewriteEntity, {world::PhysicalKind::MpmParticle, id, 1.0});

    CapturedMaterialRewriteVerifierSettings verifierSettings;
    verifierSettings.activeGaussianIndices = activeIndices;
    verifierSettings.dataset = bundle.dataset;
    verifierSettings.grid = grid;
    verifierSettings.worldSettings = selectedWorldSettings;
    verifierSettings.influenceSettings = influenceSettings;
    verifierSettings.evidenceDirectory = rewriteDirectory / "physical_evidence";

    const double rewrittenYoung = selected.youngModulus * (1.0 + settings.rewriteScaleDelta);
    const world::WorldTransaction transaction{
        "captured-world-run-material-rewrite",
        "vulkax captured-world-run",
        "adaptive solver-backed local captured material rewrite",
        {world::SetMaterialParameter{rewriteEntity, "young_modulus", rewrittenYoung}},
        0U,
    };
    const auto verifier = makeCapturedMaterialRewriteVerifier(std::move(verifierSettings));
    const auto rewrite = world::executeVerifiedRewrite(worldState, graph, transaction, {}, verifier);
    {
        std::ofstream evidence(rewriteDirectory / "transaction_evidence.csv");
        if (!evidence) throw std::runtime_error("failed to write captured-world-run transaction evidence");
        world::writeVerifiedRewriteEvidenceCsv(evidence, rewrite);
    }
    writeTransactionSummary(
        rewriteDirectory / "transaction_summary.csv", rewrite,
        selected.youngModulus, rewrittenYoung, rewriteRegion);
    writeProvenance(worldState, rewriteDirectory / "provenance.csv");
    gaussian::write3dgsPly(worldState.appearance, appearanceDirectory / "rewritten.ply");

    CapturedWorldRunSummary summary;
    summary.bundleId = bundle.manifest.id;
    summary.sourceKind = bundle.manifest.sourceKind;
    summary.appearanceGaussians = bundle.appearance.size();
    summary.physicalParticles = bundle.dataset.particles.size();
    summary.observations = bundle.dataset.observations.size();
    summary.selectedYoungModulus = selected.youngModulus;
    summary.selectedPoissonRatio = selected.poissonRatio;
    summary.fitDynamicRms = selected.fitDynamicRms;
    summary.validationDynamicRms = selected.validationDynamicRms;
    summary.robustnessScenarioCount = robustness.scenarios.size();
    summary.adaptiveRegionCount = adaptiveProposal.regions.size();
    summary.adaptiveParticleCount = adaptiveProposal.proposedParticleCount;
    summary.adaptiveAbsoluteGradientFraction = adaptiveProposal.proposedAbsoluteGradientFraction;
    summary.rewriteRegionId = rewriteRegion.id;
    summary.rewriteParticleCount = rewriteRegion.particleIds.size();
    summary.rewriteStatus = rewrite.status;
    summary.rollbackPerformed = rewrite.rollbackPerformed;
    summary.physicalObservableError = rewrite.evidence.physicalObservableError;
    summary.physicalObservableTolerance = rewrite.evidence.physicalObservableTolerance;

    if (settings.render) {
        const auto renderDirectory = outputDirectory / "render";
        std::filesystem::create_directories(renderDirectory);
        const auto renderBackend = selectRenderBackend(settings.renderBackend);
        const auto renderSettings = makeRenderSettings(
            bundle.appearance, settings.renderWidth, settings.renderHeight);
        const auto beforeRender = render::renderGaussianCloudHeadless(
            renderBackend, bundle.appearance, renderSettings);
        const auto afterRender = render::renderGaussianCloudHeadless(
            renderBackend, worldState.appearance, renderSettings);
        render::writePpm(beforeRender.image, (renderDirectory / "before.ppm").string());
        render::writePpm(afterRender.image, (renderDirectory / "after.ppm").string());
        summary.renderComparison = render::compareImages(beforeRender.image, afterRender.image);
        summary.renderProduced = true;
        summary.renderBackend = renderBackend;
        writeRenderComparison(
            renderDirectory / "comparison.csv", renderBackend,
            beforeRender, afterRender, summary.renderComparison);
    }

    writeRunSummary(outputDirectory / "run_summary.csv", summary);
    const auto artifacts = collectArtifacts(outputDirectory);
    summary.artifactCount = artifacts.size();
    writeCertificate(
        outputDirectory / "certificate.json", manifestPath, bundle, settings, summary, artifacts);
    return summary;
}

} // namespace vulkax::research
