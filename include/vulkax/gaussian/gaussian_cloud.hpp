#pragma once

#include "vulkax/core/math.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace vulkax::gaussian {

struct GaussianSplat {
    math::Vec3 position{};
    std::array<double, 3> logScale{};
    std::array<double, 4> rotation{1.0, 0.0, 0.0, 0.0};
    double opacityLogit{};
    std::array<double, 3> shDC{};
    std::vector<double> shRest;

    [[nodiscard]] std::array<double, 3> linearScale() const noexcept;
    [[nodiscard]] double opacity() const noexcept;
};

struct GaussianCloud {
    std::vector<GaussianSplat> splats;
    std::size_t shRestCoefficientsPerSplat{};

    [[nodiscard]] bool empty() const noexcept { return splats.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return splats.size(); }
};

// Loads the scalar vertex layout emitted by the original 3D Gaussian Splatting
// implementation and compatible tools. Both ASCII and binary_little_endian PLY
// are accepted. Stored scale and opacity values remain in their optimization
// parameterization (log scale and logit); helpers expose physical values.
[[nodiscard]] GaussianCloud load3dgsPly(const std::string& path);
[[nodiscard]] GaussianCloud parse3dgsPly(std::string_view bytes);

} // namespace vulkax::gaussian
