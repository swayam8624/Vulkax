#include "vulkax/research/affine_flip_sweep.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vulkax::research {
namespace {

[[nodiscard]] double particlePositionRms(
    const std::vector<solvers::MpmParticle>& lhs,
    const std::vector<solvers::MpmParticle>& rhs) {
    if (lhs.size() != rhs.size() || lhs.empty())
        throw std::invalid_argument("affine FLIP particle position comparison size mismatch");
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
    if (lhs.size() != rhs.size() || lhs.empty())
        throw std::invalid_argument("affine FLIP particle velocity comparison size mismatch");
    double squared = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto delta = lhs[index].velocity - rhs[index].velocity;
        squared += math::dot(delta, delta);
    }
    return std::sqrt(squared / static_cast<double>(lhs.size()));
}

[[nodiscard]] double gaussianPositionRms(
    const gaussian::GaussianCloud& lhs,
    const gaussian::GaussianCloud& rhs,
    const std::vector<std::size_t>& activeGaussianIndices) {
    if (lhs.size() != rhs.size() || activeGaussianIndices.empty())
        throw std::invalid_argument("affine FLIP Gaussian comparison size mismatch");
    double squared = 0.0;
    for (const auto index : activeGaussianIndices) {
        if (index >= lhs.size()) throw std::out_of_range("affine FLIP Gaussian comparison index is invalid");
        const auto delta = lhs.splats[index].position - rhs.splats[index].position;
        squared += math::dot(delta, delta);
    }
    return std::sqrt(squared / static_cast<double>(activeGaussianIndices.size()));
}

[[nodiscard]] std::string blendLabel(double blend) {
    std::ostringstream stream;
    stream << "APIC-FLIP-" << std::fixed << std::setprecision(2) << blend;
    return stream.str();
}

[[nodiscard]] TransferEnergyCycleSchemeResult runCandidate(
    const gaussian::GaussianCloud& world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const std::vector<solvers::MpmParticle>& particles,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    double physicalHorizon,
    double dt,
    double meaningfulPeakThresholdFraction,
    solvers::MpmTransferScheme scheme,
    double flipBlend) {
    settings.flipBlend = flipBlend;
    const auto result = runTransferEnergyCycleDiagnostic(
        world, activeGaussianIndices, particles, grid, settings,
        physicalHorizon, dt, meaningfulPeakThresholdFraction, {scheme});
    if (result.schemes.size() != 1)
        throw std::runtime_error("affine FLIP candidate did not produce exactly one cycle result");
    return result.schemes.front();
}

[[nodiscard]] double jExcursion(const NonlinearDeformableWorldResult& experiment) noexcept {
    return std::max(
        std::abs(1.0 - experiment.minimumDeformationDeterminant),
        std::abs(experiment.maximumDeformationDeterminant - 1.0));
}

[[nodiscard]] bool hasValidParetoObjectives(const AffineFlipBlendEntry& entry) noexcept {
    const auto& cycle = entry.cycle;
    const auto& experiment = cycle.experiment;
    return cycle.completedMeaningfulCycles > 0 &&
           std::isfinite(cycle.meanMechanicalEnergyRetentionPerCycle) &&
           cycle.meanMechanicalEnergyRetentionPerCycle > 0.0 &&
           std::isfinite(cycle.meanKineticAmplitudeRetentionPerCycle) &&
           cycle.meanKineticAmplitudeRetentionPerCycle > 0.0 &&
           std::isfinite(experiment.maximumMlsRmsResidual) &&
           experiment.maximumMlsRmsResidual >= 0.0 &&
           std::isfinite(entry.jExcursion) && entry.jExcursion >= 0.0;
}

