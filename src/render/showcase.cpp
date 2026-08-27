#include "vulkax/render/showcase.hpp"

#include "vulkax/core/sha256.hpp"
#include "vulkax/render/gaussian.hpp"
#include "vulkax/render/headless.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vulkax::render {
namespace {

struct Bounds {
    math::Vec3 minimum{};
    math::Vec3 maximum{};
};

[[nodiscard]] Bounds boundsOf(const gaussian::GaussianCloud& cloud) {
    if (cloud.empty()) throw std::invalid_argument("showcase requires a non-empty Gaussian cloud");
    Bounds bounds{
        {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
         std::numeric_limits<double>::infinity()},
        {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
         -std::numeric_limits<double>::infinity()},
    };
    for (const auto& splat : cloud.splats) {
        bounds.minimum.x = std::min(bounds.minimum.x, splat.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, splat.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, splat.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, splat.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, splat.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, splat.position.z);
    }
    return bounds;
}

[[nodiscard]] double spanOf(const Bounds& bounds) {
    const math::Vec3 extent = bounds.maximum - bounds.minimum;
    return std::max({extent.x, extent.y, extent.z, 1.0e-3});
}

[[nodiscard]] std::array<double, 3> shDcForRgb(double red, double green, double blue) {
    constexpr double sh0 = 0.28209479177387814;
    return {(red - 0.5) / sh0, (green - 0.5) / sh0, (blue - 0.5) / sh0};
}

void addPropSplat(
    gaussian::GaussianCloud& cloud,
    math::Vec3 position,
    math::Vec3 scale,
    std::array<double, 3> rgb,
    std::uint32_t localId) {
    gaussian::GaussianSplat splat;
    splat.position = position;
    splat.logScale = {
        std::log(std::max(scale.x, 1.0e-7)),
        std::log(std::max(scale.y, 1.0e-7)),
        std::log(std::max(scale.z, 1.0e-7)),
    };
    splat.rotation = {1.0, 0.0, 0.0, 0.0};
    splat.opacityLogit = 5.0;
    splat.shDC = shDcForRgb(rgb[0], rgb[1], rgb[2]);
    splat.shRest.assign(cloud.shRestCoefficientsPerSplat, 0.0);
    splat.id = {0x56584D4FU, localId};
    cloud.splats.push_back(std::move(splat));
}

[[nodiscard]] gaussian::GaussianCloud addPresentationProps(
    const gaussian::GaussianCloud& input,
    const std::string& preset) {
    if (preset != "studio_pedestal" && preset != "cloth_showcase")
        throw std::invalid_argument("unknown Vulkax showcase scene preset: " + preset);

    gaussian::GaussianCloud result = input;
    const auto bounds = boundsOf(input);
    const double span = spanOf(bounds);
    const math::Vec3 center = (bounds.minimum + bounds.maximum) * 0.5;
    std::uint32_t id = 1U;

    const double floorY = bounds.minimum.y - 0.18 * span;
    constexpr int gridRadius = 3;
    const double spacing = 0.48 * span;
    for (int x = -gridRadius; x <= gridRadius; ++x) {
        for (int z = -gridRadius; z <= gridRadius; ++z) {
            const double checker = ((x + z) & 1) == 0 ? 0.115 : 0.135;
            addPropSplat(
                result,
                {center.x + static_cast<double>(x) * spacing,
                 floorY,
                 center.z + static_cast<double>(z) * spacing},
                {0.30 * span, 0.012 * span, 0.30 * span},
                {checker, checker, checker + 0.01},
                id++);
        }
    }

    if (preset == "studio_pedestal") {
        const double pedestalY = bounds.minimum.y - 0.075 * span;
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                addPropSplat(
                    result,
                    {center.x + static_cast<double>(x) * 0.13 * span,
                     pedestalY,
                     center.z + static_cast<double>(z) * 0.13 * span},
                    {0.105 * span, 0.055 * span, 0.105 * span},
                    {0.29, 0.30, 0.32},
                    id++);
            }
        }
    }

    return result;
}

