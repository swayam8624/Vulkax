#pragma once

#include "vulkax/render/headless.hpp"

#include <cstddef>
#include <cstdint>

namespace vulkax::render {

struct ImageComparison {
    std::size_t pixelCount{};
    std::uint8_t maximumChannelDifference{};
    double meanAbsoluteDifference{};
    double rootMeanSquareError{};
    double psnrDb{};
    double changedPixelFraction{};
};

[[nodiscard]] ImageComparison compareImages(
    const ImageRGBA8& reference,
    const ImageRGBA8& candidate);

} // namespace vulkax::render
