#include "vulkax/research/captured_operator_influence.hpp"

#include "vulkax/autodiff/mpm_adjoint.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace vulkax::research {
namespace {

[[nodiscard]] math::Vec3 normalizedDirection(math::Vec3 direction) {
    const double magnitude = math::length(direction);
    if (!std::isfinite(magnitude) || magnitude <= 1.0e-15)
        throw std::invalid_argument("captured material influence objective direction must be non-zero");
    return direction / magnitude;
}

[[nodiscard]] bool sameTime(double lhs, double rhs) noexcept {
    const double scale = std::max({1.0, std::abs(lhs), std::abs(rhs)});
    return std::abs(lhs - rhs) <= scale * 1.0e-10;
}

[[nodiscard]] const CapturedReplaySample& findSample(
    const CapturedFreeRelaxationResult& replay,
    const std::string& markerId,
    double time) {
    const CapturedReplaySample* match = nullptr;
    for (const auto& sample : replay.samples) {
        if (sample.markerId != markerId || !sameTime(sample.time, time)) continue;
        if (match != nullptr)
            throw std::runtime_error("captured material influence objective sample is ambiguous");
        match = &sample;
    }
    if (match == nullptr)
        throw std::invalid_argument("captured material influence objective marker/time is not observed");
    return *match;
}

[[nodiscard]] double observable(
    const CapturedFreeRelaxationResult& replay,
    const CapturedMaterialInfluenceSettings& settings,
    math::Vec3 direction) {
    const auto& initial = findSample(replay, settings.objectiveMarkerId, 0.0);
    const auto& target = findSample(replay, settings.objectiveMarkerId, settings.objectiveTime);
    return math::dot(target.predicted - initial.predicted, direction);
}

[[nodiscard]] math::Vec3 applyAffine(
    const solvers::Matrix3& deformation,
    math::Vec3 translation,
    math::Vec3 position) noexcept {
    return {
        deformation[0] * position.x + deformation[1] * position.y + deformation[2] * position.z + translation.x,
        deformation[3] * position.x + deformation[4] * position.y + deformation[5] * position.z + translation.y,
        deformation[6] * position.x + deformation[7] * position.y + deformation[8] * position.z + translation.z,
    };
}

[[nodiscard]] std::unordered_map<std::uint64_t, const capture::CapturedParticleSpec*> particleById(
    const capture::CapturedDeformableDataset& dataset) {
    std::unordered_map<std::uint64_t, const capture::CapturedParticleSpec*> result;
    result.reserve(dataset.particles.size());
    for (const auto& particle : dataset.particles) {
        if (particle.particleId == 0U || !result.emplace(particle.particleId, &particle).second)
            throw std::invalid_argument("captured material influence requires unique non-zero particle IDs");
    }
    return result;
}

[[nodiscard]] std::unordered_map<std::uint64_t, std::size_t> particleIndexById(
    const capture::CapturedDeformableDataset& dataset) {
    std::unordered_map<std::uint64_t, std::size_t> result;
    result.reserve(dataset.particles.size());
    for (std::size_t index = 0; index < dataset.particles.size(); ++index) {
        const auto id = dataset.particles[index].particleId;
        if (id == 0U || !result.emplace(id, index).second)
            throw std::invalid_argument("captured material influence requires unique non-zero particle IDs");
    }
    return result;
}

[[nodiscard]] math::Vec3 regionCentroid(
    const CapturedMaterialInfluenceRegion& region,
    const std::unordered_map<std::uint64_t, const capture::CapturedParticleSpec*>& particles) {
    if (region.particleIds.empty())
        throw std::invalid_argument("captured material influence region cannot be empty");
    math::Vec3 centroid{};
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(region.particleIds.size());
    for (const auto particleId : region.particleIds) {
        if (!seen.insert(particleId).second)
            throw std::invalid_argument("captured material influence region contains duplicate particle IDs");
        const auto iterator = particles.find(particleId);
        if (iterator == particles.end())
            throw std::invalid_argument("captured material influence region references an unknown particle ID");
        centroid += iterator->second->restPosition;
    }
    return centroid / static_cast<double>(region.particleIds.size());
}

void validateRegions(const std::vector<CapturedMaterialInfluenceRegion>& regions) {
    if (regions.empty())
        throw std::invalid_argument("captured material influence requires at least one region");
    std::unordered_set<std::string> regionIds;
    regionIds.reserve(regions.size());
    for (const auto& region : regions) {
        if (region.id.empty() || !regionIds.insert(region.id).second)
            throw std::invalid_argument("captured material influence region IDs must be unique and non-empty");
    }
}

void validateObjectiveSettings(const CapturedMaterialInfluenceSettings& influenceSettings) {
    if (influenceSettings.objectiveMarkerId.empty())
        throw std::invalid_argument("captured material influence objective marker ID is empty");
    if (!std::isfinite(influenceSettings.objectiveTime) || influenceSettings.objectiveTime <= 0.0)
        throw std::invalid_argument("captured material influence objective time must be positive");
}

[[nodiscard]] std::unordered_map<std::uint64_t, double> regionScales(
    const CapturedMaterialInfluenceRegion& region,
    double scale) {
    std::unordered_map<std::uint64_t, double> result;
    result.reserve(region.particleIds.size());
    for (const auto particleId : region.particleIds) result.emplace(particleId, scale);
    return result;
}

[[nodiscard]] double runObservable(
    const gaussian::GaussianCloud& world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& dataset,
    const solvers::MpmGridSettings& grid,
    const NonlinearDeformableWorldSettings& settings,
    const CapturedMaterialInfluenceSettings& influenceSettings,
    math::Vec3 direction,
    const CapturedMaterialInfluenceRegion& region,
    double scale) {
    const auto replay = runCapturedFreeRelaxationBenchmark(
        world, activeGaussianIndices, dataset, grid, settings, regionScales(region, scale));
    return observable(replay, influenceSettings, direction);
}

} // namespace

