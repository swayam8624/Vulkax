#include "vulkax/render/gaussian.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

vulkax::gaussian::GaussianCloud makeScene() {
    using namespace vulkax;
    gaussian::GaussianCloud cloud;

    gaussian::GaussianSplat visible;
    visible.position = {0.0, 0.0, 0.0};
    visible.logScale = {std::log(0.28), std::log(0.07), std::log(0.07)};
    visible.rotation = {1.0, 0.0, 0.0, 0.0};
    visible.opacityLogit = 4.0;
    visible.shDC = {1.0, -0.35, -0.35};
    cloud.splats.push_back(visible);

    gaussian::GaussianSplat behind = visible;
    behind.position = {0.0, 0.0, 4.0};
    cloud.splats.push_back(behind);
    return cloud;
}

std::size_t changedPixelCount(const vulkax::render::ImageRGBA8& image,
                              const vulkax::visualization::Color& clear) {
    const auto red = static_cast<std::uint8_t>(std::lround(std::clamp(clear.r, 0.0F, 1.0F) * 255.0F));
    const auto green = static_cast<std::uint8_t>(std::lround(std::clamp(clear.g, 0.0F, 1.0F) * 255.0F));
    const auto blue = static_cast<std::uint8_t>(std::lround(std::clamp(clear.b, 0.0F, 1.0F) * 255.0F));
    std::size_t changed = 0;
    for (std::size_t pixel = 0; pixel < image.pixels.size() / 4U; ++pixel) {
        const std::size_t offset = pixel * 4U;
        if (image.pixels[offset] != red || image.pixels[offset + 1] != green ||
            image.pixels[offset + 2] != blue)
            ++changed;
    }
    return changed;
}

} // namespace

int main() {
    using namespace vulkax;
    const auto cloud = makeScene();

    render::GaussianRenderSettings settings;
    settings.image.width = 128;
    settings.image.height = 128;
    settings.camera.position = {0.0, 0.0, 3.0};
    settings.camera.target = {0.0, 0.0, 0.0};
    settings.camera.up = {0.0, 1.0, 0.0};
    settings.camera.verticalFovDegrees = 50.0;

    const auto batch = render::buildGaussianRasterBatch(cloud, settings);
    assert(batch.stats.inputSplats == 2);
    assert(batch.stats.visibleSplats == 1);
    assert(batch.stats.culledBehindCamera == 1);
    assert(batch.vertices.size() == 6);

    float minimumX = batch.vertices.front().clipX;
    float maximumX = minimumX;
    float minimumY = batch.vertices.front().clipY;
    float maximumY = minimumY;
    for (const auto& vertex : batch.vertices) {
        minimumX = std::min(minimumX, vertex.clipX);
        maximumX = std::max(maximumX, vertex.clipX);
        minimumY = std::min(minimumY, vertex.clipY);
        maximumY = std::max(maximumY, vertex.clipY);
    }
    assert((maximumX - minimumX) > 2.5F * (maximumY - minimumY));

    const auto available = render::availableHeadlessRenderBackends();
    for (const auto backend : available) {
        if (backend != backend::BackendKind::Vulkan && backend != backend::BackendKind::Metal) continue;
        const auto result = render::renderGaussianCloudHeadless(backend, cloud, settings);
        assert(result.stats.visibleSplats == 1);
        assert(result.image.width == settings.image.width);
        assert(result.image.height == settings.image.height);
        assert(result.image.pixels.size() == static_cast<std::size_t>(settings.image.width) *
                                             static_cast<std::size_t>(settings.image.height) * 4U);
        assert(changedPixelCount(result.image, settings.image.clearColor) > 32);
    }

    return 0;
}
