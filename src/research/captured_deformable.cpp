#include "vulkax/research/captured_deformable.hpp"

#include "vulkax/coupling/mls_embedding.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace vulkax::research {
namespace {

using Vector4 = std::array<double, 4>;
using Matrix4 = std::array<std::array<double, 4>, 4>;

struct InitialAffineFit {
    solvers::Matrix3 deformation{solvers::identityMatrix3()};
    math::Vec3 translation{};
    double rms{};
};

[[nodiscard]] Vector4 basis(math::Vec3 position) noexcept {
    return {1.0, position.x, position.y, position.z};
}

[[nodiscard]] Vector4 solve4(Matrix4 matrix, Vector4 rhs) {
    constexpr double pivotTolerance = 1.0e-13;
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        double magnitude = std::abs(matrix[pivot][column]);
        for (std::size_t row = column + 1; row < 4; ++row) {
            const double candidate = std::abs(matrix[row][column]);
            if (candidate > magnitude) {
                magnitude = candidate;
                pivot = row;
            }
        }
        if (magnitude < pivotTolerance)
            throw std::runtime_error("captured t=0 fit is rank deficient; use at least four non-coplanar fit markers");
        if (pivot != column) {
            std::swap(matrix[pivot], matrix[column]);
            std::swap(rhs[pivot], rhs[column]);
        }
        const double inversePivot = 1.0 / matrix[column][column];
        for (std::size_t entry = column; entry < 4; ++entry) matrix[column][entry] *= inversePivot;
        rhs[column] *= inversePivot;
        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            for (std::size_t entry = column; entry < 4; ++entry)
                matrix[row][entry] -= factor * matrix[column][entry];
            rhs[row] -= factor * rhs[column];
        }
    }
    return rhs;
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

[[nodiscard]] double determinant(const solvers::Matrix3& matrix) noexcept {
    return matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
           matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
           matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
}

[[nodiscard]] std::unordered_map<std::uint64_t, std::size_t> particleIndexById(
    const std::vector<capture::CapturedParticleSpec>& particles) {
    std::unordered_map<std::uint64_t, std::size_t> result;
    result.reserve(particles.size());
    for (std::size_t index = 0; index < particles.size(); ++index) {
        if (particles[index].particleId == 0 || !result.emplace(particles[index].particleId, index).second)
            throw std::invalid_argument("captured benchmark particle IDs must be unique and non-zero");
    }
    return result;
}

[[nodiscard]] InitialAffineFit fitInitialAffine(
    const capture::CapturedDeformableDataset& dataset,
    const std::unordered_map<std::uint64_t, std::size_t>& particleIndices) {
    Matrix4 moment{};
    Vector4 rhsX{};
    Vector4 rhsY{};
    Vector4 rhsZ{};
    std::vector<const capture::CapturedMarkerObservation*> fitRows;
    std::unordered_set<std::uint64_t> fitParticles;

    for (const auto& observation : dataset.observations) {
        if (observation.split != capture::ObservationSplit::Fit || std::abs(observation.time) > 1.0e-12) continue;
        const auto iterator = particleIndices.find(observation.particleId);
        if (iterator == particleIndices.end())
            throw std::invalid_argument("captured t=0 fit references an unknown particle");
        const auto& particle = dataset.particles[iterator->second];
        const auto b = basis(particle.restPosition);
        for (std::size_t row = 0; row < 4; ++row) {
            rhsX[row] += b[row] * observation.position.x;
            rhsY[row] += b[row] * observation.position.y;
            rhsZ[row] += b[row] * observation.position.z;
            for (std::size_t column = 0; column < 4; ++column)
                moment[row][column] += b[row] * b[column];
        }
        fitRows.push_back(&observation);
        fitParticles.insert(observation.particleId);
    }
    if (fitRows.size() < 4U || fitParticles.size() < 4U)
        throw std::invalid_argument("captured benchmark needs at least four distinct t=0 fit particles");

    const auto coefficientX = solve4(moment, rhsX);
    const auto coefficientY = solve4(moment, rhsY);
    const auto coefficientZ = solve4(moment, rhsZ);
    InitialAffineFit result;
    result.translation = {coefficientX[0], coefficientY[0], coefficientZ[0]};
    result.deformation = {
        coefficientX[1], coefficientX[2], coefficientX[3],
        coefficientY[1], coefficientY[2], coefficientY[3],
        coefficientZ[1], coefficientZ[2], coefficientZ[3],
    };
    if (!std::isfinite(determinant(result.deformation)) || determinant(result.deformation) <= 1.0e-10)
        throw std::runtime_error("captured t=0 affine fit is singular or orientation reversing");

    double squared = 0.0;
    for (const auto* observation : fitRows) {
        const auto& particle = dataset.particles.at(particleIndices.at(observation->particleId));
        const auto predicted = applyAffine(result.deformation, result.translation, particle.restPosition);
        const auto delta = predicted - observation->position;
        squared += math::dot(delta, delta);
    }
    result.rms = std::sqrt(squared / static_cast<double>(fitRows.size()));
    return result;
}