CapturedMaterialInfluenceResult computeCapturedMaterialInfluenceReference(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& dataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    const std::vector<CapturedMaterialInfluenceRegion>& regions,
    const CapturedMaterialInfluenceSettings& influenceSettings) {
    validateRegions(regions);
    validateObjectiveSettings(influenceSettings);
    if (!std::isfinite(influenceSettings.finiteDifferenceScaleStep) ||
        influenceSettings.finiteDifferenceScaleStep <= 0.0 ||
        influenceSettings.finiteDifferenceScaleStep >= 1.0)
        throw std::invalid_argument("captured material influence finite-difference step must lie in (0, 1)");
    if (!std::isfinite(influenceSettings.verificationScaleDelta) ||
        influenceSettings.verificationScaleDelta <= -1.0)
        throw std::invalid_argument("captured material influence verification scale delta must exceed -1");

    const auto direction = normalizedDirection(influenceSettings.objectiveDirection);
    const auto particles = particleById(dataset);

    CapturedMaterialInfluenceResult result;
    result.objectiveMarkerId = influenceSettings.objectiveMarkerId;
    result.objectiveTime = influenceSettings.objectiveTime;
    result.objectiveDirection = direction;
    result.baselineReplay = runCapturedFreeRelaxationBenchmark(
        world, activeGaussianIndices, dataset, grid, settings);
    result.baselineObservable = observable(result.baselineReplay, influenceSettings, direction);
    result.field.reserve(regions.size());
    result.verification.reserve(regions.size());

    const double h = influenceSettings.finiteDifferenceScaleStep;
    const double verificationDelta = influenceSettings.verificationScaleDelta;

    for (const auto& region : regions) {
        CapturedMaterialInfluenceFieldSample field;
        field.regionId = region.id;
        field.particleCount = region.particleIds.size();
        field.restCentroid = regionCentroid(region, particles);
        field.minusScale = 1.0 - h;
        field.plusScale = 1.0 + h;
        field.minusObservable = runObservable(
            world, activeGaussianIndices, dataset, grid, settings, influenceSettings,
            direction, region, field.minusScale);
        field.plusObservable = runObservable(
            world, activeGaussianIndices, dataset, grid, settings, influenceSettings,
            direction, region, field.plusScale);
        field.derivative = (field.plusObservable - field.minusObservable) / (2.0 * h);
        if (!std::isfinite(field.derivative))
            throw std::runtime_error("captured material influence derivative is non-finite");
        result.field.push_back(field);

        CapturedMaterialCounterfactualVerification verification;
        verification.regionId = region.id;
        verification.deltaScale = verificationDelta;
        verification.baselineObservable = result.baselineObservable;
        verification.predictedObservable = result.baselineObservable + field.derivative * verificationDelta;
        verification.actualObservable = runObservable(
            world, activeGaussianIndices, dataset, grid, settings, influenceSettings,
            direction, region, 1.0 + verificationDelta);
        verification.absoluteError = std::abs(
            verification.predictedObservable - verification.actualObservable);
        const double changeScale = std::max({
            1.0e-12,
            std::abs(verification.actualObservable - result.baselineObservable),
            std::abs(verification.predictedObservable - result.baselineObservable),
        });
        verification.relativeLinearizationError = verification.absoluteError / changeScale;
        result.verification.push_back(verification);
    }

    return result;
}