[[nodiscard]] GaussianRenderSettings cameraSettings(
    const gaussian::GaussianCloud& subject,
    const GaussianShowcaseSettings& settings,
    double azimuthDegrees,
    double elevationDegrees,
    double distanceScale) {
    const auto bounds = boundsOf(subject);
    const math::Vec3 center = (bounds.minimum + bounds.maximum) * 0.5;
    const double span = spanOf(bounds);
    constexpr double pi = 3.14159265358979323846;
    const double azimuth = azimuthDegrees * pi / 180.0;
    const double elevation = elevationDegrees * pi / 180.0;
    const double radius = distanceScale * span;
    const double horizontal = radius * std::cos(elevation);

    GaussianRenderSettings renderSettings;
    renderSettings.image.width = settings.width;
    renderSettings.image.height = settings.height;
    renderSettings.image.clearColor =
        settings.scenePreset == "cloth_showcase"
            ? visualization::Color{0.025F, 0.028F, 0.034F, 1.0F}
            : visualization::Color{0.045F, 0.048F, 0.056F, 1.0F};
    renderSettings.camera.target = center;
    renderSettings.camera.position = {
        center.x + horizontal * std::sin(azimuth),
        center.y + radius * std::sin(elevation),
        center.z + horizontal * std::cos(azimuth),
    };
    renderSettings.camera.up = {0.0, 1.0, 0.0};
    renderSettings.camera.verticalFovDegrees = 45.0;
    renderSettings.nearPlane = std::max(1.0e-5, span * 1.0e-5);
    return renderSettings;
}

[[nodiscard]] ImageRGBA8 sideBySide(const ImageRGBA8& left, const ImageRGBA8& right) {
    if (left.height != right.height)
        throw std::invalid_argument("showcase contact-sheet images must have equal height");
    ImageRGBA8 output;
    output.width = left.width + right.width;
    output.height = left.height;
    output.pixels.resize(static_cast<std::size_t>(output.width) * output.height * 4U);
    for (std::uint32_t y = 0; y < output.height; ++y) {
        const auto copyRow = [&](const ImageRGBA8& source, std::uint32_t xOffset) {
            for (std::uint32_t x = 0; x < source.width; ++x) {
                const std::size_t sourceIndex = (static_cast<std::size_t>(y) * source.width + x) * 4U;
                const std::size_t outputIndex =
                    (static_cast<std::size_t>(y) * output.width + xOffset + x) * 4U;
                for (std::size_t channel = 0; channel < 4U; ++channel)
                    output.pixels[outputIndex + channel] = source.pixels[sourceIndex + channel];
            }
        };
        copyRow(left, 0U);
        copyRow(right, left.width);
    }
    return output;
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

void writeSummarySvg(
    const std::filesystem::path& path,
    const std::string& preset,
    const std::string& rewriteStatus,
    backend::BackendKind backendKind,
    std::uint32_t turntableFrames) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write Vulkax showcase summary card");
    const bool rejected = rewriteStatus == "rejected";
    output << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"720\" viewBox=\"0 0 1280 720\">\n"
           << "<rect width=\"1280\" height=\"720\" fill=\"#101218\"/>\n"
           << "<rect x=\"70\" y=\"70\" width=\"1140\" height=\"580\" rx=\"32\" fill=\"#191d26\" stroke=\"#3b4252\"/>\n"
           << "<text x=\"120\" y=\"165\" fill=\"#f4f6fb\" font-family=\"sans-serif\" font-size=\"54\" font-weight=\"700\">VULKAX 0.80 SHOWCASE</text>\n"
           << "<text x=\"120\" y=\"225\" fill=\"#b8c0d0\" font-family=\"sans-serif\" font-size=\"28\">Verified Rewritable Reality — research evidence + presentation layer</text>\n"
           << "<text x=\"120\" y=\"315\" fill=\"#dce2ee\" font-family=\"monospace\" font-size=\"28\">scene: " << preset << "</text>\n"
           << "<text x=\"120\" y=\"360\" fill=\"#dce2ee\" font-family=\"monospace\" font-size=\"28\">backend: " << backend::toString(backendKind) << "</text>\n"
           << "<text x=\"120\" y=\"405\" fill=\"#dce2ee\" font-family=\"monospace\" font-size=\"28\">turntable views: " << turntableFrames << "</text>\n"
           << "<text x=\"120\" y=\"475\" fill=\"" << (rejected ? "#ffcf70" : "#8ee6a6")
           << "\" font-family=\"sans-serif\" font-size=\"38\" font-weight=\"700\">REWRITE: "
           << (rejected ? "REJECTED — ROLLBACK STATE SHOWN" : "VERIFIED") << "</text>\n"
           << "<text x=\"120\" y=\"545\" fill=\"#98a2b5\" font-family=\"sans-serif\" font-size=\"22\">Decorative studio props are presentation-only. Stored Gaussian SH appearance is preserved; no physical HDRI relighting claim.</text>\n"
           << "</svg>\n";
}

} // namespace

