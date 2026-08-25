#include "vulkax/capture/deformable_bundle.hpp"
#include "vulkax/core/sha256.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <string>
#include <vector>

namespace {

using namespace vulkax;

void writeAppearance(const std::filesystem::path& path) {
    std::ofstream stream(path);
    assert(stream);
    stream << "ply\n"
              "format ascii 1.0\n"
              "element vertex 1\n"
              "property float x\n"
              "property float y\n"
              "property float z\n"
              "property float f_dc_0\n"
              "property float f_dc_1\n"
              "property float f_dc_2\n"
              "property float opacity\n"
              "property float scale_0\n"
              "property float scale_1\n"
              "property float scale_2\n"
              "property float rot_0\n"
              "property float rot_1\n"
              "property float rot_2\n"
              "property float rot_3\n"
              "end_header\n"
              "0 0 0 0 0 0 4 -3 -3 -3 1 0 0 0\n";
    assert(stream);
}

void writeParticles(const std::filesystem::path& path) {
    std::ofstream stream(path);
    assert(stream);
    stream << "particle_id,rest_x,rest_y,rest_z,mass,rest_volume\n"
              "1,0,0,0,1,0.001\n"
              "2,1,0,0,1,0.001\n"
              "3,0,1,0,1,0.001\n"
              "4,0,0,1,1,0.001\n";
    assert(stream);
}

void writeObservations(
    const std::filesystem::path& path,
    double dynamicTime = 0.1,
    bool changeMarkerParticle = false) {
    std::ofstream stream(path);
    assert(stream);
    stream << std::setprecision(17)
           << "marker_id,particle_id,time,x,y,z,split\n"
              "m0,1,0,0,0,0,fit\n"
              "m1,2,0,1,0,0,fit\n"
              "m2,3,0,0,1,0,fit\n"
              "m3,4,0,0,0,1,fit\n"
           << "m0," << (changeMarkerParticle ? 2 : 1) << ',' << dynamicTime << ",0.01,0,0,fit\n"
           << "m1,2," << dynamicTime << ",1.01,0,0,fit\n"
           << "m2,3," << dynamicTime << ",0,1.01,0,validation\n"
           << "m3,4," << dynamicTime << ",0,0,1.01,validation\n";
    assert(stream);
}

std::vector<capture::CapturedObservationUncertainty> makeUncertainty(double dynamicTime = 0.1) {
    std::vector<capture::CapturedObservationUncertainty> result;
    for (const auto& marker : {"m0", "m1", "m2", "m3"})
        result.push_back({marker, 0.0, {0.0, 0.0, 0.0}});
    for (const auto& marker : {"m0", "m1", "m2", "m3"})
        result.push_back({marker, dynamicTime, {1.0e-6, 1.0e-6, 1.0e-6}});
    return result;
}

capture::CapturedDeformableBundleManifest makeManifest() {
    capture::CapturedDeformableBundleManifest manifest;
    manifest.id = "captured-bundle-test";
    manifest.appearanceFile = "object.ply";
    manifest.particlesFile = "particles.csv";
    manifest.observationsFile = "observations.csv";
    manifest.uncertaintyFile = "uncertainty.csv";
    manifest.lengthUnit = "m";
    manifest.massUnit = "kg";
    manifest.timeUnit = "s";
    manifest.coordinateFrame = "test-world";
    manifest.axisConvention = "right-handed-y-up";
    manifest.timeStep = 0.1;
    manifest.sourceKind = capture::CapturedSourceKind::Synthetic;
    manifest.sourceDescription = "controlled captured bundle regression";
    return manifest;
}

template <class Function>
void expectThrow(Function&& function) {
    bool threw = false;
    try {
        std::forward<Function>(function)();
    } catch (const std::exception&) {
        threw = true;
    }
    assert(threw);
}

} // namespace