[[nodiscard]] bool paretoDominates(
    const AffineFlipBlendEntry& lhs,
    const AffineFlipBlendEntry& rhs) noexcept {
    if (!lhs.paretoEligible || !rhs.paretoEligible) return false;
    constexpr double epsilon = 1.0e-12;
    const auto& a = lhs.cycle;
    const auto& b = rhs.cycle;

    const bool noWorse =
        a.meanMechanicalEnergyRetentionPerCycle + epsilon >= b.meanMechanicalEnergyRetentionPerCycle &&
        a.meanKineticAmplitudeRetentionPerCycle + epsilon >= b.meanKineticAmplitudeRetentionPerCycle &&
        a.experiment.maximumMlsRmsResidual <= b.experiment.maximumMlsRmsResidual + epsilon &&
        lhs.jExcursion <= rhs.jExcursion + epsilon;
    if (!noWorse) return false;

    return a.meanMechanicalEnergyRetentionPerCycle > b.meanMechanicalEnergyRetentionPerCycle + epsilon ||
           a.meanKineticAmplitudeRetentionPerCycle > b.meanKineticAmplitudeRetentionPerCycle + epsilon ||
           a.experiment.maximumMlsRmsResidual + epsilon < b.experiment.maximumMlsRmsResidual ||
           lhs.jExcursion + epsilon < rhs.jExcursion;
}

[[nodiscard]] bool betterRecommendation(
    const AffineFlipBlendEntry& lhs,
    const AffineFlipBlendEntry& rhs) noexcept {
    constexpr double epsilon = 1.0e-12;
    if (lhs.cycle.meanKineticAmplitudeRetentionPerCycle >
        rhs.cycle.meanKineticAmplitudeRetentionPerCycle + epsilon)
        return true;
    if (rhs.cycle.meanKineticAmplitudeRetentionPerCycle >
        lhs.cycle.meanKineticAmplitudeRetentionPerCycle + epsilon)
        return false;
    if (lhs.cycle.meanMechanicalEnergyRetentionPerCycle >
        rhs.cycle.meanMechanicalEnergyRetentionPerCycle + epsilon)
        return true;
    if (rhs.cycle.meanMechanicalEnergyRetentionPerCycle >
        lhs.cycle.meanMechanicalEnergyRetentionPerCycle + epsilon)
        return false;
    if (lhs.cycle.experiment.maximumMlsRmsResidual + epsilon <
        rhs.cycle.experiment.maximumMlsRmsResidual)
        return true;
    if (rhs.cycle.experiment.maximumMlsRmsResidual + epsilon <
        lhs.cycle.experiment.maximumMlsRmsResidual)
        return false;
    return lhs.jExcursion < rhs.jExcursion;
}

} // namespace

void analyzeAffineFlipBlendSweep(AffineFlipBlendSweepResult& result) {
    result.recommendedIndex.reset();
    if (result.entries.empty()) return;

    std::optional<std::size_t> apicIndex;
    for (std::size_t index = 0; index < result.entries.size(); ++index) {
        auto& entry = result.entries[index];
        entry.jExcursion = jExcursion(entry.cycle.experiment);
        entry.paretoEligible = hasValidParetoObjectives(entry);
        entry.onParetoFrontier = false;
        entry.dominatesApic = false;
        if (!apicIndex && entry.scheme == solvers::MpmTransferScheme::APIC)
            apicIndex = index;
    }
    if (!apicIndex) throw std::invalid_argument("affine FLIP Pareto analysis requires a pure APIC reference");

    for (std::size_t index = 0; index < result.entries.size(); ++index) {
        auto& candidate = result.entries[index];
        if (!candidate.paretoEligible) continue;
        bool dominated = false;
        for (std::size_t other = 0; other < result.entries.size(); ++other) {
            if (index == other) continue;
            if (paretoDominates(result.entries[other], candidate)) {
                dominated = true;
                break;
            }
        }
        candidate.onParetoFrontier = !dominated;
    }

    const auto& apic = result.entries[*apicIndex];
    if (!apic.paretoEligible) return;
    for (std::size_t index = 0; index < result.entries.size(); ++index) {
        if (index == *apicIndex) continue;
        auto& candidate = result.entries[index];
        candidate.dominatesApic = paretoDominates(candidate, apic);
        if (!candidate.dominatesApic) continue;
        if (!result.recommendedIndex ||
            betterRecommendation(candidate, result.entries[*result.recommendedIndex]))
            result.recommendedIndex = index;
    }
}

