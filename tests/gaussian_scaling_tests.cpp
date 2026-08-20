#include "vulkax/gaussian/hierarchy.hpp"
#include "vulkax/render/gaussian.hpp"
#include "vulkax/render/gaussian_tiles.hpp"
#include "vulkax/render/image_metrics.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

vulkax::gaussian::GaussianCloud makeGridCloud() {
    vulkax::gaussian::GaussianCloud cloud;
    for (int z = 0; z < 4; ++z) {
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 6; ++x) {
                vulkax::gaussian::GaussianSplat splat;
                splat.position = {
                    0.1 * static_cast<double>(x),
                    0.1 * static_cast<double>(y),
                    0.1 * static_cast<double>(z)};
                splat.logScale = {std::log(0.025), std::log(0.025), std::log(0.025)};
                splat.opacityLogit = 4.0;
                splat.shDC = {0.25, 0.0, -0.25};
                cloud.splats.push_back(splat);
            }
        }
    }
    return cloud;
}

void testHierarchy() {
    using namespace vulkax;
    const auto cloud = makeGridCloud();
    const auto hierarchy = gaussian::buildGaussianHierarchy(cloud, 8);
    assert(!hierarchy.empty());
    assert(hierarchy.permutation.size() == cloud.size());

    std::vector<std::size_t> sorted = hierarchy.permutation;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t index = 0; index < sorted.size(); ++index) assert(sorted[index] == index);

    const auto stats = gaussian::summarizeGaussianHierarchy(hierarchy);
    assert(stats.nodeCount == hierarchy.nodes.size());
    assert(stats.leafCount > 1);
    assert(stats.maximumDepth > 0);
    assert(stats.maximumLeafOccupancy <= 8);

    const math::Vec3 minimum{0.15, 0.15, 0.05};
    const math::Vec3 maximum{0.45, 0.35, 0.25};
    const auto accelerated =
        gaussian::queryGaussianHierarchyAabb(hierarchy, cloud, minimum, maximum);

    std::vector<std::size_t> bruteForce;
    for (std::size_t index = 0; index < cloud.size(); ++index) {
        const auto p = cloud.splats[index].position;
        if (p.x >= minimum.x && p.x <= maximum.x &&
            p.y >= minimum.y && p.y <= maximum.y &&
            p.z >= minimum.z && p.z <= maximum.z)
            bruteForce.push_back(index);
    }
    assert(accelerated == bruteForce);
}

void testTileBinning() {
    using namespace vulkax;
    gaussian::GaussianCloud cloud;

    gaussian::GaussianSplat farSplat;
    farSplat.position = {0.0, 0.0, -0.4};
    farSplat.logScale = {std::log(0.15), std::log(0.15), std::log(0.15)};
    farSplat.opacityLogit = 4.0;
    farSplat.shDC = {0.5, -0.3, -0.3};

    gaussian::GaussianSplat nearSplat = farSplat;
    nearSplat.position = {0.0, 0.0, 0.3};
    nearSplat.shDC = {-0.3, 0.5, -0.3};

    cloud.splats = {nearSplat, farSplat};

    render::GaussianRenderSettings settings;
    settings.image.width = 128;
    settings.image.height = 128;
    settings.camera.position = {0.0, 0.0, 3.0};
    settings.camera.target = {0.0, 0.0, 0.0};

    const auto batch = render::buildGaussianRasterBatch(cloud, settings);
    assert(batch.stats.visibleSplats == 2);
    const auto tiles = render::buildGaussianTileGrid(
        batch, settings.image.width, settings.image.height, 16);
    assert(tiles.columns == 8);
    assert(tiles.rows == 8);
    assert(tiles.splatReferences >= 2);
    assert(tiles.maximumSplatsPerTile >= 2);

    const auto& center = tiles.at(4, 4);
    assert(center.splatIndices.size() == 2);
    assert(center.splatIndices[0] == 0);
    assert(center.splatIndices[1] == 1);
}

void testImageComparison() {
    using namespace vulkax;
    render::ImageRGBA8 a{2, 1, {0, 0, 0, 255, 10, 20, 30, 255}};
    render::ImageRGBA8 b = a;

    const auto identical = render::compareImages(a, b);
    assert(identical.pixelCount == 2);
    assert(identical.maximumChannelDifference == 0);
    assert(identical.meanAbsoluteDifference == 0.0);
    assert(identical.rootMeanSquareError == 0.0);
    assert(std::isinf(identical.psnrDb));
    assert(identical.changedPixelFraction == 0.0);

    b.pixels[4] = 20;
    const auto changed = render::compareImages(a, b);
    assert(changed.maximumChannelDifference == 10);
    assert(changed.meanAbsoluteDifference > 0.0);
    assert(changed.rootMeanSquareError > 0.0);
    assert(std::isfinite(changed.psnrDb));
    assert(std::abs(changed.changedPixelFraction - 0.5) < 1.0e-12);
}

} // namespace

int main() {
    testHierarchy();
    testTileBinning();
    testImageComparison();
    return 0;
}
