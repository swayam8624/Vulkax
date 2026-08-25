#include "vulkax/capture/deformable_dataset.hpp"
#include "vulkax/research/captured_deformable.hpp"
#include "vulkax/research/nonlinear_deformable_world.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace vulkax;

std::vector<solvers::MpmParticle> makeBody() {
    std::vector<solvers::MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double spacing = 0.12;
    constexpr double volume = spacing * spacing * spacing;
    constexpr double density = 1000.0;
    for (int iz = 0; iz < 4; ++iz)
        for (int iy = 0; iy < 4; ++iy)
            for (int ix = 0; ix < 4; ++ix) {
                solvers::MpmParticle particle;
                particle.id = id++;
                particle.restPosition = {
                    (static_cast<double>(ix) - 1.5) * spacing,
                    (static_cast<double>(iy) - 1.5) * spacing,
                    (static_cast<double>(iz) - 1.5) * spacing,
                };
                particle.position = particle.restPosition;
                particle.mass = density * volume;
                particle.restVolume = volume;
                particles.push_back(particle);
            }
    return particles;
}

gaussian::GaussianSplat makeSplat(math::Vec3 position) {
    gaussian::GaussianSplat result;
    result.position = position;
    result.logScale = {std::log(0.055), std::log(0.045), std::log(0.035)};
    result.rotation = {1.0, 0.0, 0.0, 0.0};
    result.opacityLogit = 4.0;
    result.shDC = {0.1, 0.0, -0.1};
    return result;
}

gaussian::GaussianCloud makeRestWorld() {
    gaussian::GaussianCloud world;
    world.splats.push_back(makeSplat({-0.10, -0.05, -0.02}));
    world.splats.push_back(makeSplat({0.11, -0.04, 0.03}));
    world.splats.push_back(makeSplat({-0.03, 0.10, -0.06}));
    world.splats.push_back(makeSplat({0.04, 0.06, 0.09}));
    world.splats.push_back(makeSplat({0.00, 0.00, 0.00}));
    return world;
}

math::Vec3 applyAffine(const solvers::Matrix3& matrix, math::Vec3 translation, math::Vec3 point) {
    return {
        matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + translation.x,
        matrix[3] * point.x + matrix[4] * point.y + matrix[5] * point.z + translation.y,
        matrix[6] * point.x + matrix[7] * point.y + matrix[8] * point.z + translation.z,
    };
}

solvers::MpmGridSettings makeGrid() {
    solvers::MpmGridSettings grid;
    grid.origin = {-1.0, -1.0, -1.0};
    grid.nx = 26;
    grid.ny = 26;
    grid.nz = 26;
    grid.cellSize = 0.08;
    grid.boundaryCells = 0;
    return grid;
}

capture::CapturedDeformableDataset makeDataset(
    const std::vector<solvers::MpmParticle>& body,
    const solvers::Matrix3& deformation,
    math::Vec3 translation,
    const std::unordered_map<std::size_t, std::vector<math::Vec3>>& trajectories) {
    capture::CapturedDeformableDataset dataset;
    for (const auto& particle : body) {
        dataset.particles.push_back({
            particle.id, particle.restPosition, particle.mass, particle.restVolume,
        });
    }

    const std::array<std::uint64_t, 5> markerParticles{1, 4, 13, 49, 64};
    for (std::size_t marker = 0; marker < markerParticles.size(); ++marker) {
        const std::uint64_t particleId = markerParticles[marker];
        const auto& particle = body.at(static_cast<std::size_t>(particleId - 1U));
        capture::CapturedMarkerObservation observation;
        observation.markerId = "m" + std::to_string(marker);
        observation.particleId = particleId;
        observation.time = 0.0;
        observation.position = applyAffine(deformation, translation, particle.restPosition);
        observation.split = marker < 4U ? capture::ObservationSplit::Fit : capture::ObservationSplit::Validation;
        dataset.observations.push_back(observation);
    }

    for (const auto step : {10U, 20U}) {
        const auto& positions = trajectories.at(step);
        for (std::size_t marker = 0; marker < markerParticles.size(); ++marker) {
            capture::CapturedMarkerObservation observation;
            observation.markerId = "m" + std::to_string(marker);
            observation.particleId = markerParticles[marker];
            observation.time = static_cast<double>(step) * 1.0e-4;
            observation.position = positions.at(static_cast<std::size_t>(markerParticles[marker] - 1U));
            observation.split = marker < 3U ? capture::ObservationSplit::Fit : capture::ObservationSplit::Validation;
            dataset.observations.push_back(observation);
        }
    }
    return dataset;
}