[[nodiscard]] std::vector<coupling::PhysicalPoint> affinePhysicalPoints(
    const std::vector<capture::CapturedParticleSpec>& particles,
    const solvers::Matrix3& deformation,
    math::Vec3 translation,
    bool capturedToRest) {
    std::vector<coupling::PhysicalPoint> result;
    result.reserve(particles.size());
    for (const auto& particle : particles) {
        const auto capturedPosition = applyAffine(deformation, translation, particle.restPosition);
        if (capturedToRest)
            result.push_back({particle.particleId, capturedPosition, particle.restPosition, {}});
        else
            result.push_back({particle.particleId, particle.restPosition, capturedPosition, {}});
    }
    return result;
}

[[nodiscard]] gaussian::GaussianCloud gatherActive(
    const gaussian::GaussianCloud& world,
    const std::vector<std::size_t>& activeIndices) {
    if (activeIndices.empty()) throw std::invalid_argument("captured benchmark needs active Gaussian indices");
    gaussian::GaussianCloud result;
    result.shRestCoefficientsPerSplat = world.shRestCoefficientsPerSplat;
    std::vector<bool> seen(world.size(), false);
    result.splats.reserve(activeIndices.size());
    for (const auto index : activeIndices) {
        if (index >= world.size() || seen[index])
            throw std::invalid_argument("captured benchmark active Gaussian indices are invalid");
        seen[index] = true;
        result.splats.push_back(world.splats[index]);
    }
    return result;
}

void scatterActive(
    const gaussian::GaussianCloud& active,
    const std::vector<std::size_t>& activeIndices,
    gaussian::GaussianCloud& world) {
    if (active.size() != activeIndices.size())
        throw std::invalid_argument("captured benchmark active Gaussian scatter size mismatch");
    for (std::size_t local = 0; local < activeIndices.size(); ++local)
        world.splats[activeIndices[local]] = active.splats[local];
}

[[nodiscard]] std::pair<double, double> appearanceRoundtripError(
    const gaussian::GaussianCloud& reference,
    const gaussian::GaussianCloud& reconstructed) {
    if (reference.size() != reconstructed.size() || reference.empty())
        throw std::invalid_argument("captured appearance roundtrip size mismatch");
    double squared = 0.0;
    double maximum = 0.0;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        const double error = math::length(reference.splats[index].position - reconstructed.splats[index].position);
        squared += error * error;
        maximum = std::max(maximum, error);
    }
    return {std::sqrt(squared / static_cast<double>(reference.size())), maximum};
}

void addSample(
    CapturedFreeRelaxationResult& result,
    const capture::CapturedMarkerObservation& observation,
    math::Vec3 predicted) {
    CapturedReplaySample sample;
    sample.markerId = observation.markerId;
    sample.particleId = observation.particleId;
    sample.time = observation.time;
    sample.split = observation.split;
    sample.observed = observation.position;
    sample.predicted = predicted;
    sample.positionError = math::length(predicted - observation.position);
    result.samples.push_back(std::move(sample));
}

