#include "vulkax/render/headless.hpp"

#include <fstream>
#include <stdexcept>

#ifndef VULKAX_HAS_VULKAN_RENDER
#define VULKAX_HAS_VULKAN_RENDER 0
#endif
#ifndef VULKAX_HAS_METAL_RENDER
#define VULKAX_HAS_METAL_RENDER 0
#endif

namespace vulkax::render {

#if VULKAX_HAS_VULKAN_RENDER
ImageRGBA8 renderParticlesVulkan(const std::vector<visualization::ParticleInstance>& particles,
                                 const RenderSettings& settings);
#endif
#if VULKAX_HAS_METAL_RENDER
ImageRGBA8 renderParticlesMetal(const std::vector<visualization::ParticleInstance>& particles,
                                const RenderSettings& settings);
#endif

std::vector<backend::BackendKind> availableHeadlessRenderBackends() {
    std::vector<backend::BackendKind> result;
#if VULKAX_HAS_VULKAN_RENDER
    result.push_back(backend::BackendKind::Vulkan);
#endif
#if VULKAX_HAS_METAL_RENDER
    result.push_back(backend::BackendKind::Metal);
#endif
    return result;
}

ImageRGBA8 renderParticlesHeadless(backend::BackendKind backend,
                                   const std::vector<visualization::ParticleInstance>& particles,
                                   const RenderSettings& settings) {
    if (settings.width == 0 || settings.height == 0 || settings.worldScale <= 0.0)
        throw std::invalid_argument("invalid headless render settings");
    switch (backend) {
    case backend::BackendKind::Vulkan:
#if VULKAX_HAS_VULKAN_RENDER
        return renderParticlesVulkan(particles, settings);
#else
        throw std::runtime_error("Vulkan headless renderer is not built");
#endif
    case backend::BackendKind::Metal:
#if VULKAX_HAS_METAL_RENDER
        return renderParticlesMetal(particles, settings);
#else
        throw std::runtime_error("Metal headless renderer is not built");
#endif
    case backend::BackendKind::OpenGL:
        throw std::runtime_error("OpenGL headless renderer is not built");
    }
    throw std::logic_error("unknown renderer backend");
}

void writePpm(const ImageRGBA8& image, const std::string& path) {
    if (image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4u)
        throw std::invalid_argument("RGBA image byte count does not match dimensions");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("failed to open PPM output: " + path);
    stream << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t i = 0; i < image.pixels.size(); i += 4)
        stream.write(reinterpret_cast<const char*>(image.pixels.data() + i), 3);
}

} // namespace vulkax::render
