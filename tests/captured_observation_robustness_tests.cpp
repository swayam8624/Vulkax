#include "vulkax/research/captured_observation_robustness.hpp"

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

bool samePosition(math::Vec3 lhs, math::Vec3 rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool finiteSample(const research::CapturedObservationRobustnessSample& sample) {
    return std::isfinite(sample.selectedYoungModulus) &&
           std::isfinite(sample.selectedPoissonRatio) &&
           std::isfinite(sample.fitDynamicRms) &&
           std::isfinite(sample.validationDynamicRms) &&
           std::isfinite(sample.initializationFitRms) &&
           std::isfinite(sample.appearanceRoundtripRms) &&
           std::isfinite(sample.particleInfluenceCosineSimilarity) &&
           std::isfinite(sample.particleInfluenceRelativeL2Error) &&
           std::isfinite(sample.adaptiveAbsoluteGradientFraction) &&
           std::isfinite(sample.adaptiveParticleJaccardWithBaseline);
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

    // The perturbation itself must be deterministic and must respect the
    // initial-vs-dynamic separation used by the calibration protocol.
    const research::CapturedObservationNoiseScenario dynamicOnly{"dynamic", 0.0, 1.0e-6, 17U};
    const auto dynamicPerturbedA = research::perturbCapturedObservations(dataset, dynamicOnly);
    const auto dynamicPerturbedB = research::perturbCapturedObservations(dataset, dynamicOnly);
    bool changedDynamic = false;
    for (std::size_t index = 0; index < dataset.observations.size(); ++index) {
        const auto& clean = dataset.observations[index];
        assert(samePosition(dynamicPerturbedA.observations[index].position,
                            dynamicPerturbedB.observations[index].position));
        if (clean.time == 0.0) {
            assert(samePosition(clean.position, dynamicPerturbedA.observations[index].position));
        } else if (!samePosition(clean.position, dynamicPerturbedA.observations[index].position)) {
            changedDynamic = true;
        }
    }
    assert(changedDynamic);

    const research::CapturedObservationNoiseScenario initialOnly{"initial", 1.0e-6, 0.0, 23U};
    const auto initialPerturbed = research::perturbCapturedObservations(dataset, initialOnly);
    bool changedInitial = false;
    for (std::size_t index = 0; index < dataset.observations.size(); ++index) {
        const auto& clean = dataset.observations[index];
        if (clean.time == 0.0 && !samePosition(clean.position, initialPerturbed.observations[index].position))
            changedInitial = true;
        if (clean.time > 0.0)
            assert(samePosition(clean.position, initialPerturbed.observations[index].position));
    }
    assert(changedInitial);

    research::NonlinearDeformableWorldSettings settings;
    settings.dt = 1.0e-4;
    settings.material.density = 1000.0;
    settings.couplingNeighborCount = 20;
    settings.transferScheme = solvers::MpmTransferScheme::APIC;
    settings.flipBlend = 0.0;

    research::CapturedMaterialInfluenceSettings influence;
    influence.objectiveMarkerId = "m4";
    influence.objectiveTime = 0.003;
    influence.objectiveDirection = {1.0, 1.0, 1.0};
    influence.finiteDifferenceScaleStep = 0.01;
    influence.verificationScaleDelta = 0.02;

    research::CapturedMaterialAdaptiveRegionSettings adaptive;
    adaptive.cumulativeAbsoluteGradientFraction = 0.90;
    adaptive.relativeParticleGradientThreshold = 0.05;
    adaptive.adjacencyRadiusMultiplier = 1.05;
    adaptive.maximumRegions = 8;

    const auto result = research::evaluateCapturedObservationRobustness(
        capturedWorld,
        active,
        dataset,
        makeGrid(),
        settings,
        {1.0e4, 1.5e4, 2.2e4},
        {0.20, 0.30, 0.40},
        influence,
        adaptive,
        {
            {"zero_clone", 0.0, 0.0, 31U},
            {"small_dynamic", 0.0, 1.0e-6, 37U},
            {"small_initial", 1.0e-6, 0.0, 41U},
        });

    assert(result.scenarios.size() == 3U);
    assert(finiteSample(result.baseline));
    assert(std::abs(result.baseline.selectedYoungModulus - 1.5e4) < 1.0e-12);
    assert(std::abs(result.baseline.selectedPoissonRatio - 0.30) < 1.0e-12);
    assert(result.baseline.adaptiveParticleCount > 0U);
    assert(result.baseline.adaptiveParticleCount < dataset.particles.size());
    assert(result.baseline.adaptiveAbsoluteGradientFraction >= 0.90);

    const auto& zeroClone = result.scenarios[0];
    assert(finiteSample(zeroClone));
    assert(zeroClone.selectedYoungModulus == result.baseline.selectedYoungModulus);
    assert(zeroClone.selectedPoissonRatio == result.baseline.selectedPoissonRatio);
    assert(zeroClone.youngModulusRelativeDeltaFromBaseline == 0.0);
    assert(zeroClone.poissonRatioAbsoluteDeltaFromBaseline == 0.0);
    assert(std::abs(zeroClone.particleInfluenceCosineSimilarity - 1.0) < 1.0e-14);
    assert(zeroClone.particleInfluenceRelativeL2Error < 1.0e-14);
    assert(zeroClone.strongestParticleMatchesBaseline);
    assert(std::abs(zeroClone.adaptiveParticleJaccardWithBaseline - 1.0) < 1.0e-14);

    for (std::size_t index = 1; index < result.scenarios.size(); ++index) {
        const auto& sample = result.scenarios[index];
        assert(finiteSample(sample));
        assert(sample.selectedYoungModulus > 0.0);
        assert(sample.selectedPoissonRatio > -1.0 && sample.selectedPoissonRatio < 0.5);
        assert(sample.fitDynamicRms >= 0.0);
        assert(sample.validationDynamicRms >= 0.0);
        assert(sample.initializationFitRms >= 0.0);
        assert(sample.particleInfluenceCosineSimilarity >= -1.0);
        assert(sample.particleInfluenceCosineSimilarity <= 1.0);
        assert(sample.particleInfluenceRelativeL2Error >= 0.0);
        assert(sample.adaptiveRegionCount >= 1U);
        assert(sample.adaptiveParticleCount >= 1U);
        assert(sample.adaptiveParticleCount <= dataset.particles.size());
        assert(sample.adaptiveAbsoluteGradientFraction > 0.0);
        assert(sample.adaptiveAbsoluteGradientFraction <= 1.0 + 1.0e-12);
        assert(sample.adaptiveParticleJaccardWithBaseline >= 0.0);
        assert(sample.adaptiveParticleJaccardWithBaseline <= 1.0);
    }

    return 0;
}
