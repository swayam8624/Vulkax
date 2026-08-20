#include "vulkax/research/timestep_convergence.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vulkax::research {
namespace {

[[nodiscard]] double particlePositionRms(
    const std::vector<solvers::MpmParticle>& lhs,
    const std::vector<solvers::MpmParticle>& rhs) {
    if (lhs.size() != rhs.size())
        throw std::invalid_argument("timestep comparison requires equal particle counts");
    if (lhs.empty()) return 0.0;
    double squared = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto delta = lhs[index].position - rhs[index].position;
        squared += math::dot(delta, delta);
    }
    return std::sqrt(squared / static_cast<double>(lhs.size()));
}

[[nodiscard]] double particleVelocityRms(
    const std::vector<solvers::MpmParticle>& lhs,
    const std::vector<solvers::MpmParticle>& rhs) {
    if (lhs.size() != rhs.size())
        throw std::invalid_argument("timestep comparison requires equal particle counts");
    if (lhs.empty()) return 0.0;
    double squared = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto delta = lhs[index].velocity - rhs[index].velocity;
        squared += math::dot(delta, delta);
    }
    return std::sqrt(squared / static_cast<double>(lhs.size()));
}

[[nodiscard]] double gaussianPositionRms(
    const gaussian::GaussianCloud& lhs,
    const gaussian::GaussianCloud& rhs) {
    if (lhs.size() != rhs.size())
        throw std::invalid_argument("timestep comparison requires equal Gaussian counts");
    if (lhs.empty()) return 0.0;
    double squared = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto delta = lhs.splats[index].position - rhs.splats[index].position;
        squared += math::dot(delta, delta);
    }
    return std::sqrt(squared / static_cast<double>(lhs.size()));
}

[[nodiscard]] double observedOrder(double coarseMedium,
                                   double mediumFine,
                                   double refinementRatio) noexcept {
    if (!(coarseMedium > 0.0) || !(mediumFine > 0.0) || !(refinementRatio > 1.0))
        return std::numeric_limits<double>::quiet_NaN();
    return std::log(coarseMedium / mediumFine) / std::log(refinementRatio);
}

} // namespace

