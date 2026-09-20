#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/render/camera.hpp"
#include "vulkax/render/headless.hpp"

#include <cstddef>
#include <vector>

namespace vulkax::render {

struct GaussianRasterVertex {
    float clipX{};
    float clipY{};
    float clipZ{};
    float localX{};
    float localY{};
    float red{};
    float green{};
    float blue{};
    float opacity{};
};

struct GaussianRenderSettings {
    RenderSettings image{};
    Camera camera{};
    double nearPlane{1.0e-3};
    double sigmaCutoff{3.0};
    double minimumSigmaPixels{0.35};
    double maximumSigmaPixels{512.0};
    bool evaluateSphericalHarmonics{true};
};

struct GaussianProjectionStats {
    std::size_t inputSplats{};
    std::size_t visibleSplats{};
    std::size_t culledBehindCamera{};
    std::size_t culledOpacity{};
    std::size_t culledOutsideImage{};
};

struct GaussianRasterBatch {
    std::vector<GaussianRasterVertex> vertices;
    GaussianProjectionStats stats;
};

struct GaussianRenderResult {
    ImageRGBA8 image;
    GaussianProjectionStats stats;
    // Native raster input evidence. The legacy/oracle path uploads expanded
    // GaussianRasterVertex records; the 1.1 projected-splat path uploads one
    // fixed projected record per visible splat and expands six corners in the
    // vertex shader.
    bool directProjectedRaster{false};
    std::size_t nativeRasterInputBytes{};
    std::size_t cpuExpandedVertexBytes{};
};

[[nodiscard]] GaussianRasterBatch buildGaussianRasterBatch(
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings = {});

// Rasterizes an already projected, depth-ordered Gaussian batch through the
// selected native backend. This remains the CPU projection/raster oracle path.
[[nodiscard]] GaussianRenderResult renderGaussianRasterBatchHeadless(
    backend::BackendKind backend,
    const GaussianRasterBatch& batch,
    const RenderSettings& settings = {});

[[nodiscard]] GaussianRenderResult renderGaussianCloudHeadless(
    backend::BackendKind backend,
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings = {});

} // namespace vulkax::render
