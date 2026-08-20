#include "vulkax/research/transfer_diagnostics.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vulkax::research {
namespace {

DissipationFloorEstimate estimateFloor(
    const NonlinearTimestepLevel& coarse,
    const NonlinearTimestepLevel& medium,
    const NonlinearTimestepLevel& fine) noexcept {
    DissipationFloorEstimate out;
    const double ratioA = coarse.dt / medium.dt;
    const double ratioB = medium.dt / fine.dt;
    if (!(ratioA > 1.0) || std::abs(ratioA - ratioB) > 1.0e-10 * std::max(ratioA, ratioB)) return out;
    const double a = coarse.experiment.maximumRelativeMechanicalEnergyDrift;
    const double b = medium.experiment.maximumRelativeMechanicalEnergyDrift;
    const double c = fine.experiment.maximumRelativeMechanicalEnergyDrift;
    const double d0 = a - b;
    const double d1 = b - c;
    if (d0 == 0.0 || d1 == 0.0 || d0 * d1 <= 0.0) return out;
    const double order = std::log(std::abs(d0 / d1)) / std::log(ratioA);
    if (!std::isfinite(order) || order <= 0.0) return out;
    const double denominator = std::pow(ratioA, order) - 1.0;
    if (!std::isfinite(denominator) || std::abs(denominator) < 1.0e-12) return out;
    out.valid = true;
    out.observedOrder = order;
    out.asymptoticRelativeEnergyDrift = c - d1 / denominator;
    return out;
}

double particleRms(const std::vector<solvers::MpmParticle>& a,
                   const std::vector<solvers::MpmParticle>& b,
                   bool velocity) {
    if (a.size() != b.size()) throw std::invalid_argument("transfer diagnostics particle counts differ");
    if (a.empty()) return 0.0;
    double squared = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const math::Vec3 delta = velocity ? a[i].velocity - b[i].velocity : a[i].position - b[i].position;
        squared += math::dot(delta, delta);
    }
    return std::sqrt(squared / static_cast<double>(a.size()));
}

double gaussianRms(const gaussian::GaussianCloud& a,
                   const gaussian::GaussianCloud& b,
                   const std::vector<std::size_t>& active) {
    if (a.size() != b.size()) throw std::invalid_argument("transfer diagnostics Gaussian counts differ");
    if (active.empty()) return 0.0;
    double squared = 0.0;
    for (const std::size_t index : active) {
        if (index >= a.size()) throw std::out_of_range("transfer diagnostics Gaussian index invalid");
        const auto delta = a.splats[index].position - b.splats[index].position;
        squared += math::dot(delta, delta);
    }
    return std::sqrt(squared / static_cast<double>(active.size()));
}

TransferSchemeDiagnostics summarize(const MpmTransferAblationEntry& entry) {
    const auto& levels = entry.timestepSweep.levels;
    if (levels.size() < 4) throw std::invalid_argument("transfer diagnostics require four timestep levels");
    const auto& finest = levels.back().experiment;
    if (finest.frames.empty() || !(finest.initialMechanicalEnergy > 0.0))
        throw std::runtime_error("transfer diagnostics nonlinear evidence incomplete");

    TransferSchemeDiagnostics out;
    out.scheme = entry.scheme;
    out.finestDt = levels.back().dt;
    out.finestRelativeEnergyDrift = finest.maximumRelativeMechanicalEnergyDrift;
    out.finestGaussianDisplacement = finest.maximumGaussianDisplacement;
    out.finestMinimumDeformationDeterminant = finest.minimumDeformationDeterminant;
    out.finestMaximumDeformationDeterminant = finest.maximumDeformationDeterminant;
    double peakKinetic = 0.0;
    for (const auto& frame : finest.frames) peakKinetic = std::max(peakKinetic, frame.kineticEnergy);
    out.peakKineticEnergyFraction = peakKinetic / finest.initialMechanicalEnergy;
    out.finalKineticEnergyFraction = finest.frames.back().kineticEnergy / finest.initialMechanicalEnergy;
    out.finalElasticEnergyFraction = finest.frames.back().elasticEnergy / finest.initialMechanicalEnergy;
    out.coarseFloor = estimateFloor(levels[0], levels[1], levels[2]);
    out.fineFloor = estimateFloor(levels[levels.size() - 3], levels[levels.size() - 2], levels.back());
    out.floorEstimateDifference = out.coarseFloor.valid && out.fineFloor.valid
        ? std::abs(out.coarseFloor.asymptoticRelativeEnergyDrift - out.fineFloor.asymptoticRelativeEnergyDrift)
        : std::numeric_limits<double>::quiet_NaN();
    return out;
}

} // namespace

