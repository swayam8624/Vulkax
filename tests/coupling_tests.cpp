#include "vulkax/coupling/mls_embedding.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using Matrix3 = std::array<std::array<double, 3>, 3>;

constexpr Matrix3 deformationLinear{{
    {1.20, 0.10, -0.05},
    {-0.20, 0.90, 0.15},
    {0.07, -0.12, 1.10},
}};

constexpr vulkax::math::Vec3 deformationTranslation{0.3, -0.4, 0.2};

vulkax::math::Vec3 multiply(const Matrix3& matrix, vulkax::math::Vec3 point) {
    return {
        matrix[0][0] * point.x + matrix[0][1] * point.y + matrix[0][2] * point.z,
        matrix[1][0] * point.x + matrix[1][1] * point.y + matrix[1][2] * point.z,
        matrix[2][0] * point.x + matrix[2][1] * point.y + matrix[2][2] * point.z,
    };
}

vulkax::math::Vec3 affineTransform(vulkax::math::Vec3 point) {
    return multiply(deformationLinear, point) + deformationTranslation;
}

Matrix3 transpose(const Matrix3& matrix) {
    Matrix3 result{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            result[row][column] = matrix[column][row];
    return result;
}

Matrix3 multiply(const Matrix3& lhs, const Matrix3& rhs) {
    Matrix3 result{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            for (std::size_t inner = 0; inner < 3; ++inner)
                result[row][column] += lhs[row][inner] * rhs[inner][column];
    return result;
}

Matrix3 quaternionRotation(const std::array<double, 4>& quaternion) {
    const double w = quaternion[0];
    const double x = quaternion[1];
    const double y = quaternion[2];
    const double z = quaternion[3];
    return {{{1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w)},
             {2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)},
             {2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y)}}};
}

Matrix3 covariance(const vulkax::gaussian::GaussianSplat& splat) {
    const Matrix3 rotation = quaternionRotation(splat.rotation);
    Matrix3 variance{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double scale = std::exp(splat.logScale[axis]);
        variance[axis][axis] = scale * scale;
    }
    return multiply(multiply(rotation, variance), transpose(rotation));
}

double maximumAbsoluteDifference(const Matrix3& lhs, const Matrix3& rhs) {
    double maximum = 0.0;
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            maximum = std::max(maximum, std::abs(lhs[row][column] - rhs[row][column]));
    return maximum;
}

} // namespace

int main() {
    using namespace vulkax;
    gaussian::GaussianCloud cloud;
    gaussian::GaussianSplat splat;
    splat.position = {0.2, -0.1, 0.15};
    splat.logScale = {std::log(0.20), std::log(0.09), std::log(0.045)};
    constexpr double halfAngle = 0.2617993877991494; // 15 degrees; quaternion is a 30-degree Z rotation.
    splat.rotation = {std::cos(halfAngle), 0.0, 0.0, std::sin(halfAngle)};
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

    auto positionOnlyCloud = cloud;
    coupling::updateGaussianPositionsFromPhysics(embedding, points, positionOnlyCloud);
    const auto expectedPosition = affineTransform(cloud.splats[0].position);
    assert(math::length(positionOnlyCloud.splats[0].position - expectedPosition) < 1.0e-10);

    auto deformedCloud = cloud;
    coupling::updateGaussianGeometryFromPhysics(embedding, points, deformedCloud);
    assert(math::length(deformedCloud.splats[0].position - expectedPosition) < 1.0e-10);

    const Matrix3 restCovariance = covariance(cloud.splats[0]);
    const Matrix3 expectedCovariance =
        multiply(multiply(deformationLinear, restCovariance), transpose(deformationLinear));
    const Matrix3 actualCovariance = covariance(deformedCloud.splats[0]);
    assert(maximumAbsoluteDifference(actualCovariance, expectedCovariance) < 1.0e-10);

    // Re-applying the same physical state must not accumulate deformation.
    coupling::updateGaussianGeometryFromPhysics(embedding, points, deformedCloud);
    const Matrix3 repeatedCovariance = covariance(deformedCloud.splats[0]);
    assert(maximumAbsoluteDifference(repeatedCovariance, expectedCovariance) < 1.0e-10);

    const math::Vec3 appliedForce{3.0, -2.0, 5.0};
    const auto transfer = coupling::transferGaussianForceToPhysics(
        embedding, 0, deformedCloud.splats[0].position, appliedForce, points);
    assert(transfer.forceConservationError < 1.0e-10);
    assert(transfer.torqueConservationError < 1.0e-10);

    return 0;
}
