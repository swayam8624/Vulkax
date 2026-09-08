#pragma once

#include "vulkax/viewer/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <utility>

namespace vulkax::viewer {

[[nodiscard]] inline gaussian::GaussianCloud makeGaussianCloudFromViewer(
    std::span<const ViewerGaussian> gaussians) {
    gaussian::GaussianCloud cloud;
    if (gaussians.empty()) return cloud;

    std::size_t coefficientCount = 1U;
    for (const auto& source : gaussians)
        coefficientCount = std::max(coefficientCount,
            std::min<std::size_t>(source.shCoefficientCount, kViewerMaxShCoefficients));
    cloud.shRestCoefficientsPerSplat = (coefficientCount - 1U) * 3U;
    cloud.splats.reserve(gaussians.size());

    constexpr std::uint32_t fallbackNamespace = 0x56575201U; // "VWR" presentation fallback.
    for (std::size_t index = 0U; index < gaussians.size(); ++index) {
        const auto& source = gaussians[index];
        gaussian::GaussianSplat output;
        output.position = source.position;
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            const double scale = std::max(static_cast<double>(source.scale[axis]), 1.0e-12);
            output.logScale[axis] = std::log(scale);
        }
        for (std::size_t component = 0U; component < 4U; ++component)
            output.rotation[component] = static_cast<double>(source.rotation[component]);

        const double opacity = std::clamp(static_cast<double>(source.opacity), 1.0e-7, 1.0 - 1.0e-7);
        output.opacityLogit = std::log(opacity / (1.0 - opacity));
        for (std::size_t channel = 0U; channel < 3U; ++channel)
            output.shDC[channel] = static_cast<double>(source.shDC[channel]);

        output.shRest.assign(cloud.shRestCoefficientsPerSplat, 0.0);
        const std::size_t sourceRest = (std::min<std::size_t>(source.shCoefficientCount,
            kViewerMaxShCoefficients) - 1U) * 3U;
        const std::size_t copyCount = std::min(sourceRest, output.shRest.size());
        for (std::size_t value = 0U; value < copyCount; ++value)
            output.shRest[value] = static_cast<double>(source.shRest[value]);

        output.id = source.id.valid()
            ? source.id
            : gaussian::GaussianId{fallbackNamespace, static_cast<std::uint32_t>(index + 1U)};
        cloud.splats.push_back(std::move(output));
    }
    return cloud;
}

inline void writeViewerGaussianPly(std::span<const ViewerGaussian> gaussians,
                                   const std::filesystem::path& path) {
    if (gaussians.empty())
        throw std::invalid_argument("cannot export an empty viewer Gaussian set");
    gaussian::write3dgsPly(makeGaussianCloudFromViewer(gaussians), path);
}

} // namespace vulkax::viewer
