#include "vulkax/research/energy_cycle.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace vulkax::research {
namespace {

[[nodiscard]] std::size_t stepsFor(double physicalHorizon, double dt) {
    if (!std::isfinite(physicalHorizon) || physicalHorizon <= 0.0)
        throw std::invalid_argument("energy-cycle horizon must be positive");
    if (!std::isfinite(dt) || dt <= 0.0)
        throw std::invalid_argument("energy-cycle timestep must be positive");
    const double exact = physicalHorizon / dt;
    const auto rounded = static_cast<std::size_t>(std::llround(exact));
    if (rounded == 0 || std::abs(static_cast<double>(rounded) * dt - physicalHorizon) >
                            std::max(1.0e-12, physicalHorizon * 1.0e-10))
        throw std::invalid_argument("energy-cycle horizon must be an integer multiple of dt");
    return rounded;
}

[[nodiscard]] std::string schemeSlug(solvers::MpmTransferScheme scheme) {
    switch (scheme) {
        case solvers::MpmTransferScheme::PIC: return "pic";
        case solvers::MpmTransferScheme::FLIP: return "flip";
        case solvers::MpmTransferScheme::APIC: return "apic";
        case solvers::MpmTransferScheme::APIC_FLIP: return "apic_flip";
    }
    return "unknown";
}

void summarizeScheme(
    TransferEnergyCycleSchemeResult& scheme,
    double meaningfulPeakThresholdFraction) {
    const auto& experiment = scheme.experiment;
    if (experiment.frames.empty() || !(experiment.initialMechanicalEnergy > 0.0))
        throw std::runtime_error("energy-cycle experiment produced no valid frames");

    scheme.minimumMechanicalEnergyFraction = std::numeric_limits<double>::infinity();
    scheme.maximumMechanicalEnergyFraction = 0.0;
    for (const auto& frame : experiment.frames) {
        const double mechanicalFraction = frame.mechanicalEnergy / experiment.initialMechanicalEnergy;
        const double kineticFraction = frame.kineticEnergy / experiment.initialMechanicalEnergy;
        scheme.minimumMechanicalEnergyFraction = std::min(
            scheme.minimumMechanicalEnergyFraction, mechanicalFraction);
        scheme.maximumMechanicalEnergyFraction = std::max(
            scheme.maximumMechanicalEnergyFraction, mechanicalFraction);
        scheme.peakKineticEnergyFraction = std::max(
            scheme.peakKineticEnergyFraction, kineticFraction);
    }
    scheme.finalMechanicalEnergyFraction =
        experiment.frames.back().mechanicalEnergy / experiment.initialMechanicalEnergy;

    std::size_t ordinal = 0;
    for (std::size_t index = 1; index + 1 < experiment.frames.size(); ++index) {
        const auto& previous = experiment.frames[index - 1];
        const auto& current = experiment.frames[index];
        const auto& next = experiment.frames[index + 1];
        if (!(current.kineticEnergy >= previous.kineticEnergy &&
              current.kineticEnergy > next.kineticEnergy))
            continue;

        EnergyCyclePeak peak;
        peak.ordinal = ordinal++;
        peak.step = current.step;
        peak.time = current.time;
        peak.kineticEnergyFraction = current.kineticEnergy / experiment.initialMechanicalEnergy;
        peak.mechanicalEnergyFraction = current.mechanicalEnergy / experiment.initialMechanicalEnergy;
        peak.gaussianDisplacement = current.maximumGaussianDisplacement;
        peak.mlsRmsResidual = current.maximumMlsRmsResidual;
        scheme.kineticPeaks.push_back(peak);

        if (peak.kineticEnergyFraction < meaningfulPeakThresholdFraction) continue;
        if (scheme.meaningfulKineticPeakCount == 0) {
            scheme.firstMeaningfulPeakTime = peak.time;
            scheme.firstMeaningfulPeakMechanicalEnergyFraction = peak.mechanicalEnergyFraction;
            scheme.firstMeaningfulPeakKineticEnergyFraction = peak.kineticEnergyFraction;
        }
        ++scheme.meaningfulKineticPeakCount;
        scheme.lastMeaningfulPeakTime = peak.time;
        scheme.lastMeaningfulPeakMechanicalEnergyFraction = peak.mechanicalEnergyFraction;
        scheme.lastMeaningfulPeakKineticEnergyFraction = peak.kineticEnergyFraction;
    }

    if (scheme.meaningfulKineticPeakCount >= 2) {
        scheme.completedMeaningfulCycles = scheme.meaningfulKineticPeakCount - 1;
        const double cycleCount = static_cast<double>(scheme.completedMeaningfulCycles);
        scheme.meanMeaningfulCyclePeriod =
            (scheme.lastMeaningfulPeakTime - scheme.firstMeaningfulPeakTime) / cycleCount;

        if (scheme.firstMeaningfulPeakMechanicalEnergyFraction > 0.0) {
            scheme.peakToPeakMechanicalEnergyRetention =
                scheme.lastMeaningfulPeakMechanicalEnergyFraction /
                scheme.firstMeaningfulPeakMechanicalEnergyFraction;
            if (scheme.peakToPeakMechanicalEnergyRetention > 0.0)
                scheme.meanMechanicalEnergyRetentionPerCycle = std::pow(
                    scheme.peakToPeakMechanicalEnergyRetention, 1.0 / cycleCount);
        }
        if (scheme.firstMeaningfulPeakKineticEnergyFraction > 0.0) {
            scheme.peakToPeakKineticAmplitudeRetention =
                scheme.lastMeaningfulPeakKineticEnergyFraction /
                scheme.firstMeaningfulPeakKineticEnergyFraction;
            if (scheme.peakToPeakKineticAmplitudeRetention > 0.0)
                scheme.meanKineticAmplitudeRetentionPerCycle = std::pow(
                    scheme.peakToPeakKineticAmplitudeRetention, 1.0 / cycleCount);
        }
    }
}

} // namespace

