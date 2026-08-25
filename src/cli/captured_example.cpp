#include "vulkax/cli/captured_example.hpp"

#include "vulkax/capture/deformable_dataset.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/research/nonlinear_deformable_world.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vulkax::cli {
namespace {

[[nodiscard]] std::vector<solvers::MpmParticle> makeBody() {
    std::vector<solvers::MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double spacing = 0.12;
    constexpr double volume = spacing * spacing * spacing;
    constexpr double density = 1000.0;
    particles.reserve(64U);
    for (int iz = 0; iz < 4; ++iz) {
        for (int iy = 0; iy < 4; ++iy) {
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
        }
    }
    return particles;
}

[[nodiscard]] gaussian::GaussianSplat makeSplat(math::Vec3 position) {
    gaussian::GaussianSplat splat;
    splat.position = position;
    splat.logScale = {std::log(0.055), std::log(0.045), std::log(0.035)};
    splat.rotation = {1.0, 0.0, 0.0, 0.0};
    splat.opacityLogit = 4.0;
    splat.shDC = {0.0, 0.0, 0.0};
    return splat;
}

[[nodiscard]] gaussian::GaussianCloud makeWorld() {
    gaussian::GaussianCloud world;
    world.splats.push_back(makeSplat({-0.10, -0.05, -0.02}));
    world.splats.push_back(makeSplat({0.11, -0.04, 0.03}));
    world.splats.push_back(makeSplat({-0.03, 0.10, -0.06}));
    world.splats.push_back(makeSplat({0.04, 0.06, 0.09}));
    world.splats.push_back(makeSplat({0.00, 0.00, 0.00}));
    return world;
}

[[nodiscard]] math::Vec3 applyAffine(
    const solvers::Matrix3& matrix,
    math::Vec3 translation,
    math::Vec3 point) noexcept {
    return {
        matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + translation.x,
        matrix[3] * point.x + matrix[4] * point.y + matrix[5] * point.z + translation.y,
        matrix[6] * point.x + matrix[7] * point.y + matrix[8] * point.z + translation.z,
    };
}

[[nodiscard]] solvers::MpmGridSettings makeGrid() {
    solvers::MpmGridSettings grid;
    grid.origin = {-1.0, -1.0, -1.0};
    grid.nx = 26U;
    grid.ny = 26U;
    grid.nz = 26U;
    grid.cellSize = 0.08;
    grid.boundaryCells = 0U;
    return grid;
}

[[nodiscard]] capture::CapturedDeformableDataset makeDataset(
    const std::vector<solvers::MpmParticle>& body,
    const solvers::Matrix3& deformation,
    math::Vec3 translation,
    const std::unordered_map<std::size_t, std::vector<math::Vec3>>& trajectories) {
    capture::CapturedDeformableDataset dataset;
    dataset.particles.reserve(body.size());
    for (const auto& particle : body) {
        dataset.particles.push_back({
            particle.id,
            particle.restPosition,
            particle.mass,
            particle.restVolume,
        });
    }

    const std::array<std::uint64_t, 5> markers{1U, 4U, 13U, 49U, 64U};
    for (std::size_t marker = 0; marker < markers.size(); ++marker) {
        const auto particleIndex = static_cast<std::size_t>(markers[marker] - 1U);
        const auto& particle = body.at(particleIndex);
        dataset.observations.push_back({
            "m" + std::to_string(marker),
            markers[marker],
            0.0,
            applyAffine(deformation, translation, particle.restPosition),
            marker < 4U ? capture::ObservationSplit::Fit : capture::ObservationSplit::Validation,
        });
    }

    for (const std::size_t step : std::array<std::size_t, 3>{10U, 20U, 30U}) {
        const auto& positions = trajectories.at(step);
        for (std::size_t marker = 0; marker < markers.size(); ++marker) {
            const auto particleIndex = static_cast<std::size_t>(markers[marker] - 1U);
            dataset.observations.push_back({
                "m" + std::to_string(marker),
                markers[marker],
                static_cast<double>(step) * 1.0e-4,
                positions.at(particleIndex),
                marker < 3U ? capture::ObservationSplit::Fit : capture::ObservationSplit::Validation,
            });
        }
    }
    return dataset;
}

void writeGaussianPly(const gaussian::GaussianCloud& world, const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open synthetic captured Gaussian PLY");
    stream << "ply\n"
              "format ascii 1.0\n"
              "comment Vulkax deterministic captured-deformable calibration example\n"
              "element vertex " << world.size() << "\n"
              "property float x\n"
              "property float y\n"
              "property float z\n"
              "property float f_dc_0\n"
              "property float f_dc_1\n"
              "property float f_dc_2\n"
              "property float opacity\n"
              "property float scale_0\n"
              "property float scale_1\n"
              "property float scale_2\n"
              "property float rot_0\n"
              "property float rot_1\n"
              "property float rot_2\n"
              "property float rot_3\n"
              "end_header\n";
    stream << std::setprecision(17);
    for (const auto& splat : world.splats) {
        stream << splat.position.x << ' ' << splat.position.y << ' ' << splat.position.z << ' '
               << splat.shDC[0] << ' ' << splat.shDC[1] << ' ' << splat.shDC[2] << ' '
               << splat.opacityLogit << ' '
               << splat.logScale[0] << ' ' << splat.logScale[1] << ' ' << splat.logScale[2] << ' '
               << splat.rotation[0] << ' ' << splat.rotation[1] << ' '
               << splat.rotation[2] << ' ' << splat.rotation[3] << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing synthetic captured Gaussian PLY");
}

void writeParticlesCsv(
    const std::vector<capture::CapturedParticleSpec>& particles,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open synthetic captured particle CSV");
    stream << "particle_id,rest_x,rest_y,rest_z,mass,rest_volume\n";
    stream << std::setprecision(17);
    for (const auto& particle : particles) {
        stream << particle.particleId << ','
               << particle.restPosition.x << ',' << particle.restPosition.y << ',' << particle.restPosition.z << ','
               << particle.mass << ',' << particle.restVolume << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing synthetic captured particle CSV");
}

void writeObservationsCsv(
    const std::vector<capture::CapturedMarkerObservation>& observations,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open synthetic captured observation CSV");
    stream << "marker_id,particle_id,time,x,y,z,split\n";
    stream << std::setprecision(17);
    for (const auto& observation : observations) {
        stream << observation.markerId << ',' << observation.particleId << ',' << observation.time << ','
               << observation.position.x << ',' << observation.position.y << ',' << observation.position.z << ','
               << capture::toString(observation.split) << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing synthetic captured observation CSV");
}

void writeTruthCsv(const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open synthetic captured truth CSV");
    stream << "density,young_modulus,poisson_ratio,dt,grid_cell_size,steps\n"
           << "1000,15000,0.30,0.0001,0.08,30\n";
    if (!stream) throw std::runtime_error("failed while writing synthetic captured truth CSV");
}

} // namespace

int capturedExampleCommand(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "captured-deformable-generate-example") return -1;
    if (argc != 3) {
        throw std::invalid_argument(
            "usage: vulkax captured-deformable-generate-example <output-dir>");
    }

    const std::filesystem::path outputDirectory(argv[2]);
    const auto body = makeBody();
    const solvers::Matrix3 deformation{
        1.04, 0.04, 0.00,
        0.01, 0.97, 0.02,
        0.00, 0.01, 1.02,
    };
    const math::Vec3 translation{0.035, -0.021, 0.014};

    research::NonlinearDeformableWorldSettings truth;
    truth.steps = 30U;
    truth.dt = 1.0e-4;
    truth.material = {1000.0, 1.5e4, 0.30};
    truth.initialDeformation = deformation;
    truth.initialTranslation = translation;
    truth.couplingNeighborCount = 20U;
    truth.transferScheme = solvers::MpmTransferScheme::APIC;

    std::unordered_map<std::size_t, std::vector<math::Vec3>> trajectories;
    const auto observer = [&](const research::NonlinearDeformableWorldFrameEvidence& frame,
                              const gaussian::GaussianCloud&,
                              const std::vector<solvers::MpmParticle>& particles) {
        if (frame.step != 10U && frame.step != 20U && frame.step != 30U) return;
        auto& output = trajectories[frame.step];
        output.reserve(particles.size());
        for (const auto& particle : particles) output.push_back(particle.position);
    };

    const std::vector<std::size_t> active{0U, 1U, 2U, 3U, 4U};
    const auto restWorld = makeWorld();
    (void)research::runNonlinearDeformableWorld(
        restWorld, active, body, makeGrid(), truth, {}, observer);
    if (trajectories.size() != 3U) {
        throw std::runtime_error("synthetic captured example did not record all trajectory checkpoints");
    }

    auto capturedWorld = restWorld;
    for (auto& splat : capturedWorld.splats) {
        splat.position = applyAffine(deformation, translation, splat.position);
    }
    const auto dataset = makeDataset(body, deformation, translation, trajectories);

    std::filesystem::create_directories(outputDirectory);
    writeGaussianPly(capturedWorld, outputDirectory / "object.ply");
    writeParticlesCsv(dataset.particles, outputDirectory / "particles.csv");
    writeObservationsCsv(dataset.observations, outputDirectory / "observations.csv");
    writeTruthCsv(outputDirectory / "truth.csv");

    std::cout << "Generated deterministic captured-deformable calibration example\n"
              << "  output: " << outputDirectory.string() << '\n'
              << "  appearance_gaussians: " << capturedWorld.size() << '\n'
              << "  physical_particles: " << dataset.particles.size() << '\n'
              << "  observations: " << dataset.observations.size() << '\n'
              << "  truth_young_modulus: 15000\n"
              << "  truth_poisson_ratio: 0.30\n"
              << "  truth_dt: 0.0001\n"
              << "  truth_grid_cell_size: 0.08\n";
    return 0;
}

} // namespace vulkax::cli