NonlinearTimestepSweepResult runNonlinearTimestepSweep(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    double physicalHorizon,
    std::vector<double> timesteps) {
    if (!std::isfinite(physicalHorizon) || physicalHorizon <= 0.0)
        throw std::invalid_argument("timestep sweep physical horizon must be finite and positive");
    if (timesteps.size() < 2)
        throw std::invalid_argument("timestep sweep requires at least two levels");
    for (const double dt : timesteps) {
        if (!std::isfinite(dt) || dt <= 0.0)
            throw std::invalid_argument("timestep sweep contains a non-positive or non-finite timestep");
    }
    std::sort(timesteps.begin(), timesteps.end(), std::greater<double>());
    for (std::size_t index = 1; index < timesteps.size(); ++index) {
        if (timesteps[index] == timesteps[index - 1])
            throw std::invalid_argument("timestep sweep levels must be unique");
    }

    const gaussian::GaussianCloud referenceWorld = world;
    const std::vector<solvers::MpmParticle> referenceParticles = particles;

    NonlinearTimestepSweepResult sweep;
    sweep.observedParticlePositionOrder = std::numeric_limits<double>::quiet_NaN();
    sweep.observedParticleVelocityOrder = std::numeric_limits<double>::quiet_NaN();
    sweep.observedGaussianPositionOrder = std::numeric_limits<double>::quiet_NaN();
    sweep.levels.reserve(timesteps.size());

    for (const double dt : timesteps) {
        const double exactSteps = physicalHorizon / dt;
        const auto roundedSteps = static_cast<std::size_t>(std::llround(exactSteps));
        if (roundedSteps == 0)
            throw std::invalid_argument("timestep sweep level has zero integration steps");
        const double finalTime = static_cast<double>(roundedSteps) * dt;
        const double horizonTolerance = std::max(1.0e-12, 1.0e-10 * physicalHorizon);
        if (std::abs(finalTime - physicalHorizon) > horizonTolerance)
            throw std::invalid_argument("timestep does not divide the requested physical horizon");

        auto levelSettings = settings;
        levelSettings.dt = dt;
        levelSettings.steps = roundedSteps;

        NonlinearTimestepLevel level;
        level.dt = dt;
        level.steps = roundedSteps;
        level.finalTime = finalTime;
        level.experiment = runNonlinearDeformableWorld(
            referenceWorld,
            activeGaussianIndices,
            referenceParticles,
            grid,
            levelSettings);
        sweep.levels.push_back(std::move(level));
    }

    const auto& finest = sweep.levels.back().experiment;
    for (auto& level : sweep.levels) {
        level.particlePositionRmsToFinest = particlePositionRms(
            level.experiment.finalParticles, finest.finalParticles);
        level.particleVelocityRmsToFinest = particleVelocityRms(
            level.experiment.finalParticles, finest.finalParticles);
        level.gaussianPositionRmsToFinest = gaussianPositionRms(
            level.experiment.finalWorld, finest.finalWorld);
    }

    if (sweep.levels.size() >= 3) {
        const std::size_t coarse = sweep.levels.size() - 3;
        const std::size_t medium = sweep.levels.size() - 2;
        const std::size_t fine = sweep.levels.size() - 1;
        const double ratioCoarse = sweep.levels[coarse].dt / sweep.levels[medium].dt;
        const double ratioFine = sweep.levels[medium].dt / sweep.levels[fine].dt;
        const double ratioTolerance = 1.0e-10 * std::max(ratioCoarse, ratioFine);
        if (std::abs(ratioCoarse - ratioFine) <= ratioTolerance && ratioCoarse > 1.0) {
            const double particlePositionCoarseMedium = particlePositionRms(
                sweep.levels[coarse].experiment.finalParticles,
                sweep.levels[medium].experiment.finalParticles);
            const double particlePositionMediumFine = particlePositionRms(
                sweep.levels[medium].experiment.finalParticles,
                sweep.levels[fine].experiment.finalParticles);
            const double particleVelocityCoarseMedium = particleVelocityRms(
                sweep.levels[coarse].experiment.finalParticles,
                sweep.levels[medium].experiment.finalParticles);
            const double particleVelocityMediumFine = particleVelocityRms(
                sweep.levels[medium].experiment.finalParticles,
                sweep.levels[fine].experiment.finalParticles);
            const double gaussianPositionCoarseMedium = gaussianPositionRms(
                sweep.levels[coarse].experiment.finalWorld,
                sweep.levels[medium].experiment.finalWorld);
            const double gaussianPositionMediumFine = gaussianPositionRms(
                sweep.levels[medium].experiment.finalWorld,
                sweep.levels[fine].experiment.finalWorld);

            sweep.observedParticlePositionOrder = observedOrder(
                particlePositionCoarseMedium, particlePositionMediumFine, ratioCoarse);
            sweep.observedParticleVelocityOrder = observedOrder(
                particleVelocityCoarseMedium, particleVelocityMediumFine, ratioCoarse);
            sweep.observedGaussianPositionOrder = observedOrder(
                gaussianPositionCoarseMedium, gaussianPositionMediumFine, ratioCoarse);
        }
    }
    return sweep;
}

void writeNonlinearTimestepSweepCsv(
    const NonlinearTimestepSweepResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open timestep-sweep CSV output");
    stream << "dt,steps,final_time,initial_energy,final_energy,max_relative_energy_drift,"
              "min_J,max_J,max_mass_error,max_momentum_error,max_force_balance_error,"
              "max_momentum_balance_error,max_center_of_mass_drift,max_mls_rms_residual,"
              "max_mls_residual,max_gaussian_displacement,max_unaffected_region_drift,"
              "particle_position_rms_to_finest,particle_velocity_rms_to_finest,"
              "gaussian_position_rms_to_finest,observed_particle_position_order,"
              "observed_particle_velocity_order,observed_gaussian_position_order\n";
    stream << std::setprecision(17);
    for (const auto& level : result.levels) {
        const auto& experiment = level.experiment;
        stream << level.dt << ',' << level.steps << ',' << level.finalTime << ','
               << experiment.initialMechanicalEnergy << ',' << experiment.finalMechanicalEnergy << ','
               << experiment.maximumRelativeMechanicalEnergyDrift << ','
               << experiment.minimumDeformationDeterminant << ','
               << experiment.maximumDeformationDeterminant << ','
               << experiment.maximumMassConservationError << ','
               << experiment.maximumMomentumConservationError << ','
               << experiment.maximumForceBalanceError << ','
               << experiment.maximumMomentumBalanceError << ','
               << experiment.maximumCenterOfMassDrift << ','
               << experiment.maximumMlsRmsResidual << ','
               << experiment.maximumMlsResidual << ','
               << experiment.maximumGaussianDisplacement << ','
               << experiment.maximumUnaffectedRegionDrift << ','
               << level.particlePositionRmsToFinest << ','
               << level.particleVelocityRmsToFinest << ','
               << level.gaussianPositionRmsToFinest << ','
               << result.observedParticlePositionOrder << ','
               << result.observedParticleVelocityOrder << ','
               << result.observedGaussianPositionOrder << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing timestep-sweep CSV output");
}

} // namespace vulkax::research
