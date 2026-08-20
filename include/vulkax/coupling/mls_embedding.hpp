#pragma once

#include "vulkax/core/math.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"

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
    std::vector<SupportWeight> weights;
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

[[nodiscard]] ForceTransferEvidence transferGaussianForceToPhysics(
    const MlsEmbedding& embedding,
    std::size_t gaussianIndex,
    math::Vec3 gaussianPosition,
    math::Vec3 force,
    const std::vector<PhysicalPoint>& physicalPoints);

} // namespace vulkax::coupling
