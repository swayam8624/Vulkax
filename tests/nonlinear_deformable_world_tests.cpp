#include "vulkax/research/nonlinear_deformable_world.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

vulkax::math::Vec3 multiplyMatrixVector(
    const vulkax::solvers::Matrix3& matrix,
    vulkax::math::Vec3 value) {
    return {
        matrix[0] * value.x + matrix[1] * value.y + matrix[2] * value.z,
        matrix[3] * value.x + matrix[4] * value.y + matrix[5] * value.z,
        matrix[6] * value.x + matrix[7] * value.y + matrix[8] * value.z,
    };
}

std::vector<vulkax::solvers::MpmParticle> makeBody() {
    using namespace vulkax;
    std::vector<solvers::MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double spacing = 0.12;
    constexpr double restVolume = spacing * spacing * spacing;
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
                particle.restVolume = restVolume;
                particle.mass = density * restVolume;
                particles.push_back(particle);
            }
    return particles;
}

vulkax::gaussian::GaussianSplat splat(vulkax::math::Vec3 position, double scale) {
    vulkax::gaussian::GaussianSplat result;
    result.position = position;
    result.logScale = {std::log(scale), std::log(scale * 0.72), std::log(scale * 0.55)};
    result.rotation = {1.0, 0.0, 0.0, 0.0};
    result.opacityLogit = 4.0;
    result.shDC = {0.25, 0.08, -0.12};
    return result;
}

vulkax::gaussian::GaussianCloud makeWorld() {
    vulkax::gaussian::GaussianCloud world;
    world.splats.push_back(splat({-0.08, 0.04, 0.02}, 0.07));
    world.splats.push_back(splat({0.09, -0.06, 0.03}, 0.065));
    world.splats.push_back(splat({0.04, 0.10, -0.07}, 0.055));
    world.splats.push_back(splat({-0.05, -0.08, -0.06}, 0.06));
    world.splats.push_back(splat({0.76, 0.55, -0.30}, 0.08)); // locality control
    return world;
}

vulkax::solvers::MpmGridSettings makeGrid() {
    vulkax::solvers::MpmGridSettings grid;
    grid.origin = {-1.0, -1.0, -1.0};
    grid.nx = 26;
    grid.ny = 26;
    grid.nz = 26;
    grid.cellSize = 0.08;
    grid.boundaryCells = 0;
    return grid;
}

} // namespace

int main() {
    using namespace vulkax;
    const auto initialWorld = makeWorld();
    research::NonlinearDeformableWorldSettings settings;
    settings.steps = 120;
    settings.dt = 2.0e-4;
    settings.material = {1000.0, 1.5e4, 0.30};
    settings.couplingNeighborCount = 20;

    std::size_t observerCalls = 0;
    std::size_t lastObservedStep = 0;
    const auto observer = [&](const research::NonlinearDeformableWorldFrameEvidence& frame,
                              const gaussian::GaussianCloud& world) {
        ++observerCalls;
        lastObservedStep = frame.step;
        assert(world.size() == initialWorld.size());
        assert(frame.step == observerCalls);
        assert(std::isfinite(world.splats.front().position.x));
        assert(frame.unaffectedRegionDrift == 0.0);
    };

    const auto result = research::runNonlinearDeformableWorld(
        initialWorld, {0, 1, 2, 3}, makeBody(), makeGrid(), settings, observer);

    assert(observerCalls == settings.steps);
    assert(lastObservedStep == settings.steps);
    assert(result.frames.size() == settings.steps);
    assert(result.finalParticles.size() == 64);
    assert(result.finalWorld.size() == initialWorld.size());
    assert(std::isfinite(result.initialMechanicalEnergy));
    assert(result.initialMechanicalEnergy > 0.0);
    assert(std::isfinite(result.finalMechanicalEnergy));
    assert(result.minimumDeformationDeterminant > 0.5);
    assert(result.maximumDeformationDeterminant < 1.5);
    assert(result.maximumMassConservationError < 1.0e-10);
    assert(result.maximumMomentumConservationError < 1.0e-9);
    assert(result.maximumForceBalanceError < 1.0e-8);
    assert(result.maximumMomentumBalanceError < 1.0e-8);
    assert(result.maximumCenterOfMassDrift < 1.0e-8);
    assert(result.maximumRelativeMechanicalEnergyDrift < 0.75);
    assert(result.maximumMlsRmsResidual < 0.05);
    assert(result.maximumMlsResidual < 0.12);
    assert(result.maximumGaussianDisplacement > 1.0e-6);
    assert(result.maximumUnaffectedRegionDrift == 0.0);

    for (const auto& frame : result.frames) {
        assert(std::isfinite(frame.kineticEnergy));
        assert(std::isfinite(frame.elasticEnergy));
        assert(std::isfinite(frame.mechanicalEnergy));
        assert(frame.kineticEnergy >= 0.0);
        assert(frame.elasticEnergy >= -1.0e-10);
        assert(frame.minimumDeformationDeterminant > 0.0);
        assert(frame.unaffectedRegionDrift == 0.0);
    }

    const auto output = std::filesystem::temp_directory_path() / "vulkax_nonlinear_deformable_world.csv";
    research::writeNonlinearDeformableWorldEvidenceCsv(result, output);
    assert(std::filesystem::exists(output));
    assert(std::filesystem::file_size(output) > 256U);
    std::ifstream stream(output);
    std::string header;
    std::getline(stream, header);
    assert(header.find("relative_energy_drift") != std::string::npos);
    assert(header.find("max_mls_rms_residual") != std::string::npos);
    stream.close();
    std::filesystem::remove(output);

    // The experiment must not have merely translated the entire body rigidly.
    const auto initialBody = makeBody();
    const auto deformedFirst = multiplyMatrixVector(
        settings.initialDeformation,
        initialBody.front().restPosition);
    assert(math::length(result.finalParticles.front().position - deformedFirst) > 1.0e-7);
    return 0;
}