CapturedMaterialAdjointInfluenceResult computeCapturedMaterialInfluenceAdjoint(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& dataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    const std::vector<CapturedMaterialInfluenceRegion>& regions,
    const CapturedMaterialInfluenceSettings& influenceSettings) {
    validateRegions(regions);
    validateObjectiveSettings(influenceSettings);
    if (settings.transferScheme != solvers::MpmTransferScheme::APIC || settings.flipBlend != 0.0)
        throw std::invalid_argument("captured material adjoint currently requires pure APIC transfer");
    if (grid.boundaryCells != 0U)
        throw std::invalid_argument("captured material adjoint currently requires boundaryCells == 0");

    const auto direction = normalizedDirection(influenceSettings.objectiveDirection);
    const auto particles = particleById(dataset);
    const auto particleIndices = particleIndexById(dataset);
    for (const auto& region : regions) (void)regionCentroid(region, particles);

    const auto baseline = runCapturedFreeRelaxationBenchmark(
        world, activeGaussianIndices, dataset, grid, settings);
    const double baselineObservable = observable(baseline, influenceSettings, direction);
    const auto& objectiveSample = findSample(
        baseline, influenceSettings.objectiveMarkerId, influenceSettings.objectiveTime);
    const auto objectiveIterator = particleIndices.find(objectiveSample.particleId);
    if (objectiveIterator == particleIndices.end())
        throw std::runtime_error("captured material adjoint objective particle is missing");

    const double exactSteps = influenceSettings.objectiveTime / settings.dt;
    const auto roundedSteps = static_cast<long long>(std::llround(exactSteps));
    if (roundedSteps <= 0 ||
        std::abs(static_cast<double>(roundedSteps) * settings.dt - influenceSettings.objectiveTime) >
            std::max(1.0e-12, influenceSettings.objectiveTime * 1.0e-10))
        throw std::invalid_argument("captured material adjoint objective time is off the solver lattice");

    auto mpmParticles = capture::makeMpmParticles(dataset.particles);
    for (auto& particle : mpmParticles) {
        particle.position = applyAffine(
            baseline.fittedInitialDeformation,
            baseline.fittedInitialTranslation,
            particle.restPosition);
        particle.deformationGradient = baseline.fittedInitialDeformation;
        particle.velocity = {};
        particle.affineVelocity = {};
        particle.externalForce = {};
        particle.youngModulusScale = 1.0;
    }

    const auto adjoint = autodiff::differentiateMpmApicMaterialScales(
        std::move(mpmParticles),
        grid,
        settings.material,
        settings.dt,
        static_cast<std::size_t>(roundedSteps),
        objectiveIterator->second,
        direction);
    if (std::abs(adjoint.objective - baselineObservable) >
        std::max(1.0e-12, std::abs(baselineObservable) * 1.0e-9))
        throw std::runtime_error("captured material adjoint forward objective does not match captured replay");

    CapturedMaterialAdjointInfluenceResult result;
    result.objectiveMarkerId = influenceSettings.objectiveMarkerId;
    result.objectiveTime = influenceSettings.objectiveTime;
    result.objectiveDirection = direction;
    result.baselineObservable = baselineObservable;
    result.minimumStencilKnotMargin = adjoint.minimumStencilKnotMargin;
    result.particleScaleGradient = adjoint.particleScaleGradient;
    result.field.reserve(regions.size());

    for (const auto& region : regions) {
        CapturedMaterialAdjointInfluenceFieldSample field;
        field.regionId = region.id;
        field.particleCount = region.particleIds.size();
        field.restCentroid = regionCentroid(region, particles);
        for (const auto particleId : region.particleIds)
            field.derivative += result.particleScaleGradient.at(particleIndices.at(particleId));
        if (!std::isfinite(field.derivative))
            throw std::runtime_error("captured material adjoint derivative is non-finite");
        result.field.push_back(field);
    }
    return result;
}

