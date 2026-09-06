#include "vulkax/render/headless.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

int main() {
    using namespace vulkax;

    // PNG output is presentation infrastructure and must remain available even on
    // builds without a native graphics backend.
    const render::ImageRGBA8 pngFixture{
        2U,
        2U,
        {
            255U, 0U, 0U, 255U, 0U, 255U, 0U, 255U,
            0U, 0U, 255U, 255U, 255U, 255U, 255U, 128U,
        },
    };
    const auto pngPath = std::filesystem::temp_directory_path() / "vulkax_render_test.png";
    render::writePng(pngFixture, pngPath.string());
    std::ifstream pngStream(pngPath, std::ios::binary);
    const std::vector<std::uint8_t> pngBytes{
        std::istreambuf_iterator<char>(pngStream), std::istreambuf_iterator<char>()};
    const std::array<std::uint8_t, 8> pngSignature{
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    assert(pngBytes.size() > 40U);
    for (std::size_t index = 0U; index < pngSignature.size(); ++index)
        assert(pngBytes[index] == pngSignature[index]);
    assert(pngBytes[12U] == static_cast<std::uint8_t>('I'));
    assert(pngBytes[13U] == static_cast<std::uint8_t>('H'));
    assert(pngBytes[14U] == static_cast<std::uint8_t>('D'));
    assert(pngBytes[15U] == static_cast<std::uint8_t>('R'));
    std::filesystem::remove(pngPath);

    const std::vector<visualization::ParticleInstance> particles = {
        {{-0.45, -0.15, 0.1}, 0.22, {0.95F, 0.25F, 0.10F, 1.0F}},
        {{0.0, 0.1, 0.2}, 0.28, {0.15F, 0.80F, 0.95F, 1.0F}},
        {{0.48, -0.05, 0.3}, 0.18, {0.75F, 0.95F, 0.20F, 1.0F}},
    };
    for (const auto backend : render::availableHeadlessRenderBackends()) {
        const render::RenderSettings settings{160U, 120U, 1.0, {0.01F, 0.01F, 0.015F, 1.0F}};
        const auto image = render::renderParticlesHeadless(backend, particles, settings);
        assert(image.pixels.size() == 160U * 120U * 4U);
        std::size_t bright = 0U;
        for (std::size_t index = 0U; index < image.pixels.size(); index += 4U) {
            if (image.pixels[index] > 40U || image.pixels[index + 1U] > 40U ||
                image.pixels[index + 2U] > 40U)
                ++bright;
        }
        assert(bright > 500U);
    }
    return 0;
}
