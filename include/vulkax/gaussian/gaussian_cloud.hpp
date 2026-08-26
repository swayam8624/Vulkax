#pragma once

#include "vulkax/core/math.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
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

// std::filesystem::path does not portably convert to std::string (notably on
// Windows, where its native character type differs). Keep the historical
// string overload unambiguous for string literals/argv while accepting an exact
// filesystem::path through this constrained forwarding overload.
template <typename Path>
requires std::is_same_v<std::remove_cvref_t<Path>, std::filesystem::path>
[[nodiscard]] GaussianCloud load3dgsPly(Path&& path) {
    return load3dgsPly(path.string());
}

[[nodiscard]] GaussianCloud parse3dgsPly(std::string_view bytes);

} // namespace vulkax::gaussian
