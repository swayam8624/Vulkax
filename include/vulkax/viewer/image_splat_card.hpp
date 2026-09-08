#pragma once

#include "vulkax/viewer/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace vulkax::viewer {

struct ImageSplatCardSettings {
    std::size_t maxSplats{160000U};
    double worldHeight{2.0};
    double radiusMultiplier{0.72};
    double normalThicknessRatio{0.18};
    float alphaThreshold{1.0F / 255.0F};
};

// Convert an RGBA8 image into an explicitly flat 2.5D Gaussian card. Pixel colour
// and alpha are preserved; no depth or unseen geometry is fabricated. Input rows
// are interpreted top-to-bottom and the resulting card uses +Y upward.
[[nodiscard]] inline ViewerScene makeImageSplatCard(
    std::size_t width,
    std::size_t height,
    std::span<const std::uint8_t> rgba,
    const ImageSplatCardSettings& settings = {}) {
    constexpr double sh0 = 0.28209479177387814;
    constexpr std::uint32_t imageNamespace = 0x494d4701U; // "IMG" presentation namespace.

    if (width == 0U || height == 0U) throw std::invalid_argument("image dimensions must be positive");
    if (settings.maxSplats == 0U) throw std::invalid_argument("image maxSplats must be positive");
    if (!(settings.worldHeight > 0.0) || !(settings.radiusMultiplier > 0.0) ||
        !(settings.normalThicknessRatio > 0.0))
        throw std::invalid_argument("image splat-card scale settings must be positive");
    if (rgba.size() != width * height * 4U)
        throw std::invalid_argument("RGBA buffer size does not match image dimensions");

    const long double pixelCount = static_cast<long double>(width) * static_cast<long double>(height);
    std::size_t stride = 1U;
    if (pixelCount > static_cast<long double>(settings.maxSplats)) {
        const long double ratio = pixelCount / static_cast<long double>(settings.maxSplats);
        stride = std::max<std::size_t>(1U, static_cast<std::size_t>(std::ceil(std::sqrt(ratio))));
    }

    const double worldHeight = settings.worldHeight;
    const double worldWidth = worldHeight * static_cast<double>(width) / static_cast<double>(height);
    const double stepX = worldWidth / static_cast<double>(width);
    const double stepY = worldHeight / static_cast<double>(height);
    const double tangent = std::max(1.0e-7, 0.5 * std::max(stepX, stepY) * static_cast<double>(stride) * settings.radiusMultiplier);
    const double thickness = std::max(1.0e-7, tangent * settings.normalThicknessRatio);

    ViewerScene scene;
    scene.surfaceKind = "image_splat_card_2_5d";
    scene.bounds.minimum = {-0.5 * worldWidth, -0.5 * worldHeight, -thickness};
    scene.bounds.maximum = {0.5 * worldWidth, 0.5 * worldHeight, thickness};
    scene.bounds.center = {0.0, 0.0, 0.0};
    scene.bounds.radius = std::max(0.05, 0.5 * math::length(scene.bounds.maximum - scene.bounds.minimum));

    const std::size_t estimated = ((width + stride - 1U) / stride) * ((height + stride - 1U) / stride);
    scene.before.reserve(std::min(estimated, settings.maxSplats));

    std::uint32_t localId = 1U;
    for (std::size_t y = 0U; y < height; y += stride) {
        for (std::size_t x = 0U; x < width; x += stride) {
            if (scene.before.size() >= settings.maxSplats) break;
            const std::size_t offset = (y * width + x) * 4U;
            const auto unit = [](std::uint8_t value) noexcept {
                return static_cast<float>(value) / 255.0F;
            };
            const float alpha = unit(rgba[offset + 3U]);
            if (!std::isfinite(alpha) || alpha < settings.alphaThreshold) continue;

            ViewerGaussian gaussian;
            gaussian.position = {
                (static_cast<double>(x) + 0.5) * stepX - 0.5 * worldWidth,
                0.5 * worldHeight - (static_cast<double>(y) + 0.5) * stepY,
                0.0,
            };
            gaussian.scale = {static_cast<float>(tangent), static_cast<float>(tangent), static_cast<float>(thickness)};
            gaussian.rotation = {1.0F, 0.0F, 0.0F, 0.0F};
            gaussian.color = {unit(rgba[offset]), unit(rgba[offset + 1U]), unit(rgba[offset + 2U])};
            gaussian.shDC = {
                static_cast<float>((static_cast<double>(gaussian.color[0]) - 0.5) / sh0),
                static_cast<float>((static_cast<double>(gaussian.color[1]) - 0.5) / sh0),
                static_cast<float>((static_cast<double>(gaussian.color[2]) - 0.5) / sh0),
            };
            gaussian.shCoefficientCount = 1U;
            gaussian.opacity = alpha;
            gaussian.id = {imageNamespace, localId++};
            scene.before.push_back(gaussian);
        }
        if (scene.before.size() >= settings.maxSplats) break;
    }

    if (scene.before.empty())
        throw std::runtime_error("image contains no pixels above the alpha threshold");
    scene.after = scene.before;
    return scene;
}

} // namespace vulkax::viewer
