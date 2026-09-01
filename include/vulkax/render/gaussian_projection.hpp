#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/render/gaussian.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vulkax::render {

enum class GaussianProjectionCullReason : std::uint32_t {
    Visible = 0U,
    Opacity = 1U,
    BehindCamera = 2U,
    OutsideImage = 3U,
};

// Fixed std430/Metal-compatible records used by the native projection kernels.
// Geometry/covariance projection is performed on the GPU. View-dependent SH
// color and opacity are prepared on the CPU so the new path can be compared
// against the established shading oracle independently of projection changes.
struct alignas(16) GaussianProjectionInput {
    std::array<float, 4> positionOpacity{}; // xyz, opacity
    std::array<float, 4> scale{};           // xyz linear sigma, padding
    std::array<float, 4> rotation{};        // wxyz
    std::array<float, 4> color{};           // rgb, padding
};

struct alignas(16) GaussianProjectedSplat {
    std::array<float, 4> centerMajor{}; // center ndc xy, major-axis ndc xy
    std::array<float, 4> minorDepth{};  // minor-axis ndc xy, depth, opacity
    std::array<float, 4> colorCull{};   // rgb, cull reason as exact small integer float
    std::array<float, 4> tileBounds{};  // first column, last column, first row, last row
};

struct alignas(16) GaussianProjectionParameters {
    std::array<float, 4> cameraPosition{};
    std::array<float, 4> right{};
    std::array<float, 4> up{};
    std::array<float, 4> forward{};
    std::array<float, 4> imageFocalNear{}; // width, height, focal pixels, near plane
    std::array<float, 4> sigmaTile{};      // cutoff, min sigma px, max sigma px, tile size
};

struct GaussianNativeProjectionResult {
    std::vector<GaussianProjectedSplat> projected;
    GaussianProjectionStats stats{};
    std::uint32_t tileSize{16U};
    std::uint32_t tileColumns{};
    std::uint32_t tileRows{};
    std::size_t splatReferences{};
    std::size_t maximumSplatsPerTile{};
    double projectionMilliseconds{};
    double schedulingMilliseconds{};
    std::size_t inputBytes{};
    std::size_t outputBytes{};
    std::size_t schedulerInputBytes{};
    std::size_t schedulerOutputBytes{};
    std::size_t schedulerWorkspaceBytes{};
};

[[nodiscard]] std::vector<GaussianProjectionInput> prepareGaussianProjectionInputs(
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings = {});

[[nodiscard]] GaussianProjectionParameters makeGaussianProjectionParameters(
    const GaussianRenderSettings& settings,
    std::uint32_t tileSize = 16U);

// Runs geometric projection/tile-range generation on the requested native
// backend. Cull compaction is still host-side in 1.1A, but deterministic depth
// ordering and CSR tile scheduling are computed by the native GPU scheduler.
// The projected records are returned in the same back-to-front order as the
// established CPU oracle so downstream raster behavior remains unchanged.
[[nodiscard]] GaussianNativeProjectionResult projectGaussianCloudNative(
    backend::BackendKind backend,
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings = {},
    std::uint32_t tileSize = 16U);

[[nodiscard]] GaussianRasterBatch buildGaussianRasterBatchFromProjection(
    const GaussianNativeProjectionResult& projection,
    const GaussianRenderSettings& settings = {});

[[nodiscard]] GaussianRenderResult renderGaussianCloudScalableHeadless(
    backend::BackendKind backend,
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings = {},
    std::uint32_t tileSize = 16U);

} // namespace vulkax::render
