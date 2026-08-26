#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/render/gaussian_projection.hpp"
#include "vulkax/render/image_metrics.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vulkax::render {

struct GaussianTileReferenceStream {
    std::uint32_t tileSize{};
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::vector<std::size_t> offsets;
    std::vector<std::size_t> splatIndices;
    std::size_t maximumSplatsPerTile{};
};

// Builds a deterministic CSR-style tile reference stream from the native
// projection result. projectGaussianCloudNative() already keeps visible splats
// in stable far-to-near depth order; iterating them in that order means each
// tile's reference list inherits the same deterministic compositing order.
[[nodiscard]] GaussianTileReferenceStream buildGaussianTileReferenceStream(
    const GaussianNativeProjectionResult& projection);

struct GaussianScalingSample {
    std::size_t inputSplats{};
    std::size_t visibleSplats{};
    std::size_t tileReferences{};
    std::size_t maximumSplatsPerTile{};
    std::size_t projectionInputBytes{};
    std::size_t projectionOutputBytes{};
    std::size_t tileReferenceBytes{};
    double cpuProjectionMilliseconds{};
    double nativeProjectionMilliseconds{};
    double scalableTotalMilliseconds{};
    ImageComparison imageComparison{};
    bool usedNativeProjection{};
    std::string fallbackReason;
};

struct GaussianScalableRenderOutcome {
    GaussianRenderResult result;
    bool usedNativeProjection{};
    std::string fallbackReason;
};

// Uses native projection when available. If the requested native projection
// path is unavailable at runtime/build time, falls back to the established CPU
// projection oracle while preserving the requested native raster backend.
[[nodiscard]] GaussianScalableRenderOutcome renderGaussianCloudScalableWithFallback(
    backend::BackendKind backend,
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings = {},
    std::uint32_t tileSize = 16U);

[[nodiscard]] GaussianScalingSample benchmarkGaussianExecution(
    backend::BackendKind backend,
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings = {},
    std::uint32_t tileSize = 16U);

void writeGaussianScalingCsv(
    const std::vector<GaussianScalingSample>& samples,
    const std::filesystem::path& path);

} // namespace vulkax::render
