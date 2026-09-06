#include "vulkax/render/showcase.hpp"

#include "vulkax/core/sha256.hpp"
#include "vulkax/render/gaussian.hpp"
#include "vulkax/render/headless.hpp"

#include <algorithm>
#include <array>
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
    std::uint32_t localId,
    double opacityLogit = 4.6) {
    gaussian::GaussianSplat splat;
    splat.position = position;
    splat.logScale = {
        std::log(std::max(scale.x, 1.0e-7)),
        std::log(std::max(scale.y, 1.0e-7)),
        std::log(std::max(scale.z, 1.0e-7)),
    };
    splat.rotation = {1.0, 0.0, 0.0, 0.0};
    splat.opacityLogit = opacityLogit;
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

    // Presentation geometry is deliberately made from the same Gaussian primitive
    // as the subject. The denser, lower-contrast floor reads as a continuous studio
    // sweep instead of the coarse checkerboard used by the original 0.80 showcase.
    const double floorY = bounds.minimum.y - 0.17 * span;
    constexpr int gridRadius = 4;
    const double spacing = 0.34 * span;
    for (int x = -gridRadius; x <= gridRadius; ++x) {
        for (int z = -gridRadius; z <= gridRadius; ++z) {
            const double checker = ((x + z) & 1) == 0 ? 0.072 : 0.082;
            addPropSplat(
                result,
                {center.x + static_cast<double>(x) * spacing,
                 floorY,
                 center.z + static_cast<double>(z) * spacing},
                {0.255 * span, 0.010 * span, 0.255 * span},
                {checker, checker + 0.003, checker + 0.012},
                id++,
                4.2);
        }
    }

    if (preset == "studio_pedestal") {
        const double pedestalY = bounds.minimum.y - 0.072 * span;
        constexpr int radius = 4;
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                const double nx = static_cast<double>(x) / static_cast<double>(radius);
                const double nz = static_cast<double>(z) / static_cast<double>(radius);
                const double radial = nx * nx + nz * nz;
                if (radial > 1.12) continue;
                const double highlight = 0.024 * (1.0 - std::min(radial, 1.0));
                addPropSplat(
                    result,
                    {center.x + static_cast<double>(x) * 0.085 * span,
                     pedestalY,
                     center.z + static_cast<double>(z) * 0.085 * span},
                    {0.088 * span, 0.052 * span, 0.088 * span},
                    {0.175 + highlight, 0.182 + highlight, 0.198 + highlight},
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
            ? visualization::Color{0.016F, 0.022F, 0.035F, 1.0F}
            : visualization::Color{0.024F, 0.028F, 0.040F, 1.0F};
    renderSettings.camera.target = center;
    renderSettings.camera.position = {
        center.x + horizontal * std::sin(azimuth),
        center.y + radius * std::sin(elevation),
        center.z + horizontal * std::cos(azimuth),
    };
    renderSettings.camera.up = {0.0, 1.0, 0.0};
    renderSettings.camera.verticalFovDegrees = 42.0;
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
                const std::size_t sourceIndex =
                    (static_cast<std::size_t>(y) * source.width + x) * 4U;
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

[[nodiscard]] ImageRGBA8 polishPresentationImage(const ImageRGBA8& source) {
    ImageRGBA8 output = source;
    if (source.width == 0U || source.height == 0U) return output;

    const double invWidth = 1.0 / static_cast<double>(source.width);
    const double invHeight = 1.0 / static_cast<double>(source.height);
    for (std::uint32_t y = 0U; y < source.height; ++y) {
        for (std::uint32_t x = 0U; x < source.width; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * source.width + x) * 4U;
            double red = static_cast<double>(source.pixels[index]) / 255.0;
            double green = static_cast<double>(source.pixels[index + 1U]) / 255.0;
            double blue = static_cast<double>(source.pixels[index + 2U]) / 255.0;

            // Mild presentation-only grade: preserve the underlying image while
            // improving local separation and keeping highlights from becoming harsh.
            const double luma = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
            constexpr double saturation = 1.055;
            red = luma + (red - luma) * saturation;
            green = luma + (green - luma) * saturation;
            blue = luma + (blue - luma) * saturation;
            constexpr double contrast = 1.045;
            red = (red - 0.5) * contrast + 0.5;
            green = (green - 0.5) * contrast + 0.5;
            blue = (blue - 0.5) * contrast + 0.5;

            const double nx = (static_cast<double>(x) + 0.5) * invWidth * 2.0 - 1.0;
            const double ny = (static_cast<double>(y) + 0.5) * invHeight * 2.0 - 1.0;
            const double radiusSquared = nx * nx + ny * ny;
            const double vignette = 1.0 - 0.085 * std::clamp((radiusSquared - 0.18) / 1.5, 0.0, 1.0);
            red *= vignette * 1.018;
            green *= vignette * 1.012;
            blue *= vignette * 1.026;

            output.pixels[index] = static_cast<std::uint8_t>(
                std::lround(std::clamp(red, 0.0, 1.0) * 255.0));
            output.pixels[index + 1U] = static_cast<std::uint8_t>(
                std::lround(std::clamp(green, 0.0, 1.0) * 255.0));
            output.pixels[index + 2U] = static_cast<std::uint8_t>(
                std::lround(std::clamp(blue, 0.0, 1.0) * 255.0));
        }
    }
    return output;
}

void fillImage(ImageRGBA8& image, std::array<std::uint8_t, 4> rgba) {
    for (std::size_t index = 0U; index < image.pixels.size(); index += 4U) {
        for (std::size_t channel = 0U; channel < 4U; ++channel)
            image.pixels[index + channel] = rgba[channel];
    }
}

void blitImage(ImageRGBA8& destination, const ImageRGBA8& source, std::uint32_t offsetX,
               std::uint32_t offsetY) {
    if (offsetX + source.width > destination.width || offsetY + source.height > destination.height)
        throw std::invalid_argument("showcase image blit exceeds destination bounds");
    for (std::uint32_t y = 0U; y < source.height; ++y) {
        for (std::uint32_t x = 0U; x < source.width; ++x) {
            const std::size_t sourceIndex =
                (static_cast<std::size_t>(y) * source.width + x) * 4U;
            const std::size_t destinationIndex =
                (static_cast<std::size_t>(y + offsetY) * destination.width + offsetX + x) * 4U;
            for (std::size_t channel = 0U; channel < 4U; ++channel)
                destination.pixels[destinationIndex + channel] = source.pixels[sourceIndex + channel];
        }
    }
}

[[nodiscard]] ImageRGBA8 presentationContactSheet(
    const ImageRGBA8& left,
    const ImageRGBA8& right) {
    if (left.width != right.width || left.height != right.height)
        throw std::invalid_argument("presentation contact sheet requires equal image dimensions");
    constexpr std::uint32_t border = 28U;
    constexpr std::uint32_t gutter = 18U;
    ImageRGBA8 output;
    output.width = left.width * 2U + border * 2U + gutter;
    output.height = left.height + border * 2U;
    output.pixels.resize(static_cast<std::size_t>(output.width) * output.height * 4U);
    fillImage(output, {11U, 15U, 23U, 255U});
    blitImage(output, polishPresentationImage(left), border, border);
    blitImage(output, polishPresentationImage(right), border + left.width + gutter, border);
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

[[nodiscard]] std::string htmlEscape(const std::string& text) {
    std::ostringstream output;
    for (const char character : text) {
        switch (character) {
            case '&': output << "&amp;"; break;
            case '<': output << "&lt;"; break;
            case '>': output << "&gt;"; break;
            case '"': output << "&quot;"; break;
            case '\'': output << "&#39;"; break;
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
    const char* statusColor = rejected ? "#f5b85b" : "#75e6b6";
    const char* statusFill = rejected ? "#382d20" : "#16352d";
    output
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"720\" viewBox=\"0 0 1280 720\">\n"
        << "<defs>\n"
        << "<linearGradient id=\"bg\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"><stop stop-color=\"#080b12\"/><stop offset=\"0.55\" stop-color=\"#101827\"/><stop offset=\"1\" stop-color=\"#0b111d\"/></linearGradient>\n"
        << "<radialGradient id=\"glow\" cx=\"76%\" cy=\"20%\" r=\"58%\"><stop stop-color=\"#27486d\" stop-opacity=\".48\"/><stop offset=\"1\" stop-color=\"#27486d\" stop-opacity=\"0\"/></radialGradient>\n"
        << "</defs>\n"
        << "<rect width=\"1280\" height=\"720\" fill=\"url(#bg)\"/><rect width=\"1280\" height=\"720\" fill=\"url(#glow)\"/>\n"
        << "<rect x=\"66\" y=\"58\" width=\"1148\" height=\"604\" rx=\"34\" fill=\"#0d1420\" fill-opacity=\".76\" stroke=\"#26364d\"/>\n"
        << "<text x=\"112\" y=\"128\" fill=\"#8da5c7\" font-family=\"-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif\" font-size=\"19\" font-weight=\"700\" letter-spacing=\"4\">VULKAX / 1.0 VISUAL SYSTEM</text>\n"
        << "<text x=\"112\" y=\"210\" fill=\"#f5f8ff\" font-family=\"-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif\" font-size=\"62\" font-weight=\"760\">Verified Rewritable Reality</text>\n"
        << "<text x=\"112\" y=\"256\" fill=\"#9eabc0\" font-family=\"-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif\" font-size=\"24\">Research evidence stays raw. This layer is deterministic presentation.</text>\n"
        << "<rect x=\"112\" y=\"319\" width=\"310\" height=\"116\" rx=\"21\" fill=\"#121c2a\" stroke=\"#26364d\"/>\n"
        << "<text x=\"142\" y=\"354\" fill=\"#71839d\" font-family=\"sans-serif\" font-size=\"15\" font-weight=\"700\" letter-spacing=\"2\">SCENE</text>\n"
        << "<text x=\"142\" y=\"401\" fill=\"#e8eef9\" font-family=\"monospace\" font-size=\"24\">" << preset << "</text>\n"
        << "<rect x=\"442\" y=\"319\" width=\"280\" height=\"116\" rx=\"21\" fill=\"#121c2a\" stroke=\"#26364d\"/>\n"
        << "<text x=\"472\" y=\"354\" fill=\"#71839d\" font-family=\"sans-serif\" font-size=\"15\" font-weight=\"700\" letter-spacing=\"2\">BACKEND</text>\n"
        << "<text x=\"472\" y=\"401\" fill=\"#e8eef9\" font-family=\"monospace\" font-size=\"24\">" << backend::toString(backendKind) << "</text>\n"
        << "<rect x=\"742\" y=\"319\" width=\"250\" height=\"116\" rx=\"21\" fill=\"#121c2a\" stroke=\"#26364d\"/>\n"
        << "<text x=\"772\" y=\"354\" fill=\"#71839d\" font-family=\"sans-serif\" font-size=\"15\" font-weight=\"700\" letter-spacing=\"2\">VIEWS</text>\n"
        << "<text x=\"772\" y=\"401\" fill=\"#e8eef9\" font-family=\"monospace\" font-size=\"24\">" << turntableFrames << " turntable</text>\n"
        << "<rect x=\"1012\" y=\"319\" width=\"156\" height=\"116\" rx=\"21\" fill=\"" << statusFill << "\" stroke=\"" << statusColor << "\" stroke-opacity=\".45\"/>\n"
        << "<text x=\"1090\" y=\"355\" text-anchor=\"middle\" fill=\"" << statusColor << "\" font-family=\"sans-serif\" font-size=\"14\" font-weight=\"800\" letter-spacing=\"2\">REWRITE</text>\n"
        << "<text x=\"1090\" y=\"400\" text-anchor=\"middle\" fill=\"" << statusColor << "\" font-family=\"sans-serif\" font-size=\"20\" font-weight=\"800\">" << (rejected ? "ROLLED BACK" : "VERIFIED") << "</text>\n"
        << "<line x1=\"112\" y1=\"494\" x2=\"1168\" y2=\"494\" stroke=\"#233247\"/>\n"
        << "<text x=\"112\" y=\"546\" fill=\"#c9d4e5\" font-family=\"sans-serif\" font-size=\"21\" font-weight=\"650\">" << (rejected ? "Verifier rejected the candidate. The rollback state is the displayed final state." : "Independent verification accepted the candidate rewrite.") << "</text>\n"
        << "<text x=\"112\" y=\"595\" fill=\"#7d8da5\" font-family=\"sans-serif\" font-size=\"17\">Decorative Gaussian props and display grading are presentation-only; stored Gaussian SH appearance and research evidence remain unchanged.</text>\n"
        << "</svg>\n";
}

void writeGalleryHtml(
    const std::filesystem::path& path,
    const std::string& preset,
    const std::string& rewriteStatus,
    backend::BackendKind backendKind,
    std::uint32_t turntableFrames,
    bool closeup,
    bool summaryCard) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write Vulkax showcase gallery");
    const bool rejected = rewriteStatus == "rejected";
    const std::string beforeStem = rejected ? "baseline" : "before";
    const std::string finalStem = rejected ? "rollback" : "after";
    const std::string statusText = rejected ? "REJECTED / ROLLBACK" : "VERIFIED";

    output << R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vulkax 1.0 Showcase</title>
<style>
:root{color-scheme:dark;--bg:#070a10;--panel:#0d1420;--panel2:#111b2a;--line:#26364d;--text:#f4f7fd;--muted:#8d9bb0;--accent:#74a9ff;--good:#75e6b6;--warn:#f5b85b}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 78% 0%,#172b45 0,transparent 36rem),linear-gradient(145deg,#070a10,#0a101a 55%,#070a10);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;min-height:100vh}
main{width:min(1480px,calc(100% - 40px));margin:0 auto;padding:58px 0 80px}.eyebrow{font-size:12px;letter-spacing:.22em;font-weight:800;color:#8ca7cb;text-transform:uppercase}.top{display:flex;justify-content:space-between;gap:30px;align-items:flex-end;margin-bottom:30px}.top h1{font-size:clamp(42px,5vw,72px);line-height:.98;letter-spacing:-.045em;margin:13px 0 14px}.lede{max-width:780px;color:#9ba9bd;font-size:17px;line-height:1.65;margin:0}.pill{white-space:nowrap;padding:12px 16px;border-radius:999px;font-size:12px;letter-spacing:.12em;font-weight:850;border:1px solid var(--line);background:#101927}.pill.good{color:var(--good);border-color:#2f6d59;background:#102a23}.pill.warn{color:var(--warn);border-color:#6e5936;background:#2c2418}.meta{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:24px 0 34px}.meta div{background:rgba(17,27,42,.7);border:1px solid var(--line);border-radius:16px;padding:16px 18px}.meta span{display:block;color:#71839d;font-size:10px;font-weight:800;letter-spacing:.18em;text-transform:uppercase;margin-bottom:8px}.meta strong{font:600 14px ui-monospace,SFMono-Regular,Menlo,monospace;color:#dfe7f4}.panel{background:linear-gradient(160deg,rgba(17,27,42,.94),rgba(10,15,24,.94));border:1px solid var(--line);border-radius:24px;padding:18px;box-shadow:0 30px 80px rgba(0,0,0,.24);margin-bottom:18px}.panel img{width:100%;display:block;border-radius:14px;background:#080c13}.section-title{display:flex;align-items:end;justify-content:space-between;gap:20px;margin:44px 2px 16px}.section-title h2{font-size:25px;letter-spacing:-.02em;margin:0}.section-title p{color:var(--muted);font-size:13px;margin:0}.compare{display:grid;grid-template-columns:1fr 1fr;gap:18px}.figure{position:relative}.label{position:absolute;left:14px;top:14px;background:rgba(6,10,16,.78);backdrop-filter:blur(10px);border:1px solid rgba(140,165,199,.28);padding:8px 10px;border-radius:999px;font-size:10px;font-weight:850;letter-spacing:.15em;text-transform:uppercase;color:#c8d5e7}.turntable{display:grid;grid-template-columns:minmax(0,1fr) 310px;gap:18px}.controls{padding:24px;background:#0b121d;border:1px solid var(--line);border-radius:18px;display:flex;flex-direction:column;justify-content:center}.controls h3{margin:0 0 8px;font-size:20px}.controls p{margin:0 0 22px;color:var(--muted);font-size:13px;line-height:1.6}.controls input{width:100%;accent-color:#74a9ff}.frame-readout{font:600 12px ui-monospace,SFMono-Regular,Menlo,monospace;color:#9ab4d8;margin-top:12px}.note{margin-top:34px;padding:18px 20px;border-left:2px solid #3e608e;color:#8fa0b8;background:rgba(15,24,38,.55);border-radius:0 14px 14px 0;font-size:13px;line-height:1.7}footer{margin-top:44px;color:#65758c;font-size:12px;text-align:center}@media(max-width:850px){.top{display:block}.pill{display:inline-block;margin-top:20px}.meta,.compare,.turntable{grid-template-columns:1fr}.turntable{gap:12px}main{width:min(100% - 24px,1480px);padding-top:34px}}
</style>
</head>
<body><main>
<div class="top"><div><div class="eyebrow">Vulkax / 1.0 visual system</div><h1>Verified Rewritable<br>Reality</h1><p class="lede">A presentation surface for the stable research pipeline. Native renderer output remains available as regression evidence; these PNGs and this gallery are presentation-only derivatives.</p></div>
)HTML";
    output << "<div class=\"pill " << (rejected ? "warn" : "good") << "\">"
           << htmlEscape(statusText) << "</div></div>\n";
    output << "<div class=\"meta\"><div><span>Scene</span><strong>" << htmlEscape(preset)
           << "</strong></div><div><span>Backend</span><strong>" << htmlEscape(backend::toString(backendKind))
           << "</strong></div><div><span>Turntable</span><strong>" << turntableFrames
           << " views</strong></div></div>\n";

    if (summaryCard)
        output << "<div class=\"panel\"><img src=\"summary_card.svg\" alt=\"Vulkax 1.0 showcase summary\"></div>\n";

    output << "<div class=\"section-title\"><h2>State comparison</h2><p>Presentation grade / native Gaussian render</p></div>\n"
           << "<div class=\"compare\"><div class=\"panel figure\"><span class=\"label\">"
           << (rejected ? "Baseline" : "Before") << "</span><img src=\"hero_" << beforeStem
           << ".png\" alt=\"Initial state\"></div><div class=\"panel figure\"><span class=\"label\">"
           << (rejected ? "Rollback final" : "Verified final") << "</span><img src=\"hero_" << finalStem
           << ".png\" alt=\"Final state\"></div></div>\n";

    output << "<div class=\"section-title\"><h2>Comparison plate</h2><p>Framed presentation composite</p></div>\n"
           << "<div class=\"panel\"><img src=\"contact_sheet.png\" alt=\"Vulkax state comparison\"></div>\n";

    if (closeup) {
        output << "<div class=\"section-title\"><h2>Detail pass</h2><p>Closer camera / identical state semantics</p></div>\n"
               << "<div class=\"compare\"><div class=\"panel figure\"><span class=\"label\">Detail "
               << (rejected ? "baseline" : "before") << "</span><img src=\"closeup_" << beforeStem
               << ".png\" alt=\"Initial closeup\"></div><div class=\"panel figure\"><span class=\"label\">Detail "
               << (rejected ? "rollback" : "after") << "</span><img src=\"closeup_" << finalStem
               << ".png\" alt=\"Final closeup\"></div></div>\n";
    }

    output << R"HTML(<div class="section-title"><h2>Turntable</h2><p>Scrub the deterministic view sequence</p></div>
<div class="turntable"><div class="panel"><img id="turntableImage" src="turntable/frame_000.png" alt="Turntable frame"></div><div class="controls"><h3>Inspect the final state</h3><p>Every frame uses the same final committed or rollback state. Only the presentation camera moves.</p><input id="turntableSlider" type="range" min="0" value="0"><div id="frameReadout" class="frame-readout"></div></div></div>
<div class="note">Presentation contract: decorative Gaussian floor/pedestal geometry and the mild display grade are excluded from research evidence. The stored Gaussian SH appearance, physical verification result, transaction verdict and raw PPM regression artifacts remain unchanged.</div>
<footer>Vulkax 1.0 · deterministic offline showcase · no network assets required</footer>
<script>
)HTML";
    output << "const frames=" << turntableFrames << ";\n"
           << "const slider=document.getElementById('turntableSlider');const image=document.getElementById('turntableImage');const readout=document.getElementById('frameReadout');slider.max=Math.max(0,frames-1);function showFrame(){const n=Number(slider.value);const id=String(n).padStart(3,'0');image.src='turntable/frame_'+id+'.png';readout.textContent='frame '+id+' / '+String(frames-1).padStart(3,'0');}slider.addEventListener('input',showFrame);showFrame();\n"
           << "</script></main></body></html>\n";
}

void writePresentationPng(const ImageRGBA8& image, const std::filesystem::path& path) {
    writePng(polishPresentationImage(image), path.string());
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
    const auto heroCamera = cameraSettings(before, settings, 35.0, 14.0, 2.62);
    const auto heroBefore = renderGaussianCloudHeadless(backendKind, decoratedBefore, heroCamera);
    const auto heroFinal = renderGaussianCloudHeadless(backendKind, decoratedFinal, heroCamera);

    const bool rejected = rewriteStatus == "rejected";
    const std::string beforeStem = rejected ? "hero_baseline" : "hero_before";
    const std::string finalStem = rejected ? "hero_rollback" : "hero_after";
    writePpm(heroBefore.image, (outputDirectory / (beforeStem + ".ppm")).string());
    writePpm(heroFinal.image, (outputDirectory / (finalStem + ".ppm")).string());
    writePpm(
        sideBySide(heroBefore.image, heroFinal.image),
        (outputDirectory / "contact_sheet.ppm").string());

    // Additive browser-ready presentation outputs. PPM files above remain the raw
    // compatibility/evidence artifacts required by the frozen 1.0 contract.
    writePresentationPng(heroBefore.image, outputDirectory / (beforeStem + ".png"));
    writePresentationPng(heroFinal.image, outputDirectory / (finalStem + ".png"));
    writePng(
        presentationContactSheet(heroBefore.image, heroFinal.image),
        (outputDirectory / "contact_sheet.png").string());

    if (settings.closeup) {
        const auto detailCamera = cameraSettings(before, settings, 28.0, 9.0, 1.68);
        const auto detailBefore = renderGaussianCloudHeadless(backendKind, decoratedBefore, detailCamera);
        const auto detailFinal = renderGaussianCloudHeadless(backendKind, decoratedFinal, detailCamera);
        const std::string detailBeforeStem = rejected ? "closeup_baseline" : "closeup_before";
        const std::string detailFinalStem = rejected ? "closeup_rollback" : "closeup_after";
        writePpm(detailBefore.image, (outputDirectory / (detailBeforeStem + ".ppm")).string());
        writePpm(detailFinal.image, (outputDirectory / (detailFinalStem + ".ppm")).string());
        writePresentationPng(detailBefore.image, outputDirectory / (detailBeforeStem + ".png"));
        writePresentationPng(detailFinal.image, outputDirectory / (detailFinalStem + ".png"));
    }

    for (std::uint32_t frame = 0U; frame < settings.turntableFrames; ++frame) {
        const double azimuth = 360.0 * static_cast<double>(frame) /
                               static_cast<double>(settings.turntableFrames);
        const auto camera = cameraSettings(finalState, settings, azimuth, 11.5, 2.72);
        const auto image = renderGaussianCloudHeadless(backendKind, decoratedFinal, camera);
        std::ostringstream stem;
        stem << "frame_" << std::setw(3) << std::setfill('0') << frame;
        writePpm(image.image, (outputDirectory / "turntable" / (stem.str() + ".ppm")).string());
        writePresentationPng(image.image, outputDirectory / "turntable" / (stem.str() + ".png"));
    }

    if (settings.summaryCard)
        writeSummarySvg(
            outputDirectory / "summary_card.svg",
            settings.scenePreset,
            rewriteStatus,
            backendKind,
            settings.turntableFrames);
    writeGalleryHtml(
        outputDirectory / "gallery.html",
        settings.scenePreset,
        rewriteStatus,
        backendKind,
        settings.turntableFrames,
        settings.closeup,
        settings.summaryCard);

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
             << "  \"presentation_revision\": 2,\n"
             << "  \"scene\": \"" << jsonEscape(settings.scenePreset) << "\",\n"
             << "  \"rewrite_status\": \"" << jsonEscape(rewriteStatus) << "\",\n"
             << "  \"rollback_state_shown\": " << (rejected ? "true" : "false") << ",\n"
             << "  \"backend\": \"" << backend::toString(backendKind) << "\",\n"
             << "  \"resolution\": [" << settings.width << ',' << settings.height << "],\n"
             << "  \"turntable_frames\": " << settings.turntableFrames << ",\n"
             << "  \"browser_gallery\": \"gallery.html\",\n"
             << "  \"presentation_png\": true,\n"
             << "  \"asset_lock_sha256\": \"" << result.assetLockSha256 << "\",\n"
             << "  \"environment_asset_sha256\": \"" << result.environmentAssetSha256 << "\",\n"
             << "  \"environment_usage\": \"metadata/reference only; no physical relighting of stored Gaussian SH appearance\",\n"
             << "  \"presentation_props\": \"deterministic decorative Gaussian floor/pedestal; excluded from research evidence\",\n"
             << "  \"presentation_grade\": \"deterministic mild contrast/saturation/vignette applied only to PNG derivatives; raw PPM evidence unchanged\"\n"
             << "}\n";

    return result;
}

} // namespace vulkax::render
