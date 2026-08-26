#include "vulkax/render/gaussian_projection.hpp"
#include "vulkax/render/gaussian_tiles.hpp"
#include "vulkax/render/image_metrics.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

vulkax::gaussian::GaussianSplat makeSplat(
    vulkax::math::Vec3 position,
    double sx,
    double sy,
    double sz,
    std::array<double, 3> color) {
    vulkax::gaussian::GaussianSplat splat;
    splat.position = position;
    splat.logScale = {std::log(sx), std::log(sy), std::log(sz)};
    splat.rotation = {0.9659258262890683, 0.0, 0.25881904510252074, 0.0};
    splat.opacityLogit = 4.0;
    splat.shDC = color;
    return splat;
}

vulkax::gaussian::GaussianCloud makeScene() {
    using namespace vulkax;
    gaussian::GaussianCloud cloud;
    cloud.splats.push_back(makeSplat({-0.28, -0.14, 0.05}, 0.18, 0.06, 0.08, {0.8, -0.2, -0.2}));
    cloud.splats.push_back(makeSplat({0.22, -0.04, -0.20}, 0.10, 0.16, 0.07, {-0.2, 0.8, -0.2}));
    cloud.splats.push_back(makeSplat({0.02, 0.23, 0.12}, 0.12, 0.08, 0.15, {-0.2, -0.2, 0.8}));
    cloud.splats.push_back(makeSplat({0.34, 0.19, -0.42}, 0.07, 0.12, 0.06, {0.4, 0.3, -0.1}));

    auto behind = makeSplat({0.0, 0.0, 4.0}, 0.1, 0.1, 0.1, {0.2, 0.2, 0.2});
    cloud.splats.push_back(behind);

    auto transparent = makeSplat({0.0, 0.0, 0.0}, 0.1, 0.1, 0.1, {0.2, 0.2, 0.2});
    transparent.opacityLogit = -20.0;
    cloud.splats.push_back(transparent);

    auto outside = makeSplat({20.0, 0.0, 0.0}, 0.1, 0.1, 0.1, {0.2, 0.2, 0.2});
    cloud.splats.push_back(outside);
    return cloud;
}

void compareBatch(
    const vulkax::render::GaussianRasterBatch& reference,
    const vulkax::render::GaussianRasterBatch& accelerated) {
    assert(reference.stats.inputSplats == accelerated.stats.inputSplats);
    assert(reference.stats.visibleSplats == accelerated.stats.visibleSplats);
    assert(reference.stats.culledBehindCamera == accelerated.stats.culledBehindCamera);
    assert(reference.stats.culledOpacity == accelerated.stats.culledOpacity);
    assert(reference.stats.culledOutsideImage == accelerated.stats.culledOutsideImage);
    assert(reference.vertices.size() == accelerated.vertices.size());

    double maximum = 0.0;
    for (std::size_t index = 0U; index < reference.vertices.size(); ++index) {
        const auto& a = reference.vertices[index];
        const auto& b = accelerated.vertices[index];
        const std::array<double, 9> lhs{
            a.clipX, a.clipY, a.clipZ, a.localX, a.localY,
            a.red, a.green, a.blue, a.opacity,
        };
        const std::array<double, 9> rhs{
            b.clipX, b.clipY, b.clipZ, b.localX, b.localY,
            b.red, b.green, b.blue, b.opacity,
        };
        for (std::size_t component = 0U; component < lhs.size(); ++component)
            maximum = std::max(maximum, std::abs(lhs[component] - rhs[component]));
    }
    assert(maximum < 2.0e-4);
}

} // namespace

int main() {
    using namespace vulkax;
    const auto cloud = makeScene();

    render::GaussianRenderSettings settings;
    settings.image.width = 256U;
    settings.image.height = 192U;
    settings.camera.position = {0.0, 0.0, 3.0};
    settings.camera.target = {0.0, 0.0, 0.0};
    settings.camera.up = {0.0, 1.0, 0.0};
    settings.camera.verticalFovDegrees = 50.0;

    const auto prepared = render::prepareGaussianProjectionInputs(cloud, settings);
    assert(prepared.size() == cloud.size());
    assert(sizeof(render::GaussianProjectionInput) == 64U);
    assert(sizeof(render::GaussianProjectedSplat) == 64U);
    assert(sizeof(render::GaussianProjectionParameters) == 96U);

    const auto reference = render::buildGaussianRasterBatch(cloud, settings);
    assert(reference.stats.inputSplats == 7U);
    assert(reference.stats.visibleSplats == 4U);
    assert(reference.stats.culledBehindCamera == 1U);
    assert(reference.stats.culledOpacity == 1U);
    assert(reference.stats.culledOutsideImage == 1U);
    const auto referenceTiles = render::buildGaussianTileGrid(
        reference, settings.image.width, settings.image.height, 16U);

    const auto available = render::availableHeadlessRenderBackends();
    for (const auto backendKind : available) {
        if (backendKind != backend::BackendKind::Vulkan &&
            backendKind != backend::BackendKind::Metal)
            continue;

        const auto projection = render::projectGaussianCloudNative(
            backendKind, cloud, settings, 16U);
        assert(projection.projected.size() == reference.stats.visibleSplats);
        assert(projection.stats.inputSplats == reference.stats.inputSplats);
        assert(projection.stats.visibleSplats == reference.stats.visibleSplats);
        assert(projection.stats.culledBehindCamera == reference.stats.culledBehindCamera);
        assert(projection.stats.culledOpacity == reference.stats.culledOpacity);
        assert(projection.stats.culledOutsideImage == reference.stats.culledOutsideImage);
        assert(projection.tileColumns == referenceTiles.columns);
        assert(projection.tileRows == referenceTiles.rows);
        assert(projection.splatReferences == referenceTiles.splatReferences);
        assert(projection.maximumSplatsPerTile == referenceTiles.maximumSplatsPerTile);
        assert(std::isfinite(projection.projectionMilliseconds));
        assert(projection.projectionMilliseconds >= 0.0);
        assert(projection.inputBytes == cloud.size() * sizeof(render::GaussianProjectionInput));
        assert(projection.outputBytes == cloud.size() * sizeof(render::GaussianProjectedSplat));

        const auto acceleratedBatch = render::buildGaussianRasterBatchFromProjection(
            projection, settings);
        compareBatch(reference, acceleratedBatch);

        const auto referenceImage = render::renderGaussianRasterBatchHeadless(
            backendKind, reference, settings.image);
        const auto acceleratedImage = render::renderGaussianRasterBatchHeadless(
            backendKind, acceleratedBatch, settings.image);
        const auto comparison = render::compareImages(referenceImage.image, acceleratedImage.image);
        assert(comparison.maximumChannelDifference <= 2U);
        assert(comparison.rootMeanSquareError < 0.5);
        assert(comparison.changedPixelFraction < 0.05);

        const auto scalable = render::renderGaussianCloudScalableHeadless(
            backendKind, cloud, settings, 16U);
        const auto scalableComparison = render::compareImages(referenceImage.image, scalable.image);
        assert(scalable.stats.visibleSplats == reference.stats.visibleSplats);
        assert(scalableComparison.maximumChannelDifference <= 2U);
        assert(scalableComparison.rootMeanSquareError < 0.5);
    }

    return 0;
}
