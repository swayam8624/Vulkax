#include "vulkax/capture/deformable_bundle.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace vulkax::capture {
namespace {

[[nodiscard]] std::filesystem::path normalizedAbsolute(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] std::filesystem::path relativePayloadPath(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& payloadPath,
    const char* label) {
    if (payloadPath.empty())
        throw std::invalid_argument(std::string(label) + " cannot be empty");

    const auto manifestAbsolute = normalizedAbsolute(manifestPath);
    const auto baseDirectory = manifestAbsolute.parent_path();
    const auto payloadAbsolute = normalizedAbsolute(payloadPath);

    std::error_code error;
    if (!std::filesystem::exists(payloadAbsolute, error) || error)
        throw std::invalid_argument(std::string(label) + " does not exist: " + payloadAbsolute.string());
    if (!std::filesystem::is_regular_file(payloadAbsolute, error) || error)
        throw std::invalid_argument(std::string(label) + " is not a regular file: " + payloadAbsolute.string());

    const auto relative = payloadAbsolute.lexically_relative(baseDirectory);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.has_root_directory()) {
        throw std::invalid_argument(
            std::string(label) + " cannot be represented inside the bundle directory");
    }
    for (const auto& component : relative) {
        if (component == "..") {
            throw std::invalid_argument(
                std::string(label) + " must remain inside the bundle directory tree");
        }
    }
    return relative;
}

} // namespace

CapturedDeformableBundleManifest makeCapturedDeformableBundleManifest(
    const CapturedDeformableBundleAuthoringRequest& request) {
    if (request.manifestPath.empty())
        throw std::invalid_argument("captured bundle manifest output path cannot be empty");
    if (request.id.empty())
        throw std::invalid_argument("captured bundle id cannot be empty");
    if (request.coordinateFrame.empty())
        throw std::invalid_argument("captured bundle coordinate frame cannot be empty");
    if (request.axisConvention.empty())
        throw std::invalid_argument("captured bundle axis convention cannot be empty");
    if (request.sourceDescription.empty())
        throw std::invalid_argument("captured bundle source description cannot be empty");

    const auto manifestAbsolute = normalizedAbsolute(request.manifestPath);
    const auto baseDirectory = manifestAbsolute.parent_path();
    if (baseDirectory.empty())
        throw std::invalid_argument("captured bundle manifest must have a parent directory");

    CapturedDeformableBundleManifest manifest;
    manifest.id = request.id;
    manifest.appearanceFile = relativePayloadPath(
        request.manifestPath, request.appearancePath, "appearance payload");
    manifest.particlesFile = relativePayloadPath(
        request.manifestPath, request.particlesPath, "particles payload");
    manifest.observationsFile = relativePayloadPath(
        request.manifestPath, request.observationsPath, "observations payload");
    manifest.uncertaintyFile = relativePayloadPath(
        request.manifestPath, request.uncertaintyPath, "uncertainty payload");
    manifest.lengthUnit = "m";
    manifest.massUnit = "kg";
    manifest.timeUnit = "s";
    manifest.coordinateFrame = request.coordinateFrame;
    manifest.axisConvention = request.axisConvention;
    manifest.timeStep = request.timeStep;
    manifest.sourceKind = request.sourceKind;
    manifest.sourceDescription = request.sourceDescription;

    refreshCapturedDeformableBundleHashes(manifest, baseDirectory);
    return manifest;
}

} // namespace vulkax::capture