int main() {
    using namespace vulkax;

    assert(core::sha256Hex("") ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(core::sha256Hex("abc") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(core::isSha256Hex(core::sha256Hex("abc")));
    assert(!core::isSha256Hex("abc"));

    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path() /
                           ("vulkax_captured_bundle_tests_" + unique);
    std::filesystem::create_directories(directory);

    const auto appearancePath = directory / "object.ply";
    const auto particlesPath = directory / "particles.csv";
    const auto observationsPath = directory / "observations.csv";
    const auto uncertaintyPath = directory / "uncertainty.csv";
    const auto manifestPath = directory / "capture.vkcap";

    writeAppearance(appearancePath);
    writeParticles(particlesPath);
    writeObservations(observationsPath);
    capture::writeCapturedObservationUncertaintyCsv(makeUncertainty(), uncertaintyPath);

    auto manifest = makeManifest();
    capture::refreshCapturedDeformableBundleHashes(manifest, directory);
    capture::saveCapturedDeformableBundleManifest(manifest, manifestPath);

    const auto parsed = capture::loadCapturedDeformableBundleManifest(manifestPath);
    assert(parsed.schemaVersion == 1U);
    assert(parsed.id == manifest.id);
    assert(parsed.appearanceSha256 == core::sha256FileHex(appearancePath));
    assert(capture::writeCapturedDeformableBundleManifest(parsed) ==
           capture::writeCapturedDeformableBundleManifest(manifest));

    const auto bundle = capture::loadAndValidateCapturedDeformableBundle(manifestPath);
    assert(bundle.appearance.size() == 1U);
    assert(bundle.dataset.particles.size() == 4U);
    assert(bundle.dataset.observations.size() == 8U);
    assert(bundle.uncertainty.size() == 8U);
    assert(bundle.manifest.sourceKind == capture::CapturedSourceKind::Synthetic);
    capture::validateCapturedObservationTrajectoryContract(bundle.dataset);

    {
        auto incompleteTrajectory = bundle.dataset;
        incompleteTrajectory.observations.erase(
            std::remove_if(
                incompleteTrajectory.observations.begin(),
                incompleteTrajectory.observations.end(),
                [](const auto& observation) {
                    return observation.markerId == "m3" && observation.time > 0.0;
                }),
            incompleteTrajectory.observations.end());
        expectThrow([&] {
            capture::validateCapturedObservationTrajectoryContract(incompleteTrajectory);
        });
    }

    {
        auto inconsistentSplit = bundle.dataset;
        inconsistentSplit.observations.push_back({
            "m0", 1U, 0.2, {0.02, 0.0, 0.0}, capture::ObservationSplit::Validation,
        });
        expectThrow([&] {
            capture::validateCapturedObservationTrajectoryContract(inconsistentSplit);
        });
    }

    {
        std::ofstream stream(observationsPath, std::ios::app);
        stream << "# mutation\n";
    }
    expectThrow([&] { (void)capture::loadAndValidateCapturedDeformableBundle(manifestPath); });

    writeObservations(observationsPath, 0.15);
    capture::writeCapturedObservationUncertaintyCsv(makeUncertainty(0.15), uncertaintyPath);
    capture::refreshCapturedDeformableBundleHashes(manifest, directory);
    capture::saveCapturedDeformableBundleManifest(manifest, manifestPath);
    expectThrow([&] { (void)capture::loadAndValidateCapturedDeformableBundle(manifestPath); });

    writeObservations(observationsPath, 0.1, true);
    capture::writeCapturedObservationUncertaintyCsv(makeUncertainty(), uncertaintyPath);
    capture::refreshCapturedDeformableBundleHashes(manifest, directory);
    capture::saveCapturedDeformableBundleManifest(manifest, manifestPath);
    expectThrow([&] { (void)capture::loadAndValidateCapturedDeformableBundle(manifestPath); });

    writeObservations(observationsPath);
    auto incompleteUncertainty = makeUncertainty();
    incompleteUncertainty.pop_back();
    capture::writeCapturedObservationUncertaintyCsv(incompleteUncertainty, uncertaintyPath);
    capture::refreshCapturedDeformableBundleHashes(manifest, directory);
    capture::saveCapturedDeformableBundleManifest(manifest, manifestPath);
    expectThrow([&] { (void)capture::loadAndValidateCapturedDeformableBundle(manifestPath); });

    capture::writeCapturedObservationUncertaintyCsv(makeUncertainty(), uncertaintyPath);
    capture::refreshCapturedDeformableBundleHashes(manifest, directory);
    manifest.lengthUnit = "mm";
    expectThrow([&] { capture::saveCapturedDeformableBundleManifest(manifest, manifestPath); });
    manifest.lengthUnit = "m";
    manifest.appearanceFile = "../object.ply";
    expectThrow([&] { capture::saveCapturedDeformableBundleManifest(manifest, manifestPath); });

    std::filesystem::remove_all(directory);
    return 0;
}