std::vector<CapturedMaterialInfluenceDerivativeComparison>
compareCapturedMaterialInfluenceDerivatives(
    const CapturedMaterialInfluenceResult& reference,
    const CapturedMaterialAdjointInfluenceResult& adjoint) {
    if (reference.objectiveMarkerId != adjoint.objectiveMarkerId ||
        !sameTime(reference.objectiveTime, adjoint.objectiveTime))
        throw std::invalid_argument("captured influence derivative comparison objective mismatch");
    std::unordered_map<std::string, double> adjointByRegion;
    adjointByRegion.reserve(adjoint.field.size());
    for (const auto& field : adjoint.field) {
        if (!adjointByRegion.emplace(field.regionId, field.derivative).second)
            throw std::invalid_argument("captured adjoint influence contains duplicate region IDs");
    }
    std::vector<CapturedMaterialInfluenceDerivativeComparison> result;
    result.reserve(reference.field.size());
    for (const auto& field : reference.field) {
        const auto iterator = adjointByRegion.find(field.regionId);
        if (iterator == adjointByRegion.end())
            throw std::invalid_argument("captured adjoint influence is missing a reference region");
        CapturedMaterialInfluenceDerivativeComparison sample;
        sample.regionId = field.regionId;
        sample.referenceDerivative = field.derivative;
        sample.adjointDerivative = iterator->second;
        sample.absoluteError = std::abs(sample.adjointDerivative - sample.referenceDerivative);
        const double scale = std::max({
            1.0e-12,
            std::abs(sample.referenceDerivative),
            std::abs(sample.adjointDerivative),
        });
        sample.relativeError = sample.absoluteError / scale;
        result.push_back(sample);
    }
    if (adjointByRegion.size() != result.size())
        throw std::invalid_argument("captured adjoint influence has regions absent from the reference");
    return result;
}

void writeCapturedMaterialInfluenceCsv(
    const CapturedMaterialInfluenceResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured material influence CSV");
    stream << "region_id,particle_count,centroid_x,centroid_y,centroid_z,minus_scale,plus_scale,"
              "minus_observable,plus_observable,derivative\n";
    stream << std::setprecision(17);
    for (const auto& field : result.field) {
        stream << field.regionId << ',' << field.particleCount << ','
               << field.restCentroid.x << ',' << field.restCentroid.y << ',' << field.restCentroid.z << ','
               << field.minusScale << ',' << field.plusScale << ','
               << field.minusObservable << ',' << field.plusObservable << ',' << field.derivative << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing captured material influence CSV");
}

void writeCapturedMaterialCounterfactualCsv(
    const CapturedMaterialInfluenceResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured material counterfactual CSV");
    stream << "region_id,delta_scale,baseline_observable,predicted_observable,actual_observable,"
              "absolute_error,relative_linearization_error\n";
    stream << std::setprecision(17);
    for (const auto& verification : result.verification) {
        stream << verification.regionId << ',' << verification.deltaScale << ','
               << verification.baselineObservable << ',' << verification.predictedObservable << ','
               << verification.actualObservable << ',' << verification.absoluteError << ','
               << verification.relativeLinearizationError << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing captured material counterfactual CSV");
}

void writeCapturedMaterialAdjointInfluenceCsv(
    const CapturedMaterialAdjointInfluenceResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured material adjoint influence CSV");
    stream << "region_id,particle_count,centroid_x,centroid_y,centroid_z,adjoint_derivative\n";
    stream << std::setprecision(17);
    for (const auto& field : result.field) {
        stream << field.regionId << ',' << field.particleCount << ','
               << field.restCentroid.x << ',' << field.restCentroid.y << ',' << field.restCentroid.z << ','
               << field.derivative << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing captured material adjoint influence CSV");
}

void writeCapturedMaterialInfluenceDerivativeComparisonCsv(
    const std::vector<CapturedMaterialInfluenceDerivativeComparison>& comparison,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured influence derivative comparison CSV");
    stream << "region_id,reference_derivative,adjoint_derivative,absolute_error,relative_error\n";
    stream << std::setprecision(17);
    for (const auto& sample : comparison) {
        stream << sample.regionId << ',' << sample.referenceDerivative << ',' << sample.adjointDerivative << ','
               << sample.absoluteError << ',' << sample.relativeError << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing captured influence derivative comparison CSV");
}

} // namespace vulkax::research
