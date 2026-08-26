#include "vulkax/capture/deformable_bundle.hpp"
#include "vulkax/core/sha256.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

namespace {

void writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream stream(path, std::ios::binary);
    assert(stream);
    stream << contents;
    assert(stream);
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

    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
                      ("vulkax_bundle_authoring_" + unique);
    const auto bundleDirectory = root / "bundle";
    std::filesystem::create_directories(bundleDirectory / "payloads");

    const auto appearance = bundleDirectory / "payloads" / "object.ply";
    const auto particles = bundleDirectory / "payloads" / "particles.csv";
    const auto observations = bundleDirectory / "payloads" / "observations.csv";
    const auto uncertainty = bundleDirectory / "payloads" / "uncertainty.csv";
    const auto manifestPath = bundleDirectory / "capture.vkcap";

    writeFile(
        appearance,
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 1\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n"
        "property float opacity\n"
        "property float scale_0\nproperty float scale_1\nproperty float scale_2\n"
        "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n"
        "end_header\n"
        "0 0 0 0 0 0 4 -3 -3 -3 1 0 0 0\n");
    writeFile(
        particles,
        "particle_id,rest_x,rest_y,rest_z,mass,rest_volume\n"
        "1,0,0,0,1,0.001\n"
        "2,1,0,0,1,0.001\n"
        "3,0,1,0,1,0.001\n"
        "4,0,0,1,1,0.001\n");
    writeFile(
        observations,
        "marker_id,particle_id,time,x,y,z,split\n"
        "m0,1,0,0,0,0,fit\n"
        "m1,2,0,1,0,0,fit\n"
        "m2,3,0,0,1,0,fit\n"
        "m3,4,0,0,0,1,fit\n"
        "m0,1,0.1,0.01,0,0,fit\n"
        "m1,2,0.1,1.01,0,0,fit\n"
        "m2,3,0.1,0,1.01,0,validation\n"
        "m3,4,0.1,0,0,1.01,validation\n");
    writeFile(
        uncertainty,
        "marker_id,time,sigma_x,sigma_y,sigma_z\n"
        "m0,0,0,0,0\n"
        "m1,0,0,0,0\n"
        "m2,0,0,0,0\n"
        "m3,0,0,0,0\n"
        "m0,0.1,0.0001,0.0001,0.0001\n"
        "m1,0.1,0.0001,0.0001,0.0001\n"
        "m2,0.1,0.0001,0.0001,0.0001\n"
        "m3,0.1,0.0001,0.0001,0.0001\n");

    capture::CapturedDeformableBundleAuthoringRequest request;
    request.manifestPath = manifestPath;
    request.appearancePath = appearance;
    request.particlesPath = particles;
    request.observationsPath = observations;
    request.uncertaintyPath = uncertainty;
    request.id = "measured-authoring-regression";
    request.timeStep = 0.1;
    request.coordinateFrame = "lab-table-frame";
    request.axisConvention = "right-handed-y-up";
    request.sourceKind = capture::CapturedSourceKind::Measured;
    request.sourceDescription = "authoring regression payload; source label supplied explicitly by caller";

    const auto manifest = capture::makeCapturedDeformableBundleManifest(request);
    assert(manifest.appearanceFile.generic_string() == "payloads/object.ply");
    assert(manifest.particlesFile.generic_string() == "payloads/particles.csv");
    assert(manifest.observationsFile.generic_string() == "payloads/observations.csv");
    assert(manifest.uncertaintyFile.generic_string() == "payloads/uncertainty.csv");
    assert(manifest.appearanceSha256 == core::sha256FileHex(appearance));
    assert(manifest.particlesSha256 == core::sha256FileHex(particles));
    assert(manifest.observationsSha256 == core::sha256FileHex(observations));
    assert(manifest.uncertaintySha256 == core::sha256FileHex(uncertainty));
    assert(manifest.sourceKind == capture::CapturedSourceKind::Measured);

    capture::saveCapturedDeformableBundleManifest(manifest, manifestPath);
    const auto bundle = capture::loadAndValidateCapturedDeformableBundle(manifestPath);
    capture::validateCapturedObservationTrajectoryContract(bundle.dataset);
    assert(bundle.dataset.particles.size() == 4U);
    assert(bundle.dataset.observations.size() == 8U);
    assert(bundle.uncertainty.size() == 8U);

    const auto outside = root / "outside.csv";
    writeFile(outside, "outside\n");
    auto outsideRequest = request;
    outsideRequest.uncertaintyPath = outside;
    expectThrow([&] { (void)capture::makeCapturedDeformableBundleManifest(outsideRequest); });

    auto missingRequest = request;
    missingRequest.appearancePath = bundleDirectory / "missing.ply";
    expectThrow([&] { (void)capture::makeCapturedDeformableBundleManifest(missingRequest); });

    auto badMetadata = request;
    badMetadata.sourceDescription.clear();
    expectThrow([&] { (void)capture::makeCapturedDeformableBundleManifest(badMetadata); });

    std::filesystem::remove_all(root);
    return 0;
}
