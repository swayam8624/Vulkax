#include "vulkax/coupling/mpm_gaussian.hpp"

#include <stdexcept>

namespace vulkax::coupling {

std::vector<PhysicalPoint> mpmPhysicalPoints(
    const std::vector<solvers::MpmParticle>& particles) {
    std::vector<PhysicalPoint> points;
    points.reserve(particles.size());
    for (std::size_t index = 0; index < particles.size(); ++index) {
        const auto& particle = particles[index];
        points.push_back({
            particle.id != 0 ? particle.id : static_cast<std::uint64_t>(index + 1U),
            particle.restPosition,
            particle.position,
            particle.velocity,
        });
    }
    return points;
}

MpmGaussianBinding bindGaussianCloudToMpm(
    const gaussian::GaussianCloud& cloud,
    const std::vector<solvers::MpmParticle>& particles,
    std::size_t neighborCount) {
    if (particles.empty())
        throw std::invalid_argument("Gaussian-to-MPM binding requires physical particles");
    const auto points = mpmPhysicalPoints(particles);
    return {buildMlsEmbedding(cloud, points, neighborCount)};
}

void updateGaussianCloudFromMpm(
    const MpmGaussianBinding& binding,
    const std::vector<solvers::MpmParticle>& particles,
    gaussian::GaussianCloud& cloud) {
    const auto points = mpmPhysicalPoints(particles);
    updateGaussianGeometryFromPhysics(binding.embedding, points, cloud);
}

ForceTransferEvidence applyGaussianForceToMpm(
    const MpmGaussianBinding& binding,
    std::size_t gaussianIndex,
    math::Vec3 force,
    const gaussian::GaussianCloud& cloud,
    std::vector<solvers::MpmParticle>& particles) {
    if (gaussianIndex >= cloud.size())
        throw std::out_of_range("Gaussian force target is outside the cloud");
    const auto points = mpmPhysicalPoints(particles);
    auto evidence = transferGaussianForceToPhysics(
        binding.embedding, gaussianIndex, cloud.splats[gaussianIndex].position, force, points);
    if (evidence.physicalForces.size() != particles.size())
        throw std::runtime_error("Gaussian-to-MPM force transfer returned the wrong support size");
    for (std::size_t index = 0; index < particles.size(); ++index)
        particles[index].externalForce += evidence.physicalForces[index];
    return evidence;
}

} // namespace vulkax::coupling