[[nodiscard]] CapturedReplayErrorMetrics summarize(
    const std::vector<CapturedReplaySample>& samples,
    capture::ObservationSplit split) {
    CapturedReplayErrorMetrics result;
    double squared = 0.0;
    for (const auto& sample : samples) {
        if (sample.split != split) continue;
        ++result.sampleCount;
        squared += sample.positionError * sample.positionError;
        result.maximumPositionError = std::max(result.maximumPositionError, sample.positionError);
    }
    if (result.sampleCount > 0)
        result.rmsPositionError = std::sqrt(squared / static_cast<double>(result.sampleCount));
    return result;
}

} // namespace

CapturedFreeRelaxationResult runCapturedFreeRelaxationBenchmark(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& dataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings) {
    if (world.empty()) throw std::invalid_argument("captured benchmark Gaussian world is empty");
    if (dataset.particles.size() < 4U || dataset.observations.empty())
        throw std::invalid_argument("captured benchmark dataset is incomplete");
    if (!std::isfinite(settings.dt) || settings.dt <= 0.0)
        throw std::invalid_argument("captured benchmark timestep must be positive");

    const auto particleIndices = particleIndexById(dataset.particles);
    std::unordered_map<std::string, std::uint64_t> markerBindings;
    std::size_t maximumStep = 0;
    std::vector<std::size_t> observationSteps(dataset.observations.size(), 0);
    for (std::size_t index = 0; index < dataset.observations.size(); ++index) {
        const auto& observation = dataset.observations[index];
        if (!particleIndices.contains(observation.particleId))
            throw std::invalid_argument("captured observation references an unknown particle_id");
        const auto [iterator, inserted] = markerBindings.emplace(observation.markerId, observation.particleId);
        if (!inserted && iterator->second != observation.particleId)
            throw std::invalid_argument("captured marker_id changes particle_id over time");
        const double exactStep = observation.time / settings.dt;
        const auto rounded = static_cast<long long>(std::llround(exactStep));
        if (rounded < 0 || std::abs(static_cast<double>(rounded) * settings.dt - observation.time) >
                               std::max(1.0e-12, std::max(observation.time, settings.dt) * 1.0e-10))
            throw std::invalid_argument("captured observation time does not lie on the solver timestep lattice");
        observationSteps[index] = static_cast<std::size_t>(rounded);
        maximumStep = std::max(maximumStep, observationSteps[index]);
    }
    if (maximumStep == 0) throw std::invalid_argument("captured benchmark needs at least one observation after t=0");

    const InitialAffineFit initialFit = fitInitialAffine(dataset, particleIndices);
    settings.initialDeformation = initialFit.deformation;
    settings.initialTranslation = initialFit.translation;
    settings.steps = maximumStep;

    CapturedFreeRelaxationResult result;
    result.fittedInitialDeformation = initialFit.deformation;
    result.fittedInitialTranslation = initialFit.translation;
    result.initializationFitRms = initialFit.rms;
    result.samples.reserve(dataset.observations.size());

    const auto capturedActive = gatherActive(world, activeGaussianIndices);
    auto restActive = capturedActive;
    const auto capturedToRestPoints = affinePhysicalPoints(
        dataset.particles, initialFit.deformation, initialFit.translation, true);
    const auto capturedToRestEmbedding = coupling::buildMlsEmbedding(
        capturedActive, capturedToRestPoints,
        std::min(settings.couplingNeighborCount, dataset.particles.size()));
    coupling::updateGaussianGeometryFromPhysics(capturedToRestEmbedding, capturedToRestPoints, restActive);

    auto reconstructedCaptured = restActive;
    const auto restToCapturedPoints = affinePhysicalPoints(
        dataset.particles, initialFit.deformation, initialFit.translation, false);
    const auto restToCapturedEmbedding = coupling::buildMlsEmbedding(
        restActive, restToCapturedPoints,
        std::min(settings.couplingNeighborCount, dataset.particles.size()));
    coupling::updateGaussianGeometryFromPhysics(restToCapturedEmbedding, restToCapturedPoints, reconstructedCaptured);
    const auto [roundtripRms, roundtripMaximum] = appearanceRoundtripError(capturedActive, reconstructedCaptured);
    result.appearanceRoundtripRms = roundtripRms;
    result.appearanceRoundtripMaximum = roundtripMaximum;
    scatterActive(restActive, activeGaussianIndices, world);

    for (std::size_t index = 0; index < dataset.observations.size(); ++index) {
        if (observationSteps[index] != 0) continue;
        const auto& observation = dataset.observations[index];
        const auto& particle = dataset.particles.at(particleIndices.at(observation.particleId));
        addSample(result, observation,
                  applyAffine(initialFit.deformation, initialFit.translation, particle.restPosition));
    }

    std::vector<std::vector<std::size_t>> observationsByStep(maximumStep + 1U);
    for (std::size_t index = 0; index < dataset.observations.size(); ++index) {
        const std::size_t step = observationSteps[index];
        if (step > 0) observationsByStep[step].push_back(index);
    }

    const auto physicalStateObserver = [&](const NonlinearDeformableWorldFrameEvidence& frame,
                                           const gaussian::GaussianCloud&,
                                           const std::vector<solvers::MpmParticle>& particles) {
        if (frame.step >= observationsByStep.size()) return;
        for (const std::size_t observationIndex : observationsByStep[frame.step]) {
            const auto& observation = dataset.observations[observationIndex];
            const auto particleIndex = particleIndices.at(observation.particleId);
            if (particleIndex >= particles.size())
                throw std::runtime_error("captured replay particle ordering changed unexpectedly");
            addSample(result, observation, particles[particleIndex].position);
        }
    };

    result.simulation = runNonlinearDeformableWorld(
        std::move(world), activeGaussianIndices, capture::makeMpmParticles(dataset.particles),
        grid, settings, {}, physicalStateObserver);
    if (result.samples.size() != dataset.observations.size())
        throw std::runtime_error("captured replay did not evaluate every observation");
    std::sort(result.samples.begin(), result.samples.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.time != rhs.time) return lhs.time < rhs.time;
        return lhs.markerId < rhs.markerId;
    });
    result.fit = summarize(result.samples, capture::ObservationSplit::Fit);
    result.validation = summarize(result.samples, capture::ObservationSplit::Validation);
    return result;
}