MpmTransferDiagnosticsResult runMpmTransferDiagnostics(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    double physicalHorizon,
    std::vector<double> timesteps,
    std::vector<solvers::MpmTransferScheme> schemes) {
    if (timesteps.size() < 4) throw std::invalid_argument("transfer diagnostics require four timestep levels");
    MpmTransferDiagnosticsResult out;
    out.ablation = runMpmTransferAblation(
        std::move(world), activeGaussianIndices, std::move(particles), grid, settings,
        physicalHorizon, std::move(timesteps), std::move(schemes));
    for (const auto& entry : out.ablation.entries) out.schemes.push_back(summarize(entry));

    for (std::size_t a = 0; a < out.ablation.entries.size(); ++a) {
        for (std::size_t b = a + 1; b < out.ablation.entries.size(); ++b) {
            const auto& lhs = out.ablation.entries[a].timestepSweep.levels.back().experiment;
            const auto& rhs = out.ablation.entries[b].timestepSweep.levels.back().experiment;
            TransferSchemePairDifference pair;
            pair.first = out.ablation.entries[a].scheme;
            pair.second = out.ablation.entries[b].scheme;
            pair.particlePositionRms = particleRms(lhs.finalParticles, rhs.finalParticles, false);
            pair.particleVelocityRms = particleRms(lhs.finalParticles, rhs.finalParticles, true);
            pair.gaussianPositionRms = gaussianRms(lhs.finalWorld, rhs.finalWorld, activeGaussianIndices);
            out.finestPairDifferences.push_back(pair);
        }
    }
    return out;
}

void writeMpmTransferDiagnosticsSummaryCsv(
    const MpmTransferDiagnosticsResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open transfer diagnostics summary CSV");
    stream << "scheme,finest_dt,finest_energy_drift,peak_kinetic_fraction,final_kinetic_fraction,"
              "final_elastic_fraction,max_gaussian_displacement,min_J,max_J,"
              "coarse_floor_valid,coarse_floor_order,coarse_energy_floor,"
              "fine_floor_valid,fine_floor_order,fine_energy_floor,floor_estimate_difference\n";
    stream << std::setprecision(17);
    for (const auto& s : result.schemes)
        stream << solvers::toString(s.scheme) << ',' << s.finestDt << ',' << s.finestRelativeEnergyDrift << ','
               << s.peakKineticEnergyFraction << ',' << s.finalKineticEnergyFraction << ','
               << s.finalElasticEnergyFraction << ',' << s.finestGaussianDisplacement << ','
               << s.finestMinimumDeformationDeterminant << ',' << s.finestMaximumDeformationDeterminant << ','
               << (s.coarseFloor.valid ? 1 : 0) << ',' << s.coarseFloor.observedOrder << ','
               << s.coarseFloor.asymptoticRelativeEnergyDrift << ',' << (s.fineFloor.valid ? 1 : 0) << ','
               << s.fineFloor.observedOrder << ',' << s.fineFloor.asymptoticRelativeEnergyDrift << ','
               << s.floorEstimateDifference << '\n';
}

void writeMpmTransferDiagnosticsPairCsv(
    const MpmTransferDiagnosticsResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open transfer diagnostics pair CSV");
    stream << "scheme_a,scheme_b,particle_position_rms,particle_velocity_rms,gaussian_position_rms\n";
    stream << std::setprecision(17);
    for (const auto& p : result.finestPairDifferences)
        stream << solvers::toString(p.first) << ',' << solvers::toString(p.second) << ','
               << p.particlePositionRms << ',' << p.particleVelocityRms << ',' << p.gaussianPositionRms << '\n';
}

} // namespace vulkax::research
