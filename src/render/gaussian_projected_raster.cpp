#include "vulkax/render/gaussian_projection.hpp"

#include <stdexcept>
#include <utility>

#ifndef VULKAX_HAS_VULKAN_RENDER
#define VULKAX_HAS_VULKAN_RENDER 0
#endif
#ifndef VULKAX_HAS_METAL_RENDER
#define VULKAX_HAS_METAL_RENDER 0
#endif

namespace vulkax::render {

#if VULKAX_HAS_VULKAN_RENDER
ImageRGBA8 renderGaussianProjectedVulkan(
    const std::vector<GaussianProjectedSplat>& projected,
    const GaussianRenderSettings& settings);
#endif
#if VULKAX_HAS_METAL_RENDER
ImageRGBA8 renderGaussianProjectedMetal(
    const std::vector<GaussianProjectedSplat>& projected,
    const GaussianRenderSettings& settings);
#endif

GaussianRenderResult renderGaussianProjectionHeadless(
    backend::BackendKind backend,
    const GaussianNativeProjectionResult& projection,
    const GaussianRenderSettings& settings) {
    if (projection.projected.size() != projection.stats.visibleSplats)
        throw std::invalid_argument("projected Gaussian raster visible count is inconsistent");

    ImageRGBA8 image;
    switch (backend) {
        case backend::BackendKind::Vulkan:
#if VULKAX_HAS_VULKAN_RENDER
            image = renderGaussianProjectedVulkan(projection.projected, settings);
            break;
#else
            throw std::runtime_error("Vulkan projected Gaussian rendering was not compiled into this build");
#endif
        case backend::BackendKind::Metal:
#if VULKAX_HAS_METAL_RENDER
            image = renderGaussianProjectedMetal(projection.projected, settings);
            break;
#else
            throw std::runtime_error("Metal projected Gaussian rendering was not compiled into this build");
#endif
        case backend::BackendKind::OpenGL:
            throw std::runtime_error("OpenGL projected Gaussian rendering is not implemented");
    }

    GaussianRenderResult result;
    result.image = std::move(image);
    result.stats = projection.stats;
    result.directProjectedRaster = true;
    result.nativeRasterInputBytes =
        projection.projected.size() * sizeof(GaussianProjectedSplat);
    result.cpuExpandedVertexBytes = 0U;
    return result;
}

} // namespace vulkax::render
