#include "vulkax/render/gaussian.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

#ifndef VULKAX_HAS_VULKAN_RENDER
#define VULKAX_HAS_VULKAN_RENDER 0
#endif
#ifndef VULKAX_HAS_METAL_RENDER
#define VULKAX_HAS_METAL_RENDER 0
#endif

namespace vulkax::render {

#if VULKAX_HAS_VULKAN_RENDER
ImageRGBA8 renderGaussianVerticesVulkan(
    const std::vector<GaussianRasterVertex>& vertices,
    const RenderSettings& settings);
#endif
#if VULKAX_HAS_METAL_RENDER
ImageRGBA8 renderGaussianVerticesMetal(
    const std::vector<GaussianRasterVertex>& vertices,
    const RenderSettings& settings);
#endif

GaussianRenderResult renderGaussianRasterBatchHeadless(
    backend::BackendKind backend,
    const GaussianRasterBatch& batch,
    const RenderSettings& settings) {
    ImageRGBA8 image;
    switch (backend) {
        case backend::BackendKind::Vulkan:
#if VULKAX_HAS_VULKAN_RENDER
            image = renderGaussianVerticesVulkan(batch.vertices, settings);
            break;
#else
            throw std::runtime_error("Vulkan Gaussian rendering was not compiled into this build");
#endif
        case backend::BackendKind::Metal:
#if VULKAX_HAS_METAL_RENDER
            image = renderGaussianVerticesMetal(batch.vertices, settings);
            break;
#else
            throw std::runtime_error("Metal Gaussian rendering was not compiled into this build");
#endif
        case backend::BackendKind::OpenGL:
            throw std::runtime_error("OpenGL Gaussian rendering is not implemented yet");
    }
    return {std::move(image), batch.stats};
}

} // namespace vulkax::render
