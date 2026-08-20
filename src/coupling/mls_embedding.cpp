#include "vulkax/coupling/mls_embedding.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vulkax::coupling {
namespace {

using Vector4 = std::array<double, 4>;
using Matrix3 = std::array<std::array<double, 3>, 3>;
using Matrix4 = std::array<std::array<double, 4>, 4>;

struct AffineMap {
    Matrix3 linear{};
    math::Vec3 translation{};
};

[[nodiscard]] Vector4 basis(math::Vec3 position) {
    return {1.0, position.x, position.y, position.z};
}

[[nodiscard]] double squaredDistance(math::Vec3 lhs, math::Vec3 rhs) {
    const auto delta = lhs - rhs;
    return math::dot(delta, delta);
}

[[nodiscard]] Vector4 solveLinearSystem(Matrix4 matrix, Vector4 rhs) {
    constexpr double pivotTolerance = 1.0e-13;
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        double pivotMagnitude = std::abs(matrix[pivot][column]);
        for (std::size_t row = column + 1; row < 4; ++row) {
            const double candidate = std::abs(matrix[row][column]);
            if (candidate > pivotMagnitude) {
                pivot = row;
                pivotMagnitude = candidate;
            }
        }
        if (pivotMagnitude < pivotTolerance)
            throw std::runtime_error(
                "MLS support is rank deficient; use non-coplanar physical support points");
        if (pivot != column) {
            std::swap(matrix[pivot], matrix[column]);
            std::swap(rhs[pivot], rhs[column]);
        }

        const double inversePivot = 1.0 / matrix[column][column];
        for (std::size_t entry = column; entry < 4; ++entry)
            matrix[column][entry] *= inversePivot;
        rhs[column] *= inversePivot;

        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            if (std::abs(factor) <= std::numeric_limits<double>::epsilon()) continue;
            for (std::size_t entry = column; entry < 4; ++entry)
                matrix[row][entry] -= factor * matrix[column][entry];
            rhs[row] -= factor * rhs[column];
        }
    }
    return rhs;
}

[[nodiscard]] double dot4(const Vector4& lhs, const Vector4& rhs) {
    double result = 0.0;
    for (std::size_t index = 0; index < 4; ++index) result += lhs[index] * rhs[index];
    return result;
}

[[nodiscard]] const GaussianSupport& supportFor(const MlsEmbedding& embedding,
                                                std::size_t gaussianIndex) {
    const auto iterator = std::find_if(
        embedding.supports.begin(), embedding.supports.end(),
        [gaussianIndex](const GaussianSupport& support) {
            return support.gaussianIndex == gaussianIndex;
        });
    if (iterator == embedding.supports.end())
        throw std::out_of_range("Gaussian has no MLS physical support");
    return *iterator;
}

