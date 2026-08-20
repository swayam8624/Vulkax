#include "vulkax/coupling/mls_embedding.hpp"

#include <cassert>
#include <cmath>
#include <vector>

namespace {

vulkax::math::Vec3 affineTransform(vulkax::math::Vec3 point) {
    return {
        1.2 * point.x + 0.1 * point.y - 0.05 * point.z + 0.3,
        -0.2 * point.x + 0.9 * point.y + 0.15 * point.z - 0.4,
        0.07 * point.x - 0.12 * point.y + 1.1 * point.z + 0.2,
    };
}

} // namespace

int main() {
    using namespace vulkax;
    gaussian::GaussianCloud cloud;
    gaussian::GaussianSplat splat;
    splat.position = {0.2, -0.1, 0.15};
    cloud.splats.push_back(splat);

    std::vector<coupling::PhysicalPoint> points;
    std::uint64_t id = 0;
    for (double z : {-1.0, 1.0}) {
        for (double y : {-1.0, 1.0}) {
            for (double x : {-1.0, 1.0}) {
                coupling::PhysicalPoint point;
                point.id = id++;
                point.restPosition = {x, y, z};
                point.position = point.restPosition;
                points.push_back(point);
            }
        }
    }

    const auto embedding = coupling::buildMlsEmbedding(cloud, points, 8);
    assert(embedding.supports.size() == 1);
    assert(embedding.maximumPartitionOfUnityError < 1.0e-10);
    assert(embedding.maximumAffineReproductionError < 1.0e-10);

    for (auto& point : points) point.position = affineTransform(point.restPosition);
    auto deformedCloud = cloud;
    coupling::updateGaussianPositionsFromPhysics(embedding, points, deformedCloud);
    const auto expected = affineTransform(cloud.splats[0].position);
    assert(math::length(deformedCloud.splats[0].position - expected) < 1.0e-10);

    const math::Vec3 appliedForce{3.0, -2.0, 5.0};
    const auto transfer = coupling::transferGaussianForceToPhysics(
        embedding, 0, deformedCloud.splats[0].position, appliedForce, points);
    assert(transfer.forceConservationError < 1.0e-10);
    assert(transfer.torqueConservationError < 1.0e-10);

    return 0;
}
