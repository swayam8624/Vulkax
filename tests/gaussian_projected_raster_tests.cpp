#include "vulkax/render/gaussian_projection.hpp"
#include "vulkax/render/image_metrics.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

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
    return cloud;
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

    const auto available = render::availableHeadlessRenderBackends();
    for (const auto backendKind : available) {
        if (backendKind != backend::BackendKind::Vulkan &&
            backendKind != backend::BackendKind::Metal)
            continue;

        const auto projection = render::projectGaussianCloudNative(
            backendKind, cloud, settings, 16U);
        assert(projection.fusedProjectionScheduling);
        assert(projection.projected.size() == cloud.size());

        const auto expandedBatch = render::buildGaussianRasterBatchFromProjection(
            projection, settings);
        assert(expandedBatch.vertices.size() == projection.projected.size() * 6U);

        const auto legacy = render::renderGaussianRasterBatchHeadless(
            backendKind, expandedBatch, settings.image);
        const auto direct = render::renderGaussianProjectionHeadless(
            backendKind, projection, settings);

        assert(!legacy.directProjectedRaster);
        assert(legacy.cpuExpandedVertexBytes ==
               expandedBatch.vertices.size() * sizeof(render::GaussianRasterVertex));
        assert(legacy.nativeRasterInputBytes == legacy.cpuExpandedVertexBytes);

        assert(direct.directProjectedRaster);
        assert(direct.cpuExpandedVertexBytes == 0U);
        assert(direct.nativeRasterInputBytes ==
               projection.projected.size() * sizeof(render::GaussianProjectedSplat));
        assert(direct.nativeRasterInputBytes < legacy.nativeRasterInputBytes);
        assert(direct.stats.visibleSplats == legacy.stats.visibleSplats);

        const auto comparison = render::compareImages(legacy.image, direct.image);
        assert(comparison.maximumChannelDifference <= 2U);
        assert(comparison.rootMeanSquareError < 0.5);
        assert(comparison.changedPixelFraction < 0.05);
    }

    return 0;
}
