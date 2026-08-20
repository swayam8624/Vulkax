#include "vulkax/cli/deformable_animation.hpp"

#include "vulkax/backend/backend.hpp"
#include "vulkax/render/gaussian.hpp"
#include "vulkax/render/headless.hpp"
#include "vulkax/research/nonlinear_deformable_world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace vulkax::cli {
namespace {

std::optional<backend::BackendKind> parseBackend(std::string_view name) {
    if (name == "Metal") return backend::BackendKind::Metal;
    if (name == "Vulkan") return backend::BackendKind::Vulkan;
    return std::nullopt;
}

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

    // Locality-control splat remains outside the coupled region and camera view.
    result.cloud.splats.push_back(makeSplat({0.78, 0.58, -0.28}, 0.08, 0.5));
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

backend::BackendKind selectBackend(int argc, char** argv) {
    const auto available = render::availableHeadlessRenderBackends();
    if (available.empty()) throw std::runtime_error("no native Gaussian render backend is available");
    if (argc >= 4) {
        const auto requested = parseBackend(argv[3]);
        if (!requested) throw std::invalid_argument("animation backend must be Metal or Vulkan");
        if (std::find(available.begin(), available.end(), *requested) == available.end())
            throw std::runtime_error("requested animation backend is not available in this build");
        return *requested;
    }
    const auto metal = std::find(available.begin(), available.end(), backend::BackendKind::Metal);
    if (backend::currentPlatform() == backend::PlatformKind::MacOS && metal != available.end())
        return backend::BackendKind::Metal;
    const auto vulkan = std::find(available.begin(), available.end(), backend::BackendKind::Vulkan);
    if (vulkan != available.end()) return backend::BackendKind::Vulkan;
    return available.front();
}

std::size_t parseStride(int argc, char** argv) {
    if (argc < 5) return 4;
    const std::string_view text(argv[4]);
    std::size_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') throw std::invalid_argument("animation stride must be a positive integer");
        value = value * 10U + static_cast<std::size_t>(c - '0');
    }
    if (value == 0) throw std::invalid_argument("animation stride must be positive");
    return value;
}

std::string frameName(std::size_t step) {
    std::ostringstream stream;
    stream << "frame_" << std::setw(5) << std::setfill('0') << step << ".ppm";
    return stream.str();
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

} // namespace

int deformableAnimationCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "deformable-nonlinear-render") return -1;
    if (argc < 3)
        throw std::invalid_argument(
            "usage: vulkax deformable-nonlinear-render <output-dir> [Metal|Vulkan] [stride]");

    const std::filesystem::path outputDirectory(argv[2]);
    const std::filesystem::path framesDirectory = outputDirectory / "frames";
    std::filesystem::create_directories(framesDirectory);

    const backend::BackendKind selectedBackend = selectBackend(argc, argv);
    const std::size_t stride = parseStride(argc, argv);
    const auto settings = renderSettings();

    auto dense = makeDenseWorld();
    research::NonlinearDeformableWorldSettings physics;
    physics.steps = 240;
    physics.dt = 2.0e-4;
    physics.material = {1000.0, 1.5e4, 0.30};
    physics.couplingNeighborCount = 20;

    std::ofstream manifest(outputDirectory / "frames.csv");
    if (!manifest) throw std::runtime_error("failed to open nonlinear animation frame manifest");
    manifest << "render_index,step,time,frame,relative_energy_drift,max_mls_rms_residual,"
                "max_mls_residual,max_gaussian_displacement,unaffected_region_drift\n";
    manifest << std::setprecision(17);

    std::size_t renderedFrames = 0;
    std::size_t totalVisibleSplats = 0;
    const auto observer = [&](const research::NonlinearDeformableWorldFrameEvidence& frame,
                              const gaussian::GaussianCloud& world) {
        if (frame.step % stride != 0 && frame.step != physics.steps) return;
        const auto rendered = render::renderGaussianCloudHeadless(selectedBackend, world, settings);
        const std::string filename = frameName(frame.step);
        render::writePpm(rendered.image, framesDirectory / filename);
        totalVisibleSplats += rendered.stats.visibleSplats;
        manifest << renderedFrames << ',' << frame.step << ',' << frame.time << ',' << filename << ','
                 << frame.relativeMechanicalEnergyDrift << ',' << frame.maximumMlsRmsResidual << ','
                 << frame.maximumMlsResidual << ',' << frame.maximumGaussianDisplacement << ','
                 << frame.unaffectedRegionDrift << '\n';
        ++renderedFrames;
    };

    const auto result = research::runNonlinearDeformableWorld(
        std::move(dense.cloud), dense.activeIndices, makeBody(), makeGrid(), physics, observer);
    research::writeNonlinearDeformableWorldEvidenceCsv(result, outputDirectory / "evidence.csv");
    manifest.flush();
    if (!manifest) throw std::runtime_error("failed while writing nonlinear animation frame manifest");

    std::cout << std::setprecision(8)
              << "Nonlinear Gaussian-world animation\n"
              << "  backend: " << backend::toString(selectedBackend) << '\n'
              << "  physics_particles: " << result.finalParticles.size() << '\n'
              << "  appearance_gaussians: " << result.finalWorld.size() - 1U << '\n'
              << "  simulated_steps: " << result.frames.size() << '\n'
              << "  rendered_frames: " << renderedFrames << '\n'
              << "  stride: " << stride << '\n'
              << "  mean_visible_splats: "
              << (renderedFrames == 0 ? 0.0 : static_cast<double>(totalVisibleSplats) / static_cast<double>(renderedFrames))
              << '\n'
              << "  max_relative_energy_drift: " << result.maximumRelativeMechanicalEnergyDrift << '\n'
              << "  min_J: " << result.minimumDeformationDeterminant << '\n'
              << "  max_J: " << result.maximumDeformationDeterminant << '\n'
              << "  max_mls_rms_residual: " << result.maximumMlsRmsResidual << '\n'
              << "  max_mls_residual: " << result.maximumMlsResidual << '\n'
              << "  max_gaussian_displacement: " << result.maximumGaussianDisplacement << '\n'
              << "  max_unaffected_region_drift: " << result.maximumUnaffectedRegionDrift << '\n'
              << "  frames: " << framesDirectory.string() << '\n'
              << "  manifest: " << (outputDirectory / "frames.csv").string() << '\n'
              << "  evidence: " << (outputDirectory / "evidence.csv").string() << '\n';
    return 0;
}

} // namespace vulkax::cli
