#include "vulkax/research/captured_material_calibration.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    return result;
}

gaussian::GaussianCloud makeWorld() {
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
    for (const auto& particle : body)
        dataset.particles.push_back({particle.id, particle.restPosition, particle.mass, particle.restVolume});

    const std::array<std::uint64_t, 5> markers{1, 4, 13, 49, 64};
    for (std::size_t marker = 0; marker < markers.size(); ++marker) {
        const auto& particle = body.at(static_cast<std::size_t>(markers[marker] - 1U));
        dataset.observations.push_back({
            "m" + std::to_string(marker), markers[marker], 0.0,
            applyAffine(deformation, translation, particle.restPosition),
            marker < 4U ? capture::ObservationSplit::Fit : capture::ObservationSplit::Validation,
        });
    }

    for (const auto step : {10U, 20U, 30U}) {
        const auto& positions = trajectories.at(step);
        for (std::size_t marker = 0; marker < markers.size(); ++marker) {
            dataset.observations.push_back({
                "m" + std::to_string(marker), markers[marker], static_cast<double>(step) * 1.0e-4,
                positions.at(static_cast<std::size_t>(markers[marker] - 1U)),
                marker < 3U ? capture::ObservationSplit::Fit : capture::ObservationSplit::Validation,
            });
        }
    }
    return dataset;
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

    research::NonlinearDeformableWorldSettings truth;
    truth.steps = 30;
    truth.dt = 1.0e-4;
    truth.material = {1000.0, 1.5e4, 0.30};
    truth.initialDeformation = deformation;
    truth.initialTranslation = translation;
    truth.couplingNeighborCount = 20;
    truth.transferScheme = solvers::MpmTransferScheme::APIC;

    std::unordered_map<std::size_t, std::vector<math::Vec3>> trajectories;
    const auto observer = [&](const research::NonlinearDeformableWorldFrameEvidence& frame,
                              const gaussian::GaussianCloud&,
                              const std::vector<solvers::MpmParticle>& particles) {
        if (frame.step != 10U && frame.step != 20U && frame.step != 30U) return;
        auto& output = trajectories[frame.step];
        for (const auto& particle : particles) output.push_back(particle.position);
    };
    const std::vector<std::size_t> active{0, 1, 2, 3, 4};
    const auto restWorld = makeWorld();
    (void)research::runNonlinearDeformableWorld(
        restWorld, active, body, makeGrid(), truth, {}, observer);
    assert(trajectories.size() == 3U);

    auto capturedWorld = restWorld;
    for (auto& splat : capturedWorld.splats)
        splat.position = applyAffine(deformation, translation, splat.position);
    const auto dataset = makeDataset(body, deformation, translation, trajectories);

    research::NonlinearDeformableWorldSettings settings;
    settings.dt = 1.0e-4;
    settings.material.density = 1000.0;
    settings.couplingNeighborCount = 20;
    settings.transferScheme = solvers::MpmTransferScheme::APIC;

    const auto calibration = research::calibrateCapturedMaterialGrid(
        capturedWorld, active, dataset, makeGrid(), settings,
        {1.0e4, 1.5e4, 2.2e4}, {0.20, 0.30, 0.40});

    assert(calibration.candidates.size() == 9U);
    assert(calibration.selectedIndex < calibration.candidates.size());
    const auto& selected = calibration.candidates[calibration.selectedIndex];
    assert(selected.selected);
    assert(std::abs(selected.youngModulus - 1.5e4) < 1.0e-12);
    assert(std::abs(selected.poissonRatio - 0.30) < 1.0e-12);
    assert(selected.fitDynamicSamples == 9U);
    assert(selected.validationDynamicSamples == 6U);
    assert(selected.fitDynamicRms < 1.0e-10);
    assert(selected.validationDynamicRms < 1.0e-10);
    assert(calibration.selectedReplay.initializationFitRms < 1.0e-12);
    assert(calibration.selectedReplay.appearanceRoundtripRms < 1.0e-10);

    // Selection must be driven by dynamic fit rows, not t=0 initialization or validation rows.
    for (const auto& candidate : calibration.candidates) {
        assert(candidate.fitDynamicSamples == 9U);
        assert(candidate.validationDynamicSamples == 6U);
        assert(candidate.initializationFitRms < 1.0e-12);
    }
    return 0;
}