void writeDatasetCsv(
    const capture::CapturedDeformableDataset& dataset,
    const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    std::ofstream particles(directory / "particles.csv");
    particles << "particle_id,rest_x,rest_y,rest_z,mass,rest_volume\n";
    for (const auto& particle : dataset.particles) {
        particles << particle.particleId << ',' << particle.restPosition.x << ',' << particle.restPosition.y << ','
                  << particle.restPosition.z << ',' << particle.mass << ',' << particle.restVolume << '\n';
    }
    std::ofstream observations(directory / "observations.csv");
    observations << "marker_id,particle_id,time,x,y,z,split\n";
    for (const auto& observation : dataset.observations) {
        observations << observation.markerId << ',' << observation.particleId << ',' << observation.time << ','
                     << observation.position.x << ',' << observation.position.y << ',' << observation.position.z << ','
                     << capture::toString(observation.split) << '\n';
    }
}

} // namespace

int main() {
    using namespace vulkax;

    const auto body = makeBody();
    const solvers::Matrix3 deformation{
        1.04, 0.04, 0.00,
        0.01, 0.97, 0.02,
        0.00, 0.01, 1.02,
    };
    const math::Vec3 translation{0.035, -0.021, 0.014};

    research::NonlinearDeformableWorldSettings referenceSettings;
    referenceSettings.steps = 20;
    referenceSettings.dt = 1.0e-4;
    referenceSettings.material = {1000.0, 1.5e4, 0.30};
    referenceSettings.initialDeformation = deformation;
    referenceSettings.initialTranslation = translation;
    referenceSettings.couplingNeighborCount = 20;
    referenceSettings.transferScheme = solvers::MpmTransferScheme::APIC;

    std::unordered_map<std::size_t, std::vector<math::Vec3>> trajectories;
    const auto stateObserver = [&](const research::NonlinearDeformableWorldFrameEvidence& frame,
                                   const gaussian::GaussianCloud&,
                                   const std::vector<solvers::MpmParticle>& particles) {
        if (frame.step != 10U && frame.step != 20U) return;
        auto& positions = trajectories[frame.step];
        positions.reserve(particles.size());
        for (const auto& particle : particles) positions.push_back(particle.position);
    };
    const auto restWorld = makeRestWorld();
    const std::vector<std::size_t> active{0, 1, 2, 3, 4};
    const auto reference = research::runNonlinearDeformableWorld(
        restWorld, active, body, makeGrid(), referenceSettings, {}, stateObserver);
    assert(reference.frames.size() == 20U);
    assert(trajectories.size() == 2U);

    auto capturedWorld = restWorld;
    for (auto& splat : capturedWorld.splats)
        splat.position = applyAffine(deformation, translation, splat.position);
    const auto dataset = makeDataset(body, deformation, translation, trajectories);

    research::NonlinearDeformableWorldSettings replaySettings;
    replaySettings.dt = 1.0e-4;
    replaySettings.material = referenceSettings.material;
    replaySettings.couplingNeighborCount = 20;
    replaySettings.transferScheme = solvers::MpmTransferScheme::APIC;
    const auto replay = research::runCapturedFreeRelaxationBenchmark(
        capturedWorld, active, dataset, makeGrid(), replaySettings);

    assert(replay.simulation.frames.size() == 20U);
    assert(replay.samples.size() == dataset.observations.size());
    assert(replay.initializationFitRms < 1.0e-12);
    assert(replay.appearanceRoundtripRms < 1.0e-10);
    assert(replay.appearanceRoundtripMaximum < 1.0e-10);
    assert(replay.fit.sampleCount == 10U);
    assert(replay.validation.sampleCount == 5U);
    assert(replay.fit.rmsPositionError < 1.0e-10);
    assert(replay.validation.rmsPositionError < 1.0e-10);
    for (std::size_t index = 0; index < deformation.size(); ++index)
        assert(std::abs(replay.fittedInitialDeformation[index] - deformation[index]) < 1.0e-12);
    assert(math::length(replay.fittedInitialTranslation - translation) < 1.0e-12);

    const auto directory = std::filesystem::temp_directory_path() / "vulkax-captured-deformable-test";
    std::filesystem::remove_all(directory);
    writeDatasetCsv(dataset, directory);
    const auto parsed = capture::loadCapturedDeformableDataset(
        directory / "particles.csv", directory / "observations.csv");
    assert(parsed.particles.size() == dataset.particles.size());
    assert(parsed.observations.size() == dataset.observations.size());

    research::writeCapturedReplaySamplesCsv(replay, directory / "samples.csv");
    research::writeCapturedReplaySummaryCsv(replay, directory / "summary.csv");
    assert(std::filesystem::file_size(directory / "samples.csv") > 100U);
    assert(std::filesystem::file_size(directory / "summary.csv") > 100U);
    std::filesystem::remove_all(directory);
    return 0;
}