[[nodiscard]] Matrix3 identity3() {
    return {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
}

[[nodiscard]] Matrix3 transpose(const Matrix3& matrix) {
    Matrix3 result{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            result[row][column] = matrix[column][row];
    return result;
}

[[nodiscard]] Matrix3 multiply(const Matrix3& lhs, const Matrix3& rhs) {
    Matrix3 result{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            for (std::size_t inner = 0; inner < 3; ++inner)
                result[row][column] += lhs[row][inner] * rhs[inner][column];
    return result;
}

[[nodiscard]] math::Vec3 applyLinear(const Matrix3& matrix, math::Vec3 vector) {
    return {
        matrix[0][0] * vector.x + matrix[0][1] * vector.y + matrix[0][2] * vector.z,
        matrix[1][0] * vector.x + matrix[1][1] * vector.y + matrix[1][2] * vector.z,
        matrix[2][0] * vector.x + matrix[2][1] * vector.y + matrix[2][2] * vector.z,
    };
}

[[nodiscard]] double determinant(const Matrix3& matrix) {
    return matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
           matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
           matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
}

[[nodiscard]] Matrix3 quaternionRotation(const std::array<double, 4>& quaternion) {
    double normSquared = 0.0;
    for (const double component : quaternion) normSquared += component * component;
    if (!(normSquared > 0.0) || !std::isfinite(normSquared))
        throw std::invalid_argument("Gaussian rest rotation must be a finite non-zero quaternion");
    const double inverseNorm = 1.0 / std::sqrt(normSquared);
    const double w = quaternion[0] * inverseNorm;
    const double x = quaternion[1] * inverseNorm;
    const double y = quaternion[2] * inverseNorm;
    const double z = quaternion[3] * inverseNorm;
    return {{{1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w),
              2.0 * (x * z + y * w)},
             {2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z),
              2.0 * (y * z - x * w)},
             {2.0 * (x * z - y * w), 2.0 * (y * z + x * w),
              1.0 - 2.0 * (x * x + y * y)}}};
}

[[nodiscard]] Matrix3 covarianceFromShape(const std::array<double, 3>& logScale,
                                          const std::array<double, 4>& rotation) {
    const Matrix3 axes = quaternionRotation(rotation);
    Matrix3 variance{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double scale = std::exp(logScale[axis]);
        if (!std::isfinite(scale)) throw std::invalid_argument("Gaussian rest scale is non-finite");
        variance[axis][axis] = scale * scale;
    }
    return multiply(multiply(axes, variance), transpose(axes));
}

[[nodiscard]] AffineMap fitAffineMap(const GaussianSupport& support,
                                     const std::vector<PhysicalPoint>& physicalPoints) {
    if (support.fitWeights.size() < 4)
        throw std::runtime_error("affine Gaussian deformation needs at least four fit points");

    Matrix4 moment{};
    Vector4 rhsX{};
    Vector4 rhsY{};
    Vector4 rhsZ{};
    for (const auto& weightedPoint : support.fitWeights) {
        if (weightedPoint.physicalIndex >= physicalPoints.size())
            throw std::out_of_range("MLS affine-fit support index is out of range");
        const auto& point = physicalPoints[weightedPoint.physicalIndex];
        const Vector4 restBasis = basis(point.restPosition);
        const double weight = weightedPoint.weight;
        for (std::size_t row = 0; row < 4; ++row) {
            rhsX[row] += weight * restBasis[row] * point.position.x;
            rhsY[row] += weight * restBasis[row] * point.position.y;
            rhsZ[row] += weight * restBasis[row] * point.position.z;
            for (std::size_t column = 0; column < 4; ++column)
                moment[row][column] += weight * restBasis[row] * restBasis[column];
        }
    }

    const auto coefficientX = solveLinearSystem(moment, rhsX);
    const auto coefficientY = solveLinearSystem(moment, rhsY);
    const auto coefficientZ = solveLinearSystem(moment, rhsZ);
    AffineMap map;
    map.translation = {coefficientX[0], coefficientY[0], coefficientZ[0]};
    map.linear = {{{coefficientX[1], coefficientX[2], coefficientX[3]},
                   {coefficientY[1], coefficientY[2], coefficientY[3]},
                   {coefficientZ[1], coefficientZ[2], coefficientZ[3]}}};
    return map;
}

struct SymmetricEigenSystem {
    std::array<double, 3> values{};
    Matrix3 vectors{}; // eigenvectors are columns
};

[[nodiscard]] SymmetricEigenSystem diagonalizeSymmetric(Matrix3 matrix) {
    Matrix3 vectors = identity3();
    constexpr std::size_t maximumIterations = 32;
    constexpr double tolerance = 1.0e-14;

    for (std::size_t iteration = 0; iteration < maximumIterations; ++iteration) {
        std::size_t p = 0;
        std::size_t q = 1;
        double largest = std::abs(matrix[p][q]);
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = row + 1; column < 3; ++column) {
                const double candidate = std::abs(matrix[row][column]);
                if (candidate > largest) {
                    largest = candidate;
                    p = row;
                    q = column;
                }
            }
        }
        if (largest < tolerance) break;

        const double app = matrix[p][p];
        const double aqq = matrix[q][q];
        const double apq = matrix[p][q];
        const double tau = (aqq - app) / (2.0 * apq);
        const double tangent =
            (tau >= 0.0 ? 1.0 : -1.0) /
            (std::abs(tau) + std::sqrt(1.0 + tau * tau));
        const double cosine = 1.0 / std::sqrt(1.0 + tangent * tangent);
        const double sine = tangent * cosine;

        matrix[p][p] = app - tangent * apq;
        matrix[q][q] = aqq + tangent * apq;
        matrix[p][q] = 0.0;
        matrix[q][p] = 0.0;

        for (std::size_t row = 0; row < 3; ++row) {
            if (row == p || row == q) continue;
            const double arp = matrix[row][p];
            const double arq = matrix[row][q];
            const double newRp = cosine * arp - sine * arq;
            const double newRq = sine * arp + cosine * arq;
            matrix[row][p] = newRp;
            matrix[p][row] = newRp;
            matrix[row][q] = newRq;
            matrix[q][row] = newRq;
        }

        for (std::size_t row = 0; row < 3; ++row) {
            const double vrp = vectors[row][p];
            const double vrq = vectors[row][q];
            vectors[row][p] = cosine * vrp - sine * vrq;
            vectors[row][q] = sine * vrp + cosine * vrq;
        }
    }

    std::array<std::pair<double, std::size_t>, 3> ordering{{
        {matrix[0][0], 0}, {matrix[1][1], 1}, {matrix[2][2], 2}}};
    std::stable_sort(ordering.begin(), ordering.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });

    SymmetricEigenSystem result;
    Matrix3 sortedVectors{};
    for (std::size_t newAxis = 0; newAxis < 3; ++newAxis) {
        result.values[newAxis] = ordering[newAxis].first;
        const std::size_t oldAxis = ordering[newAxis].second;
        for (std::size_t row = 0; row < 3; ++row)
            sortedVectors[row][newAxis] = vectors[row][oldAxis];
    }
    if (determinant(sortedVectors) < 0.0) {
        for (std::size_t row = 0; row < 3; ++row) sortedVectors[row][2] *= -1.0;
    }
    result.vectors = sortedVectors;
    return result;
}

