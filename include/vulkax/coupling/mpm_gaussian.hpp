#pragma once

#include "vulkax/coupling/mls_embedding.hpp"
#include "vulkax/solvers/mpm.hpp"

#include <cstddef>
#include <vector>

namespace vulkax::coupling {

struct MpmGaussianBinding {
    MlsEmbedding embedding;
};

[[nodiscard]] std::vector<PhysicalPoint> mpmPhysicalPoints(
    const std::vector<solvers::MpmParticle>& particles);

[[nodiscard]] MpmGaussianBinding bindGaussianCloudToMpm(
    const gaussian::GaussianCloud& cloud,
    const std::vector<solvers::MpmParticle>& particles,
    std::size_t neighborCount = 12);

void updateGaussianCloudFromMpm(
    const MpmGaussianBinding& binding,
    const std::vector<solvers::MpmParticle>& particles,
    gaussian::GaussianCloud& cloud);

[[nodiscard]] ForceTransferEvidence applyGaussianForceToMpm(
    const MpmGaussianBinding& binding,
    std::size_t gaussianIndex,
    math::Vec3 force,
    const gaussian::GaussianCloud& cloud,
    std::vector<solvers::MpmParticle>& particles);

} // namespace vulkax::coupling
