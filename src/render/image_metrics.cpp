#include "vulkax/render/image_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vulkax::render {

ImageComparison compareImages(const ImageRGBA8& reference, const ImageRGBA8& candidate) {
    if (reference.width != candidate.width || reference.height != candidate.height)
        throw std::invalid_argument("image comparison requires identical dimensions");
    if (reference.pixels.size() != candidate.pixels.size())
        throw std::invalid_argument("image comparison requires identical byte counts");
    const std::size_t expected =
        static_cast<std::size_t>(reference.width) * reference.height * 4U;
    if (reference.pixels.size() != expected)
        throw std::invalid_argument("RGBA image byte count does not match dimensions");

    ImageComparison result;
    result.pixelCount = static_cast<std::size_t>(reference.width) * reference.height;
    if (reference.pixels.empty()) {
        result.psnrDb = std::numeric_limits<double>::infinity();
        return result;
    }

    double absoluteSum = 0.0;
    double squaredSum = 0.0;
    std::size_t changedPixels = 0;
    const std::size_t channelCount = reference.pixels.size();

    for (std::size_t pixel = 0; pixel < result.pixelCount; ++pixel) {
        bool changed = false;
        for (std::size_t channel = 0; channel < 4; ++channel) {
            const std::size_t index = pixel * 4U + channel;
            const int difference =
                std::abs(static_cast<int>(reference.pixels[index]) -
                         static_cast<int>(candidate.pixels[index]));
            result.maximumChannelDifference = std::max(
                result.maximumChannelDifference, static_cast<std::uint8_t>(difference));
            const double normalized = static_cast<double>(difference) / 255.0;
            absoluteSum += normalized;
            squaredSum += normalized * normalized;
            changed = changed || difference != 0;
        }
        if (changed) ++changedPixels;
    }

    result.meanAbsoluteDifference = absoluteSum / static_cast<double>(channelCount);
    result.rootMeanSquareError =
        std::sqrt(squaredSum / static_cast<double>(channelCount));
    result.psnrDb = result.rootMeanSquareError == 0.0
                        ? std::numeric_limits<double>::infinity()
                        : 20.0 * std::log10(1.0 / result.rootMeanSquareError);
    result.changedPixelFraction = result.pixelCount == 0
                                      ? 0.0
                                      : static_cast<double>(changedPixels) /
                                            static_cast<double>(result.pixelCount);
    return result;
}

} // namespace vulkax::render