[[nodiscard]] std::array<double, 4> quaternionFromRotation(const Matrix3& rotation) {
    std::array<double, 4> quaternion{};
    const double trace = rotation[0][0] + rotation[1][1] + rotation[2][2];
    if (trace > 0.0) {
        const double scale = std::sqrt(trace + 1.0) * 2.0;
        quaternion[0] = 0.25 * scale;
        quaternion[1] = (rotation[2][1] - rotation[1][2]) / scale;
        quaternion[2] = (rotation[0][2] - rotation[2][0]) / scale;
        quaternion[3] = (rotation[1][0] - rotation[0][1]) / scale;
    } else if (rotation[0][0] > rotation[1][1] && rotation[0][0] > rotation[2][2]) {
        const double scale = std::sqrt(1.0 + rotation[0][0] - rotation[1][1] - rotation[2][2]) * 2.0;
        quaternion[0] = (rotation[2][1] - rotation[1][2]) / scale;
        quaternion[1] = 0.25 * scale;
        quaternion[2] = (rotation[0][1] + rotation[1][0]) / scale;
        quaternion[3] = (rotation[0][2] + rotation[2][0]) / scale;
    } else if (rotation[1][1] > rotation[2][2]) {
        const double scale = std::sqrt(1.0 + rotation[1][1] - rotation[0][0] - rotation[2][2]) * 2.0;
        quaternion[0] = (rotation[0][2] - rotation[2][0]) / scale;
        quaternion[1] = (rotation[0][1] + rotation[1][0]) / scale;
        quaternion[2] = 0.25 * scale;
        quaternion[3] = (rotation[1][2] + rotation[2][1]) / scale;
    } else {
        const double scale = std::sqrt(1.0 + rotation[2][2] - rotation[0][0] - rotation[1][1]) * 2.0;
        quaternion[0] = (rotation[1][0] - rotation[0][1]) / scale;
        quaternion[1] = (rotation[0][2] + rotation[2][0]) / scale;
        quaternion[2] = (rotation[1][2] + rotation[2][1]) / scale;
        quaternion[3] = 0.25 * scale;
    }

    double normSquared = 0.0;
    for (const double component : quaternion) normSquared += component * component;
    if (!(normSquared > 0.0) || !std::isfinite(normSquared))
        throw std::runtime_error("failed to recover a finite Gaussian orientation");
    const double inverseNorm = 1.0 / std::sqrt(normSquared);
    for (double& component : quaternion) component *= inverseNorm;
    return quaternion;
}