AffineFlipBlendSweepResult runAffineFlipBlendSweep(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    double physicalHorizon,
    double dt,
    std::vector<double> flipBlends,
    double meaningfulPeakThresholdFraction) {
    if (flipBlends.empty()) throw std::invalid_argument("affine FLIP sweep needs at least one blend value");
    for (const double blend : flipBlends) {
        if (!std::isfinite(blend) || blend < 0.0 || blend > 1.0)
            throw std::invalid_argument("affine FLIP sweep blend must lie in [0, 1]");
    }
    std::sort(flipBlends.begin(), flipBlends.end());
    flipBlends.erase(std::unique(flipBlends.begin(), flipBlends.end()), flipBlends.end());
    if (std::find(flipBlends.begin(), flipBlends.end(), 0.0) == flipBlends.end())
        throw std::invalid_argument("affine FLIP sweep must include beta=0 endpoint validation");

    AffineFlipBlendSweepResult result;
    result.physicalHorizon = physicalHorizon;
    result.dt = dt;
    result.meaningfulPeakThresholdFraction = meaningfulPeakThresholdFraction;
    result.entries.reserve(flipBlends.size() + 2U);

    AffineFlipBlendEntry apic;
    apic.label = "APIC";
    apic.scheme = solvers::MpmTransferScheme::APIC;
    apic.flipBlend = 0.0;
    apic.cycle = runCandidate(
        world, activeGaussianIndices, particles, grid, settings,
        physicalHorizon, dt, meaningfulPeakThresholdFraction,
        solvers::MpmTransferScheme::APIC, 0.0);
    result.entries.push_back(std::move(apic));

    for (const double blend : flipBlends) {
        AffineFlipBlendEntry entry;
        entry.label = blendLabel(blend);
        entry.scheme = solvers::MpmTransferScheme::APIC_FLIP;
        entry.flipBlend = blend;
        entry.cycle = runCandidate(
            world, activeGaussianIndices, particles, grid, settings,
            physicalHorizon, dt, meaningfulPeakThresholdFraction,
            solvers::MpmTransferScheme::APIC_FLIP, blend);
        result.entries.push_back(std::move(entry));
    }

    AffineFlipBlendEntry flip;
    flip.label = "FLIP";
    flip.scheme = solvers::MpmTransferScheme::FLIP;
    flip.flipBlend = 1.0;
    flip.cycle = runCandidate(
        world, activeGaussianIndices, particles, grid, settings,
        physicalHorizon, dt, meaningfulPeakThresholdFraction,
        solvers::MpmTransferScheme::FLIP, 0.0);
    result.entries.push_back(std::move(flip));

    const auto& apicReference = result.entries.front().cycle.experiment;
    for (auto& entry : result.entries) {
        entry.particlePositionRmsToApic = particlePositionRms(
            entry.cycle.experiment.finalParticles, apicReference.finalParticles);
        entry.particleVelocityRmsToApic = particleVelocityRms(
            entry.cycle.experiment.finalParticles, apicReference.finalParticles);
        entry.gaussianPositionRmsToApic = gaussianPositionRms(
            entry.cycle.experiment.finalWorld, apicReference.finalWorld, activeGaussianIndices);
    }
    analyzeAffineFlipBlendSweep(result);
    return result;
}

