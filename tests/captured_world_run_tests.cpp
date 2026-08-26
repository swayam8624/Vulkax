#include "vulkax/cli/captured_example.hpp"
#include "vulkax/research/captured_world_run.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

int main() {
    namespace fs = std::filesystem;
    using namespace vulkax;

    const fs::path root = fs::temp_directory_path() / "vulkax_captured_world_run_tests";
    fs::remove_all(root);
    const fs::path bundleDirectory = root / "bundle";
    const fs::path outputDirectory = root / "run";

    std::string program = "vulkax";
    std::string command = "captured-deformable-generate-example";
    std::string bundlePath = bundleDirectory.string();
    std::array<char*, 3> argv{program.data(), command.data(), bundlePath.data()};
    assert(cli::capturedExampleCommand(static_cast<int>(argv.size()), argv.data()) == 0);

    research::CapturedWorldRunSettings settings;
    settings.objectiveMarkerId = "m4";
    settings.objectiveTime = 0.003;
    settings.objectiveDirection = {1.0, 1.0, 1.0};
    settings.cellSize = 0.08;
    settings.finiteDifferenceScaleStep = 0.01;
    settings.rewriteScaleDelta = 0.02;
    settings.robustnessSeed = 12345U;
    settings.youngModulusCandidates = {15000.0};
    settings.poissonRatioCandidates = {0.30};
    settings.render = false;

    const auto summary = research::runCapturedWorldResearchDemo(
        bundleDirectory / "capture.vkcap", outputDirectory, settings);
    assert(summary.bundleId == "vulkax-controlled-captured-deformable-v1");
    assert(summary.sourceKind == capture::CapturedSourceKind::Synthetic);
    assert(summary.appearanceGaussians == 5U);
    assert(summary.physicalParticles == 64U);
    assert(summary.observations == 20U);
    assert(std::abs(summary.selectedYoungModulus - 15000.0) < 1.0e-12);
    assert(std::abs(summary.selectedPoissonRatio - 0.30) < 1.0e-12);
    assert(summary.robustnessScenarioCount == 3U);
    assert(summary.adaptiveRegionCount > 0U);
    assert(summary.adaptiveParticleCount > 0U);
    assert(summary.adaptiveParticleCount < summary.physicalParticles);
    assert(summary.adaptiveAbsoluteGradientFraction >= 0.90);
    assert(summary.rewriteRegionId == "adaptive_0");
    assert(summary.rewriteParticleCount > 0U);
    assert(summary.rewriteStatus == world::RewriteVerificationStatus::Verified);
    assert(!summary.rollbackPerformed);
    assert(summary.physicalObservableError <= summary.physicalObservableTolerance);
    assert(!summary.renderProduced);
    assert(!summary.renderBackend.has_value());
    assert(summary.artifactCount >= 20U);

    const std::array<fs::path, 15> required{
        outputDirectory / "input/validated_manifest.txt",
        outputDirectory / "calibration/material_grid.csv",
        outputDirectory / "calibration/selected_summary.csv",
        outputDirectory / "robustness/robustness.csv",
        outputDirectory / "robustness/scenarios.csv",
        outputDirectory / "influence/particle_adjoint.csv",
        outputDirectory / "influence/adaptive_proposal_summary.csv",
        outputDirectory / "influence/selected_rewrite_region.csv",
        outputDirectory / "rewrite/transaction_evidence.csv",
        outputDirectory / "rewrite/transaction_summary.csv",
        outputDirectory / "rewrite/provenance.csv",
        outputDirectory / "rewrite/physical_evidence/reference.csv",
        outputDirectory / "appearance/before.ply",
        outputDirectory / "appearance/rewritten.ply",
        outputDirectory / "certificate.json",
    };
    for (const auto& path : required) assert(fs::is_regular_file(path));
    assert(!fs::exists(outputDirectory / "render"));

    std::ifstream certificateStream(outputDirectory / "certificate.json");
    assert(certificateStream);
    const std::string certificate{
        std::istreambuf_iterator<char>(certificateStream), std::istreambuf_iterator<char>()};
    assert(certificate.find("\"schema\": \"vulkax_captured_world_run\"") != std::string::npos);
    assert(certificate.find("\"status\": \"verified\"") != std::string::npos);
    assert(certificate.find("\"source_kind\": \"synthetic\"") != std::string::npos);
    assert(certificate.find("appearance/rewritten.ply") != std::string::npos);
    assert(certificate.find("rewrite/transaction_evidence.csv") != std::string::npos);
    assert(certificate.find("certificate.json\"") == std::string::npos);

    bool rejectedNonEmptyOutput = false;
    try {
        (void)research::runCapturedWorldResearchDemo(
            bundleDirectory / "capture.vkcap", outputDirectory, settings);
    } catch (const std::invalid_argument&) {
        rejectedNonEmptyOutput = true;
    }
    assert(rejectedNonEmptyOutput);

    fs::remove_all(root);
    return 0;
}