GaussianShowcaseResult renderGaussianShowcase(
    backend::BackendKind backendKind,
    const gaussian::GaussianCloud& before,
    const gaussian::GaussianCloud& finalState,
    const std::string& rewriteStatus,
    const std::filesystem::path& outputDirectory,
    const GaussianShowcaseSettings& settings) {
    if (!settings.enabled) return {};
    if (settings.width == 0U || settings.height == 0U)
        throw std::invalid_argument("showcase render dimensions must be positive");
    if (settings.turntableFrames == 0U || settings.turntableFrames > 72U)
        throw std::invalid_argument("showcase turntable frame count must lie in [1,72]");
    if (rewriteStatus != "verified" && rewriteStatus != "rejected")
        throw std::invalid_argument("showcase rewrite status must be verified or rejected");

    std::filesystem::create_directories(outputDirectory);
    std::filesystem::create_directories(outputDirectory / "turntable");

    const auto decoratedBefore = addPresentationProps(before, settings.scenePreset);
    const auto decoratedFinal = addPresentationProps(finalState, settings.scenePreset);
    const auto heroCamera = cameraSettings(before, settings, 35.0, 15.0, 2.7);
    const auto heroBefore = renderGaussianCloudHeadless(backendKind, decoratedBefore, heroCamera);
    const auto heroFinal = renderGaussianCloudHeadless(backendKind, decoratedFinal, heroCamera);

    const bool rejected = rewriteStatus == "rejected";
    const std::string beforeName = rejected ? "hero_baseline.ppm" : "hero_before.ppm";
    const std::string finalName = rejected ? "hero_rollback.ppm" : "hero_after.ppm";
    writePpm(heroBefore.image, (outputDirectory / beforeName).string());
    writePpm(heroFinal.image, (outputDirectory / finalName).string());
    writePpm(sideBySide(heroBefore.image, heroFinal.image), (outputDirectory / "contact_sheet.ppm").string());

    if (settings.closeup) {
        const auto detailCamera = cameraSettings(before, settings, 28.0, 10.0, 1.65);
        const auto detailBefore = renderGaussianCloudHeadless(backendKind, decoratedBefore, detailCamera);
        const auto detailFinal = renderGaussianCloudHeadless(backendKind, decoratedFinal, detailCamera);
        writePpm(
            detailBefore.image,
            (outputDirectory / (rejected ? "closeup_baseline.ppm" : "closeup_before.ppm")).string());
        writePpm(
            detailFinal.image,
            (outputDirectory / (rejected ? "closeup_rollback.ppm" : "closeup_after.ppm")).string());
    }

    for (std::uint32_t frame = 0U; frame < settings.turntableFrames; ++frame) {
        const double azimuth = 360.0 * static_cast<double>(frame) /
                               static_cast<double>(settings.turntableFrames);
        const auto camera = cameraSettings(finalState, settings, azimuth, 12.0, 2.8);
        const auto image = renderGaussianCloudHeadless(backendKind, decoratedFinal, camera);
        std::ostringstream name;
        name << "frame_" << std::setw(3) << std::setfill('0') << frame << ".ppm";
        writePpm(image.image, (outputDirectory / "turntable" / name.str()).string());
    }

    if (settings.summaryCard)
        writeSummarySvg(
            outputDirectory / "summary_card.svg",
            settings.scenePreset,
            rewriteStatus,
            backendKind,
            settings.turntableFrames);

    GaussianShowcaseResult result;
    result.produced = true;
    result.scenePreset = settings.scenePreset;
    result.turntableFrames = settings.turntableFrames;
    if (!settings.assetLock.empty() && std::filesystem::is_regular_file(settings.assetLock))
        result.assetLockSha256 = core::sha256FileHex(settings.assetLock);
    const auto environment = settings.assetRoot / "hdri" / "studio_small_03_1k.hdr";
    if (!settings.assetRoot.empty() && std::filesystem::is_regular_file(environment))
        result.environmentAssetSha256 = core::sha256FileHex(environment);

    std::ofstream manifest(outputDirectory / "showcase_manifest.json");
    if (!manifest) throw std::runtime_error("failed to write Vulkax showcase manifest");
    manifest << "{\n"
             << "  \"schema\": \"vulkax_showcase\",\n"
             << "  \"schema_version\": 1,\n"
             << "  \"scene\": \"" << jsonEscape(settings.scenePreset) << "\",\n"
             << "  \"rewrite_status\": \"" << jsonEscape(rewriteStatus) << "\",\n"
             << "  \"rollback_state_shown\": " << (rejected ? "true" : "false") << ",\n"
             << "  \"backend\": \"" << backend::toString(backendKind) << "\",\n"
             << "  \"resolution\": [" << settings.width << ',' << settings.height << "],\n"
             << "  \"turntable_frames\": " << settings.turntableFrames << ",\n"
             << "  \"asset_lock_sha256\": \"" << result.assetLockSha256 << "\",\n"
             << "  \"environment_asset_sha256\": \"" << result.environmentAssetSha256 << "\",\n"
             << "  \"environment_usage\": \"metadata/reference only; no physical relighting of stored Gaussian SH appearance\",\n"
             << "  \"presentation_props\": \"deterministic decorative Gaussian floor/pedestal; excluded from research evidence\"\n"
             << "}\n";

    return result;
}

} // namespace vulkax::render
