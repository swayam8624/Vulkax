#include "vulkax/research/transfer_ablation.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <utility>

namespace vulkax::research {

MpmTransferAblationResult runMpmTransferAblation(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    double physicalHorizon,
    std::vector<double> timesteps,
    std::vector<solvers::MpmTransferScheme> schemes) {
    if (schemes.empty())
        throw std::invalid_argument("MPM transfer ablation requires at least one scheme");
    std::sort(schemes.begin(), schemes.end(), [](auto lhs, auto rhs) {
        return static_cast<int>(lhs) < static_cast<int>(rhs);
    });
    if (std::adjacent_find(schemes.begin(), schemes.end()) != schemes.end())
        throw std::invalid_argument("MPM transfer ablation schemes must be unique");

    MpmTransferAblationResult result;
    result.physicalHorizon = physicalHorizon;
    result.entries.reserve(schemes.size());

    for (const auto scheme : schemes) {
        settings.transferScheme = scheme;
        MpmTransferAblationEntry entry;
        entry.scheme = scheme;
        entry.timestepSweep = runNonlinearTimestepSweep(
            world,
            activeGaussianIndices,
            particles,
            grid,
            settings,
            physicalHorizon,
            timesteps);
        result.entries.push_back(std::move(entry));
    }
    return result;
}

void writeMpmTransferAblationCsv(
    const MpmTransferAblationResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open MPM transfer-ablation CSV output");
    stream << "scheme,dt,steps,final_time,initial_energy,final_energy,max_relative_energy_drift,"
              "min_J,max_J,max_mass_error,max_momentum_error,max_force_balance_error,"
              "max_momentum_balance_error,max_center_of_mass_drift,max_mls_rms_residual,"
              "max_mls_residual,max_gaussian_displacement,max_unaffected_region_drift,"
              "particle_position_rms_to_scheme_finest,particle_velocity_rms_to_scheme_finest,"
              "gaussian_position_rms_to_scheme_finest,observed_particle_position_order,"
              "observed_particle_velocity_order,observed_gaussian_position_order\n";
    stream << std::setprecision(17);

    for (const auto& entry : result.entries) {
        for (const auto& level : entry.timestepSweep.levels) {
            const auto& experiment = level.experiment;
            stream << solvers::toString(entry.scheme) << ','
                   << level.dt << ',' << level.steps << ',' << level.finalTime << ','
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
                   << entry.timestepSweep.observedParticlePositionOrder << ','
                   << entry.timestepSweep.observedParticleVelocityOrder << ','
                   << entry.timestepSweep.observedGaussianPositionOrder << '\n';
        }
    }
    if (!stream) throw std::runtime_error("failed while writing MPM transfer-ablation CSV output");
}

} // namespace vulkax::research
