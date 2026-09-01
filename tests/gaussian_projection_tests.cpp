#include "vulkax/render/gaussian_projection.hpp"
#include "vulkax/render/gaussian_scheduler.hpp"
#include "vulkax/render/gaussian_tiles.hpp"
#include "vulkax/render/image_metrics.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

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

vulkax::render::GaussianNativeProjectionResult makeTieProjection() {
    using namespace vulkax;
    render::GaussianNativeProjectionResult projection;
    projection.tileSize = 16U;
    projection.tileColumns = 1U;
    projection.tileRows = 1U;
    projection.projected.resize(4U);
    projection.stats.visibleSplats = 4U;
    projection.splatReferences = 4U;
    projection.maximumSplatsPerTile = 4U;
    const std::array<float, 4> depths{4.0F, 4.0F, 2.0F, 1.0F};
    for (std::size_t index = 0U; index < depths.size(); ++index) {
        projection.projected[index].minorDepth[2] = depths[index];
        projection.projected[index].tileBounds = {0.0F, 0.0F, 0.0F, 0.0F};
    }
    return projection;
}

void verifyDeterministicScheduleOracle() {
    using namespace vulkax;
    auto projection = makeTieProjection();
    const auto schedule = render::buildGaussianTileScheduleOracle(projection);
    render::validateGaussianTileSchedule(schedule, projection);
    assert(schedule.tileOffsets == std::vector<std::uint32_t>({0U, 4U}));
    assert(schedule.projectedSplatIndices ==
           std::vector<std::uint32_t>({0U, 1U, 2U, 3U}));
    assert(schedule.maximumSplatsPerTile == 4U);
}

} // namespace

int main() {
    using namespace vulkax;
    verifyDeterministicScheduleOracle();

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

        const auto oracleSchedule = render::buildGaussianTileScheduleOracle(projection);
        render::validateGaussianTileSchedule(oracleSchedule, projection);
        assert(oracleSchedule.splatReferences == referenceTiles.splatReferences);
        assert(oracleSchedule.maximumSplatsPerTile == referenceTiles.maximumSplatsPerTile);
        assert(oracleSchedule.tileOffsets.size() == referenceTiles.tiles.size() + 1U);
        for (std::uint32_t row = 0U; row < oracleSchedule.rows; ++row) {
            for (std::uint32_t column = 0U; column < oracleSchedule.columns; ++column) {
                const std::size_t tileId = static_cast<std::size_t>(row) * oracleSchedule.columns + column;
                const auto& referenceTile = referenceTiles.at(column, row);
                const std::uint32_t begin = oracleSchedule.tileOffsets[tileId];
                const std::uint32_t end = oracleSchedule.tileOffsets[tileId + 1U];
                assert(static_cast<std::size_t>(end - begin) == referenceTile.splatIndices.size());
                for (std::uint32_t offset = 0U; offset < end - begin; ++offset) {
                    assert(oracleSchedule.projectedSplatIndices[begin + offset] ==
                           referenceTile.splatIndices[offset]);
                }
            }
        }

        const auto nativeSchedule = render::scheduleGaussianProjectionNative(backendKind, projection);
        assert(std::isfinite(nativeSchedule.schedulingMilliseconds));
        assert(nativeSchedule.schedulingMilliseconds >= 0.0);
        assert(nativeSchedule.inputBytes ==
               projection.projected.size() * sizeof(render::GaussianProjectedSplat));
        assert(nativeSchedule.outputBytes ==
               oracleSchedule.tileOffsets.size() * sizeof(std::uint32_t) +
               oracleSchedule.projectedSplatIndices.size() * sizeof(std::uint32_t) +
               4U * sizeof(std::uint32_t));
        assert(nativeSchedule.schedule.tileOffsets == oracleSchedule.tileOffsets);
        assert(nativeSchedule.schedule.projectedSplatIndices == oracleSchedule.projectedSplatIndices);
        assert(nativeSchedule.schedule.splatReferences == oracleSchedule.splatReferences);
        assert(nativeSchedule.schedule.maximumSplatsPerTile == oracleSchedule.maximumSplatsPerTile);

        const auto tieProjection = makeTieProjection();
        const auto nativeTie = render::scheduleGaussianProjectionNative(backendKind, tieProjection);
        assert(nativeTie.schedule.tileOffsets == std::vector<std::uint32_t>({0U, 4U}));
        assert(nativeTie.schedule.projectedSplatIndices ==
               std::vector<std::uint32_t>({0U, 1U, 2U, 3U}));

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