TransferEnergyCycleResult runTransferEnergyCycleDiagnostic(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    double physicalHorizon,
    double dt,
    double meaningfulPeakThresholdFraction,
    std::vector<solvers::MpmTransferScheme> schemes) {
    if (!std::isfinite(meaningfulPeakThresholdFraction) || meaningfulPeakThresholdFraction < 0.0)
        throw std::invalid_argument("meaningful kinetic-peak threshold must be non-negative");
    if (schemes.empty()) throw std::invalid_argument("energy-cycle diagnostic requires at least one scheme");
    std::sort(schemes.begin(), schemes.end(), [](auto lhs, auto rhs) {
        return static_cast<int>(lhs) < static_cast<int>(rhs);
    });
    if (std::adjacent_find(schemes.begin(), schemes.end()) != schemes.end())
        throw std::invalid_argument("energy-cycle transfer schemes must be unique");

    const std::size_t steps = stepsFor(physicalHorizon, dt);
    TransferEnergyCycleResult result;
    result.physicalHorizon = physicalHorizon;
    result.dt = dt;
    result.meaningfulPeakThresholdFraction = meaningfulPeakThresholdFraction;
    result.schemes.reserve(schemes.size());

    for (const auto transferScheme : schemes) {
        settings.steps = steps;
        settings.dt = dt;
        settings.transferScheme = transferScheme;

        TransferEnergyCycleSchemeResult scheme;
        scheme.scheme = transferScheme;
        scheme.dt = dt;
        scheme.physicalHorizon = physicalHorizon;
        scheme.experiment = runNonlinearDeformableWorld(
            world, activeGaussianIndices, particles, grid, settings);
        summarizeScheme(scheme, meaningfulPeakThresholdFraction);
        result.schemes.push_back(std::move(scheme));
    }
    return result;
}