void writeAffineFlipBlendSweepCsv(
    const AffineFlipBlendSweepResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open affine FLIP sweep CSV");
    stream << "label,scheme,flip_blend,meaningful_peaks,completed_cycles,mean_cycle_period,"
              "final_energy_fraction,min_energy_fraction,peak_kinetic_fraction,"
              "mean_total_energy_retention_per_cycle,mean_kinetic_retention_per_cycle,"
              "max_gaussian_displacement,max_mls_rms_residual,max_mls_residual,min_J,max_J,j_excursion,"
              "max_unaffected_region_drift,particle_position_rms_to_apic,"
              "particle_velocity_rms_to_apic,gaussian_position_rms_to_apic,"
              "pareto_eligible,on_pareto_frontier,dominates_apic,recommended\n";
    stream << std::setprecision(17);
    for (std::size_t index = 0; index < result.entries.size(); ++index) {
        const auto& entry = result.entries[index];
        const auto& cycle = entry.cycle;
        const auto& experiment = cycle.experiment;
        stream << entry.label << ',' << solvers::toString(entry.scheme) << ',' << entry.flipBlend << ','
               << cycle.meaningfulKineticPeakCount << ',' << cycle.completedMeaningfulCycles << ','
               << cycle.meanMeaningfulCyclePeriod << ',' << cycle.finalMechanicalEnergyFraction << ','
               << cycle.minimumMechanicalEnergyFraction << ',' << cycle.peakKineticEnergyFraction << ','
               << cycle.meanMechanicalEnergyRetentionPerCycle << ','
               << cycle.meanKineticAmplitudeRetentionPerCycle << ','
               << experiment.maximumGaussianDisplacement << ',' << experiment.maximumMlsRmsResidual << ','
               << experiment.maximumMlsResidual << ',' << experiment.minimumDeformationDeterminant << ','
               << experiment.maximumDeformationDeterminant << ',' << entry.jExcursion << ','
               << experiment.maximumUnaffectedRegionDrift << ','
               << entry.particlePositionRmsToApic << ',' << entry.particleVelocityRmsToApic << ','
               << entry.gaussianPositionRmsToApic << ',' << (entry.paretoEligible ? 1 : 0) << ','
               << (entry.onParetoFrontier ? 1 : 0) << ',' << (entry.dominatesApic ? 1 : 0) << ','
               << (result.recommendedIndex && *result.recommendedIndex == index ? 1 : 0) << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing affine FLIP sweep CSV");
}

void writeAffineFlipBlendPeaksCsv(
    const AffineFlipBlendSweepResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open affine FLIP peak CSV");
    stream << "label,scheme,flip_blend,ordinal,step,time,kinetic_fraction,mechanical_fraction,"
              "gaussian_displacement,mls_rms_residual,meaningful\n";
    stream << std::setprecision(17);
    for (const auto& entry : result.entries) {
        for (const auto& peak : entry.cycle.kineticPeaks) {
            stream << entry.label << ',' << solvers::toString(entry.scheme) << ',' << entry.flipBlend << ','
                   << peak.ordinal << ',' << peak.step << ',' << peak.time << ','
                   << peak.kineticEnergyFraction << ',' << peak.mechanicalEnergyFraction << ','
                   << peak.gaussianDisplacement << ',' << peak.mlsRmsResidual << ','
                   << (peak.kineticEnergyFraction >= result.meaningfulPeakThresholdFraction ? 1 : 0) << '\n';
        }
    }
    if (!stream) throw std::runtime_error("failed while writing affine FLIP peak CSV");
}

void writeAffineFlipBlendParetoCsv(
    const AffineFlipBlendSweepResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open affine FLIP Pareto CSV");
    stream << "label,scheme,flip_blend,total_energy_retention_per_cycle,kinetic_retention_per_cycle,"
              "max_mls_rms_residual,j_excursion,particle_position_rms_to_apic,"
              "particle_velocity_rms_to_apic,gaussian_position_rms_to_apic,dominates_apic,recommended\n";
    stream << std::setprecision(17);
    for (std::size_t index = 0; index < result.entries.size(); ++index) {
        const auto& entry = result.entries[index];
        if (!entry.onParetoFrontier) continue;
        stream << entry.label << ',' << solvers::toString(entry.scheme) << ',' << entry.flipBlend << ','
               << entry.cycle.meanMechanicalEnergyRetentionPerCycle << ','
               << entry.cycle.meanKineticAmplitudeRetentionPerCycle << ','
               << entry.cycle.experiment.maximumMlsRmsResidual << ',' << entry.jExcursion << ','
               << entry.particlePositionRmsToApic << ',' << entry.particleVelocityRmsToApic << ','
               << entry.gaussianPositionRmsToApic << ',' << (entry.dominatesApic ? 1 : 0) << ','
               << (result.recommendedIndex && *result.recommendedIndex == index ? 1 : 0) << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing affine FLIP Pareto CSV");
}

} // namespace vulkax::research
