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
using Matrix4 = std::array<std::array<double, 4>, 4>;

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
        const auto gaussianPosition = cloud.splats[gaussianIndex].position;
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
        support.weights.reserve(supportCount);

        double weightSum = 0.0;
        math::Vec3 reproducedPosition{};
        for (std::size_t local = 0; local < supportCount; ++local) {
            const std::size_t physicalIndex = distances[local].second;
            const auto physicalBasis = basis(physicalPoints[physicalIndex].restPosition);
            const double weight = kernelWeights[local] * dot4(physicalBasis, coefficients);
            support.weights.push_back({physicalIndex, weight});
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
