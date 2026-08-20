#pragma once

#include "vulkax/core/math.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vulkax::coupling {

struct PhysicalPoint {
    std::uint64_t id{};
    math::Vec3 restPosition{};
    math::Vec3 position{};
    math::Vec3 velocity{};
};

struct SupportWeight {
    std::size_t physicalIndex{};
    double weight{};
};

struct GaussianSupport {
    std::size_t gaussianIndex{};
    math::Vec3 restPosition{};
    std::array<double, 3> restLogScale{};
    std::array<double, 4> restRotation{1.0, 0.0, 0.0, 0.0};
    std::vector<SupportWeight> weights;
    std::vector<SupportWeight> fitWeights;
    double partitionOfUnityError{};
    double affineReproductionError{};
};

struct MlsEmbedding {
    std::size_t physicalPointCount{};
    std::vector<GaussianSupport> supports;
    double maximumPartitionOfUnityError{};
    double maximumAffineReproductionError{};
};

struct ForceTransferEvidence {
    std::vector<math::Vec3> physicalForces;
    math::Vec3 requestedForce{};
    math::Vec3 transferredForce{};
    math::Vec3 requestedTorque{};
    math::Vec3 transferredTorque{};
    double forceConservationError{};
    double torqueConservationError{};
};

[[nodiscard]] MlsEmbedding buildMlsEmbedding(
    const gaussian::GaussianCloud& cloud,
    const std::vector<PhysicalPoint>& physicalPoints,
    std::size_t neighborCount = 12);

void updateGaussianPositionsFromPhysics(
    const MlsEmbedding& embedding,
    const std::vector<PhysicalPoint>& physicalPoints,
    gaussian::GaussianCloud& cloud);

// Fits a local affine map from each Gaussian's physical support, then transports
// both its center and its full covariance. Rest geometry stored in the embedding
// makes this operation non-accumulating: repeated calls always map from the same
// captured reference state.
void updateGaussianGeometryFromPhysics(
    const MlsEmbedding& embedding,
    const std::vector<PhysicalPoint>& physicalPoints,
    gaussian::GaussianCloud& cloud);

[[nodiscard]] ForceTransferEvidence transferGaussianForceToPhysics(
    const MlsEmbedding& embedding,
    std::size_t gaussianIndex,
    math::Vec3 gaussianPosition,
    math::Vec3 force,
    const std::vector<PhysicalPoint>& physicalPoints);

} // namespace vulkax::coupling
