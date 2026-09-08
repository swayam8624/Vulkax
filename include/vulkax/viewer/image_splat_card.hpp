#pragma once

#include "vulkax/viewer/scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

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
[[nodiscard]] ViewerScene makeImageSplatCard(
    std::size_t width,
    std::size_t height,
    std::span<const std::uint8_t> rgba,
    const ImageSplatCardSettings& settings = {});

} // namespace vulkax::viewer