void assignShapeFromCovariance(const Matrix3& covariance, gaussian::GaussianSplat& splat) {
    Matrix3 symmetric = covariance;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = row + 1; column < 3; ++column) {
            const double average = 0.5 * (symmetric[row][column] + symmetric[column][row]);
            symmetric[row][column] = average;
            symmetric[column][row] = average;
        }
    }
    const auto eigen = diagonalizeSymmetric(symmetric);
    constexpr double minimumVariance = 1.0e-24;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double variance = std::max(eigen.values[axis], minimumVariance);
        splat.logScale[axis] = 0.5 * std::log(variance);
    }
    splat.rotation = quaternionFromRotation(eigen.vectors);
}

} // namespace

MlsEmbedding buildMlsEmbedding(const gaussian::GaussianCloud& cloud,
                               const std::vector<PhysicalPoint>& physicalPoints,
                               std::size_t neighborCount) {
    if (cloud.empty()) throw std::invalid_argument("MLS embedding requires at least one Gaussian");
    if (physicalPoints.size() < 4)
        throw std::invalid_argument("3D affine MLS embedding requires at least four physical points");
    if (neighborCount < 4)
        throw std::invalid_argument("MLS neighbor count must be at least four in 3D");

    MlsEmbedding embedding;
    embedding.physicalPointCount = physicalPoints.size();
    embedding.supports.reserve(cloud.size());
    const std::size_t supportCount = std::min(neighborCount, physicalPoints.size());

    std::vector<std::pair<double, std::size_t>> distances;
    distances.reserve(physicalPoints.size());

    for (std::size_t gaussianIndex = 0; gaussianIndex < cloud.size(); ++gaussianIndex) {
        const auto& sourceSplat = cloud.splats[gaussianIndex];
        const auto gaussianPosition = sourceSplat.position;
        distances.clear();
        for (std::size_t physicalIndex = 0; physicalIndex < physicalPoints.size(); ++physicalIndex) {
            const double distanceSquared =
                squaredDistance(gaussianPosition, physicalPoints[physicalIndex].restPosition);
            if (!std::isfinite(distanceSquared))
                throw std::invalid_argument("MLS embedding encountered a non-finite physical position");
            distances.emplace_back(distanceSquared, physicalIndex);
        }
        std::stable_sort(distances.begin(), distances.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.first != rhs.first) return lhs.first < rhs.first;
            return lhs.second < rhs.second;
        });
        distances.resize(supportCount);

        const double farthestSquared = distances.back().first;
        const double kernelScaleSquared = std::max(farthestSquared * 1.000001, 1.0e-18);
        Matrix4 moment{};
        std::vector<double> kernelWeights;
        kernelWeights.reserve(supportCount);
        for (const auto& [distanceSquared, physicalIndex] : distances) {
            const double kernel = std::exp(-distanceSquared / kernelScaleSquared) + 1.0e-12;
            kernelWeights.push_back(kernel);
            const auto physicalBasis = basis(physicalPoints[physicalIndex].restPosition);
            for (std::size_t row = 0; row < 4; ++row)
                for (std::size_t column = 0; column < 4; ++column)
                    moment[row][column] += kernel * physicalBasis[row] * physicalBasis[column];
        }

        const auto coefficients = solveLinearSystem(moment, basis(gaussianPosition));
        GaussianSupport support;
        support.gaussianIndex = gaussianIndex;
        support.restPosition = sourceSplat.position;
        support.restLogScale = sourceSplat.logScale;
        support.restRotation = sourceSplat.rotation;
        support.weights.reserve(supportCount);
        support.fitWeights.reserve(supportCount);

        double weightSum = 0.0;
        math::Vec3 reproducedPosition{};
        for (std::size_t local = 0; local < supportCount; ++local) {
            const std::size_t physicalIndex = distances[local].second;
            const auto physicalBasis = basis(physicalPoints[physicalIndex].restPosition);
            const double weight = kernelWeights[local] * dot4(physicalBasis, coefficients);
            support.weights.push_back({physicalIndex, weight});
            support.fitWeights.push_back({physicalIndex, kernelWeights[local]});
            weightSum += weight;
            reproducedPosition += physicalPoints[physicalIndex].restPosition * weight;
        }
        support.partitionOfUnityError = std::abs(weightSum - 1.0);
        support.affineReproductionError = math::length(reproducedPosition - gaussianPosition);
        embedding.maximumPartitionOfUnityError =
            std::max(embedding.maximumPartitionOfUnityError, support.partitionOfUnityError);
        embedding.maximumAffineReproductionError =
            std::max(embedding.maximumAffineReproductionError, support.affineReproductionError);
        embedding.supports.push_back(std::move(support));
    }
    return embedding;
}

