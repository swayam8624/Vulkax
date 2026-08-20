#include "vulkax/cli/research_validation.hpp"

#include "vulkax/backend/backend.hpp"
#include "vulkax/render/gaussian.hpp"
#include "vulkax/render/headless.hpp"
#include "vulkax/render/image_metrics.hpp"
#include "vulkax/research/nonlinear_deformable_world.hpp"
#include "vulkax/research/timestep_convergence.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vulkax::cli {
namespace {

std::vector<solvers::MpmParticle> makeBody() {
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

gaussian::GaussianSplat makeSplat(math::Vec3 position, double scale, double tint) {
    gaussian::GaussianSplat splat;
    splat.position = position;
    splat.logScale = {std::log(scale), std::log(scale * 0.82), std::log(scale * 0.66)};
    splat.rotation = {1.0, 0.0, 0.0, 0.0};
    splat.opacityLogit = 3.5;
    splat.shDC = {0.25 + 0.25 * tint, 0.02 + 0.12 * tint, -0.10 + 0.18 * tint};
    return splat;
}

struct DenseWorld {
    gaussian::GaussianCloud cloud;
    std::vector<std::size_t> activeIndices;
};

DenseWorld makeDenseWorld() {
    DenseWorld result;
    result.cloud.splats.reserve(126);
    result.activeIndices.reserve(125);
    constexpr int resolution = 5;
    constexpr double spacing = 0.075;
    constexpr double scale = 0.045;
    for (int iz = 0; iz < resolution; ++iz)
        for (int iy = 0; iy < resolution; ++iy)
            for (int ix = 0; ix < resolution; ++ix) {
                const math::Vec3 position{
                    (static_cast<double>(ix) - 2.0) * spacing,
                    (static_cast<double>(iy) - 2.0) * spacing,
                    (static_cast<double>(iz) - 2.0) * spacing,
                };
                const double tint = static_cast<double>(ix + iy + iz) / 12.0;
                result.activeIndices.push_back(result.cloud.splats.size());
                result.cloud.splats.push_back(makeSplat(position, scale, tint));
            }

    // Retained in the simulated world as a strict locality control. Validation
    // renderers explicitly exclude it from the appearance image.
    result.cloud.splats.push_back(makeSplat({5.0, 5.0, 5.0}, 0.08, 0.5));
    return result;
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

research::NonlinearDeformableWorldSettings physicsSettings() {
    research::NonlinearDeformableWorldSettings physics;
    physics.steps = 240;
    physics.dt = 2.0e-4;
    physics.material = {1000.0, 1.5e4, 0.30};
    physics.couplingNeighborCount = 20;
    return physics;
}

render::GaussianRenderSettings renderSettings() {
    render::GaussianRenderSettings settings;
    settings.image.width = 1280;
    settings.image.height = 720;
    settings.camera.position = {0.62, 0.46, 1.15};
    settings.camera.target = {0.0, 0.0, 0.0};
    settings.camera.up = {0.0, 1.0, 0.0};
    settings.camera.verticalFovDegrees = 42.0;
    settings.nearPlane = 1.0e-4;
    settings.sigmaCutoff = 3.0;
    return settings;
}

std::size_t parsePositiveSize(std::string_view text, std::string_view label) {
    std::size_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9')
            throw std::invalid_argument(std::string(label) + " must be a positive integer");
        value = value * 10U + static_cast<std::size_t>(c - '0');
    }
    if (value == 0)
        throw std::invalid_argument(std::string(label) + " must be positive");
    return value;
}

std::string frameName(std::size_t step) {
    std::ostringstream stream;
    stream << "frame_" << std::setw(5) << std::setfill('0') << step << ".ppm";
    return stream.str();
}

gaussian::GaussianCloud appearanceOnly(const gaussian::GaussianCloud& world,
                                       std::size_t activeCount) {
    if (activeCount > world.size())
        throw std::out_of_range("active appearance count exceeds Gaussian world size");
    gaussian::GaussianCloud result;
    result.shRestCoefficientsPerSplat = world.shRestCoefficientsPerSplat;
    result.splats.assign(world.splats.begin(), world.splats.begin() + static_cast<std::ptrdiff_t>(activeCount));
    return result;
}

void requireMetalAndVulkan() {
    const auto available = render::availableHeadlessRenderBackends();
    const bool hasMetal = std::find(
        available.begin(), available.end(), backend::BackendKind::Metal) != available.end();
    const bool hasVulkan = std::find(
        available.begin(), available.end(), backend::BackendKind::Vulkan) != available.end();
    if (!hasMetal || !hasVulkan)
        throw std::runtime_error(
            "backend comparison requires both native Metal and Vulkan render paths");
}

} // namespace

int deformableBackendCompareCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "deformable-backend-compare") return -1;
    if (argc < 3)
        throw std::invalid_argument(
            "usage: vulkax deformable-backend-compare <output-dir> [stride]");

    requireMetalAndVulkan();
    const std::filesystem::path outputDirectory(argv[2]);
    const std::filesystem::path metalDirectory = outputDirectory / "metal";
    const std::filesystem::path vulkanDirectory = outputDirectory / "vulkan";
    std::filesystem::create_directories(metalDirectory);
    std::filesystem::create_directories(vulkanDirectory);
    const std::size_t stride = argc >= 4 ? parsePositiveSize(argv[3], "comparison stride") : 4U;

    auto dense = makeDenseWorld();
    const std::size_t activeCount = dense.activeIndices.size();
    const auto physics = physicsSettings();
    const auto graphics = renderSettings();

    std::ofstream csv(outputDirectory / "backend_comparison.csv");
    if (!csv) throw std::runtime_error("failed to open backend-comparison CSV output");
    csv << "render_index,step,time,max_channel_difference,mean_absolute_difference,rmse,psnr_db,"
           "changed_pixel_fraction,metal_visible,vulkan_visible,relative_energy_drift,"
           "max_mls_rms_residual,max_mls_residual,max_gaussian_displacement,"
           "unaffected_region_drift\n";
    csv << std::setprecision(17);

    std::size_t renderedFrames = 0;
    std::uint8_t maximumChannelDifference = 0;
    double maximumMeanAbsoluteDifference = 0.0;
    double maximumRmse = 0.0;
    double minimumPsnr = std::numeric_limits<double>::infinity();
    double maximumChangedPixelFraction = 0.0;

    const auto observer = [&](const research::NonlinearDeformableWorldFrameEvidence& frame,
                              const gaussian::GaussianCloud& world) {
        if (frame.step % stride != 0 && frame.step != physics.steps) return;
        const auto appearance = appearanceOnly(world, activeCount);
        const auto metal = render::renderGaussianCloudHeadless(
            backend::BackendKind::Metal, appearance, graphics);
        const auto vulkan = render::renderGaussianCloudHeadless(
            backend::BackendKind::Vulkan, appearance, graphics);
        const auto comparison = render::compareImages(metal.image, vulkan.image);
        const std::string filename = frameName(frame.step);
        render::writePpm(metal.image, (metalDirectory / filename).string());
        render::writePpm(vulkan.image, (vulkanDirectory / filename).string());

        maximumChannelDifference = std::max(
            maximumChannelDifference, comparison.maximumChannelDifference);
        maximumMeanAbsoluteDifference = std::max(
            maximumMeanAbsoluteDifference, comparison.meanAbsoluteDifference);
        maximumRmse = std::max(maximumRmse, comparison.rootMeanSquareError);
        minimumPsnr = std::min(minimumPsnr, comparison.psnrDb);
        maximumChangedPixelFraction = std::max(
            maximumChangedPixelFraction, comparison.changedPixelFraction);

        csv << renderedFrames << ',' << frame.step << ',' << frame.time << ','
            << static_cast<unsigned int>(comparison.maximumChannelDifference) << ','
            << comparison.meanAbsoluteDifference << ','
            << comparison.rootMeanSquareError << ','
            << comparison.psnrDb << ','
            << comparison.changedPixelFraction << ','
            << metal.stats.visibleSplats << ',' << vulkan.stats.visibleSplats << ','
            << frame.relativeMechanicalEnergyDrift << ','
            << frame.maximumMlsRmsResidual << ','
            << frame.maximumMlsResidual << ','
            << frame.maximumGaussianDisplacement << ','
            << frame.unaffectedRegionDrift << '\n';
        ++renderedFrames;
    };

    const auto result = research::runNonlinearDeformableWorld(
        std::move(dense.cloud), dense.activeIndices, makeBody(), makeGrid(), physics, observer);
    research::writeNonlinearDeformableWorldEvidenceCsv(
        result, outputDirectory / "evidence.csv");
    csv.flush();
    if (!csv) throw std::runtime_error("failed while writing backend-comparison CSV output");

    std::cout << std::setprecision(8)
              << "Metal/Vulkan Gaussian backend comparison\n"
              << "  physics_particles: " << result.finalParticles.size() << '\n'
              << "  appearance_gaussians: " << activeCount << '\n'
              << "  rendered_frames: " << renderedFrames << '\n'
              << "  stride: " << stride << '\n'
              << "  max_channel_difference: "
              << static_cast<unsigned int>(maximumChannelDifference) << '\n'
              << "  max_mean_absolute_difference: " << maximumMeanAbsoluteDifference << '\n'
              << "  max_rmse: " << maximumRmse << '\n'
              << "  min_psnr_db: " << minimumPsnr << '\n'
              << "  max_changed_pixel_fraction: " << maximumChangedPixelFraction << '\n'
              << "  max_relative_energy_drift: "
              << result.maximumRelativeMechanicalEnergyDrift << '\n'
              << "  max_unaffected_region_drift: "
              << result.maximumUnaffectedRegionDrift << '\n'
              << "  csv: " << (outputDirectory / "backend_comparison.csv").string() << '\n';
    return 0;
}

int deformableTimestepSweepCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "deformable-timestep-sweep") return -1;
    if (argc < 3)
        throw std::invalid_argument(
            "usage: vulkax deformable-timestep-sweep <output.csv>");

    auto dense = makeDenseWorld();
    auto settings = physicsSettings();
    constexpr double physicalHorizon = 0.048;
    const auto sweep = research::runNonlinearTimestepSweep(
        std::move(dense.cloud), dense.activeIndices, makeBody(), makeGrid(), settings,
        physicalHorizon, {2.0e-4, 1.0e-4, 5.0e-5});
    research::writeNonlinearTimestepSweepCsv(sweep, argv[2]);

    std::cout << std::setprecision(10)
              << "Nonlinear deformable-world timestep sweep\n"
              << "  physical_horizon: " << physicalHorizon << '\n';
    for (const auto& level : sweep.levels) {
        const auto& experiment = level.experiment;
        std::cout << "  dt=" << level.dt
                  << " | steps=" << level.steps
                  << " | max_energy_drift=" << experiment.maximumRelativeMechanicalEnergyDrift
                  << " | final_energy=" << experiment.finalMechanicalEnergy
                  << " | particle_pos_rms_to_finest=" << level.particlePositionRmsToFinest
                  << " | particle_vel_rms_to_finest=" << level.particleVelocityRmsToFinest
                  << " | gaussian_pos_rms_to_finest=" << level.gaussianPositionRmsToFinest
                  << "\n";
    }
    std::cout << "  observed_particle_position_order: "
              << sweep.observedParticlePositionOrder << '\n'
              << "  observed_particle_velocity_order: "
              << sweep.observedParticleVelocityOrder << '\n'
              << "  observed_gaussian_position_order: "
              << sweep.observedGaussianPositionOrder << '\n'
              << "  csv: " << argv[2] << '\n';
    return 0;
}

} // namespace vulkax::cli
