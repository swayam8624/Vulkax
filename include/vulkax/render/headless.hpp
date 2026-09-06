#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/visualization/scientific.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vulkax::render {

struct RenderSettings {
    std::uint32_t width{1280};
    std::uint32_t height{720};
    double worldScale{1.0};
    visualization::Color clearColor{0.015F, 0.018F, 0.024F, 1.0F};
};

struct ImageRGBA8 {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
};

[[nodiscard]] std::vector<backend::BackendKind> availableHeadlessRenderBackends();
[[nodiscard]] ImageRGBA8 renderParticlesHeadless(
    backend::BackendKind backend, const std::vector<visualization::ParticleInstance>& particles,
    const RenderSettings& settings = {});

// PPM remains the compact regression/evidence format used by the 1.0 contract.
void writePpm(const ImageRGBA8& image, const std::string& path);

// Dependency-free RGBA PNG writer used by presentation-facing artifacts. The
// encoder intentionally uses stored DEFLATE blocks: files are deterministic and
// universally viewable without introducing zlib/libpng into the stable core.
void writePng(const ImageRGBA8& image, const std::string& path);

} // namespace vulkax::render