void updateGaussianPositionsFromPhysics(const MlsEmbedding& embedding,
                                        const std::vector<PhysicalPoint>& physicalPoints,
                                        gaussian::GaussianCloud& cloud) {
    if (embedding.physicalPointCount != physicalPoints.size())
        throw std::invalid_argument("MLS physical-point count changed after embedding construction");
    for (const auto& support : embedding.supports) {
        if (support.gaussianIndex >= cloud.size())
            throw std::out_of_range("MLS Gaussian support is out of range for the target cloud");
        math::Vec3 position{};
        for (const auto& weightedPoint : support.weights) {
            if (weightedPoint.physicalIndex >= physicalPoints.size())
                throw std::out_of_range("MLS physical support index is out of range");
            position += physicalPoints[weightedPoint.physicalIndex].position * weightedPoint.weight;
        }
        cloud.splats[support.gaussianIndex].position = position;
    }
}

void updateGaussianGeometryFromPhysics(const MlsEmbedding& embedding,
                                       const std::vector<PhysicalPoint>& physicalPoints,
                                       gaussian::GaussianCloud& cloud) {
    if (embedding.physicalPointCount != physicalPoints.size())
        throw std::invalid_argument("MLS physical-point count changed after embedding construction");
    for (const auto& support : embedding.supports) {
        if (support.gaussianIndex >= cloud.size())
            throw std::out_of_range("MLS Gaussian support is out of range for the target cloud");
        const AffineMap deformation = fitAffineMap(support, physicalPoints);
        auto& splat = cloud.splats[support.gaussianIndex];
        splat.position = applyLinear(deformation.linear, support.restPosition) + deformation.translation;
        const Matrix3 restCovariance = covarianceFromShape(support.restLogScale, support.restRotation);
        const Matrix3 deformedCovariance =
            multiply(multiply(deformation.linear, restCovariance), transpose(deformation.linear));
        assignShapeFromCovariance(deformedCovariance, splat);
    }
}

ForceTransferEvidence transferGaussianForceToPhysics(
    const MlsEmbedding& embedding,
    std::size_t gaussianIndex,
    math::Vec3 gaussianPosition,
    math::Vec3 force,
    const std::vector<PhysicalPoint>& physicalPoints) {
    if (embedding.physicalPointCount != physicalPoints.size())
        throw std::invalid_argument("MLS physical-point count changed after embedding construction");
    const auto& support = supportFor(embedding, gaussianIndex);

    ForceTransferEvidence evidence;
    evidence.physicalForces.assign(physicalPoints.size(), math::Vec3{});
    evidence.requestedForce = force;
    evidence.requestedTorque = math::cross(gaussianPosition, force);

    for (const auto& weightedPoint : support.weights) {
        if (weightedPoint.physicalIndex >= physicalPoints.size())
            throw std::out_of_range("MLS physical support index is out of range");
        const math::Vec3 contribution = force * weightedPoint.weight;
        evidence.physicalForces[weightedPoint.physicalIndex] += contribution;
    }

    for (std::size_t index = 0; index < physicalPoints.size(); ++index) {
        evidence.transferredForce += evidence.physicalForces[index];
        evidence.transferredTorque +=
            math::cross(physicalPoints[index].position, evidence.physicalForces[index]);
    }
    evidence.forceConservationError =
        math::length(evidence.transferredForce - evidence.requestedForce);
    evidence.torqueConservationError =
        math::length(evidence.transferredTorque - evidence.requestedTorque);
    return evidence;
}

} // namespace vulkax::coupling