void writeTransferEnergyCycleSummaryCsv(
    const TransferEnergyCycleResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open energy-cycle summary CSV");
    stream << "scheme,dt,physical_horizon,steps,initial_energy,final_energy_fraction,"
              "min_energy_fraction,max_energy_fraction,peak_kinetic_fraction,all_kinetic_peak_count,"
              "meaningful_kinetic_peak_count,completed_meaningful_cycles,first_peak_time,last_peak_time,"
              "mean_cycle_period,first_peak_total_energy_fraction,last_peak_total_energy_fraction,"
              "peak_to_peak_total_energy_retention,mean_total_energy_retention_per_cycle,"
              "first_peak_kinetic_fraction,last_peak_kinetic_fraction,peak_to_peak_kinetic_retention,"
              "mean_kinetic_retention_per_cycle,max_gaussian_displacement,max_mls_rms_residual,"
              "max_mls_residual,min_J,max_J,max_unaffected_region_drift\n";
    stream << std::setprecision(17);
    for (const auto& scheme : result.schemes) {
        const auto& experiment = scheme.experiment;
        stream << solvers::toString(scheme.scheme) << ','
               << scheme.dt << ',' << scheme.physicalHorizon << ',' << experiment.frames.size() << ','
               << experiment.initialMechanicalEnergy << ',' << scheme.finalMechanicalEnergyFraction << ','
               << scheme.minimumMechanicalEnergyFraction << ',' << scheme.maximumMechanicalEnergyFraction << ','
               << scheme.peakKineticEnergyFraction << ',' << scheme.kineticPeaks.size() << ','
               << scheme.meaningfulKineticPeakCount << ',' << scheme.completedMeaningfulCycles << ','
               << scheme.firstMeaningfulPeakTime << ',' << scheme.lastMeaningfulPeakTime << ','
               << scheme.meanMeaningfulCyclePeriod << ','
               << scheme.firstMeaningfulPeakMechanicalEnergyFraction << ','
               << scheme.lastMeaningfulPeakMechanicalEnergyFraction << ','
               << scheme.peakToPeakMechanicalEnergyRetention << ','
               << scheme.meanMechanicalEnergyRetentionPerCycle << ','
               << scheme.firstMeaningfulPeakKineticEnergyFraction << ','
               << scheme.lastMeaningfulPeakKineticEnergyFraction << ','
               << scheme.peakToPeakKineticAmplitudeRetention << ','
               << scheme.meanKineticAmplitudeRetentionPerCycle << ','
               << experiment.maximumGaussianDisplacement << ','
               << experiment.maximumMlsRmsResidual << ',' << experiment.maximumMlsResidual << ','
               << experiment.minimumDeformationDeterminant << ',' << experiment.maximumDeformationDeterminant << ','
               << experiment.maximumUnaffectedRegionDrift << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing energy-cycle summary CSV");
}

void writeTransferEnergyCycleTraces(
    const TransferEnergyCycleResult& result,
    const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    for (const auto& scheme : result.schemes) {
        const std::string slug = schemeSlug(scheme.scheme);
        std::ofstream trace(directory / ("trace_" + slug + ".csv"));
        if (!trace) throw std::runtime_error("failed to open energy-cycle trace CSV");
        trace << "step,time,kinetic_energy,elastic_energy,mechanical_energy,kinetic_fraction,"
                 "elastic_fraction,mechanical_fraction,relative_energy_drift,min_J,max_J,"
                 "max_mls_rms_residual,max_mls_residual,max_gaussian_displacement,"
                 "unaffected_region_drift\n";
        trace << std::setprecision(17);
        for (const auto& frame : scheme.experiment.frames) {
            const double initial = scheme.experiment.initialMechanicalEnergy;
            trace << frame.step << ',' << frame.time << ','
                  << frame.kineticEnergy << ',' << frame.elasticEnergy << ',' << frame.mechanicalEnergy << ','
                  << frame.kineticEnergy / initial << ',' << frame.elasticEnergy / initial << ','
                  << frame.mechanicalEnergy / initial << ',' << frame.relativeMechanicalEnergyDrift << ','
                  << frame.minimumDeformationDeterminant << ',' << frame.maximumDeformationDeterminant << ','
                  << frame.maximumMlsRmsResidual << ',' << frame.maximumMlsResidual << ','
                  << frame.maximumGaussianDisplacement << ',' << frame.unaffectedRegionDrift << '\n';
        }
        if (!trace) throw std::runtime_error("failed while writing energy-cycle trace CSV");

        std::ofstream peaks(directory / ("peaks_" + slug + ".csv"));
        if (!peaks) throw std::runtime_error("failed to open energy-cycle peaks CSV");
        peaks << "ordinal,step,time,kinetic_fraction,mechanical_fraction,gaussian_displacement,"
                 "mls_rms_residual,meaningful\n";
        peaks << std::setprecision(17);
        for (const auto& peak : scheme.kineticPeaks) {
            peaks << peak.ordinal << ',' << peak.step << ',' << peak.time << ','
                  << peak.kineticEnergyFraction << ',' << peak.mechanicalEnergyFraction << ','
                  << peak.gaussianDisplacement << ',' << peak.mlsRmsResidual << ','
                  << (peak.kineticEnergyFraction >= result.meaningfulPeakThresholdFraction ? 1 : 0) << '\n';
        }
        if (!peaks) throw std::runtime_error("failed while writing energy-cycle peaks CSV");
    }
}

} // namespace vulkax::research