void writeCapturedReplaySamplesCsv(
    const CapturedFreeRelaxationResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured replay sample CSV");
    stream << "marker_id,particle_id,time,split,observed_x,observed_y,observed_z,"
              "predicted_x,predicted_y,predicted_z,position_error\n";
    stream << std::setprecision(17);
    for (const auto& sample : result.samples) {
        stream << sample.markerId << ',' << sample.particleId << ',' << sample.time << ','
               << capture::toString(sample.split) << ','
               << sample.observed.x << ',' << sample.observed.y << ',' << sample.observed.z << ','
               << sample.predicted.x << ',' << sample.predicted.y << ',' << sample.predicted.z << ','
               << sample.positionError << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing captured replay sample CSV");
}

void writeCapturedReplaySummaryCsv(
    const CapturedFreeRelaxationResult& result,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured replay summary CSV");
    stream << "initialization_fit_rms,appearance_roundtrip_rms,appearance_roundtrip_max,"
              "fit_samples,fit_rms,fit_max,validation_samples,validation_rms,validation_max,"
              "F00,F01,F02,F10,F11,F12,F20,F21,F22,tx,ty,tz,"
              "max_energy_drift,min_J,max_J,max_mls_rms_residual,max_mls_residual\n";
    stream << std::setprecision(17)
           << result.initializationFitRms << ',' << result.appearanceRoundtripRms << ','
           << result.appearanceRoundtripMaximum << ','
           << result.fit.sampleCount << ',' << result.fit.rmsPositionError << ',' << result.fit.maximumPositionError << ','
           << result.validation.sampleCount << ',' << result.validation.rmsPositionError << ','
           << result.validation.maximumPositionError;
    for (const double value : result.fittedInitialDeformation) stream << ',' << value;
    stream << ',' << result.fittedInitialTranslation.x << ',' << result.fittedInitialTranslation.y << ','
           << result.fittedInitialTranslation.z << ','
           << result.simulation.maximumRelativeMechanicalEnergyDrift << ','
           << result.simulation.minimumDeformationDeterminant << ','
           << result.simulation.maximumDeformationDeterminant << ','
           << result.simulation.maximumMlsRmsResidual << ',' << result.simulation.maximumMlsResidual << '\n';
    if (!stream) throw std::runtime_error("failed while writing captured replay summary CSV");
}

} // namespace vulkax::research
