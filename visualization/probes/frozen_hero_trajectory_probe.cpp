#include "vulkax/research/nonlinear_deformable_world.hpp"
#include "vulkax/research/prescribed_mpm.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using vulkax::gaussian::GaussianCloud;
using vulkax::math::Vec3;
using vulkax::research::NonlinearDeformableWorldSettings;
using vulkax::research::PrescribedParticleTarget;
using vulkax::solvers::Matrix3;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmMaterial;
using vulkax::solvers::MpmParticle;
using vulkax::solvers::MpmTransferScheme;

constexpr int kTruthId = 5;
constexpr double kTruthE = 18125.0;
constexpr double kTruthNu = 0.22;
constexpr double kTruthDt = 2.5e-5;
constexpr double kCandidateDt = 1.0e-4;
constexpr double kHorizon = 0.0032;
constexpr double kForceHorizon = 0.006;
constexpr double kForcePerTopParticle = 40.0;
constexpr double kCalibrationAmplitude = 0.025;
constexpr double kHeldoutAmplitude = 0.040;
constexpr double kTargetShear = 0.087;
constexpr double kTargetAxial = 0.068;
constexpr double kNoise = 2.0e-5;

// Exact row already frozen in the orthogonal-force workflow artifact. These are
// used only as a replay guard: the exporter refuses to emit animation data if it
// does not reproduce the selected row.
constexpr double kExpectedBaselineHeldout = 1.0517712035032275e-05;
constexpr double kExpectedRepairHeldout = 8.947388417773706e-06;
constexpr double kExpectedBaselineTarget = 1.0987448733584696e-05;
constexpr double kExpectedRepairTarget = 1.1979910421188088e-05;

struct Fit {
    double E{};
    double nu{};
    double objective{std::numeric_limits<double>::infinity()};
};

struct Candidate {
    std::string name;
    MpmTransferScheme scheme{MpmTransferScheme::APIC};
    Fit fit;
    double heldoutError{};
    double targetError{};
};

struct Direction {
    std::string_view name;
    Vec3 force;
};

struct Compliance {
    Vec3 top{};
    Vec3 interior{};
};

std::vector<MpmParticle> body() {
    std::vector<MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double h = 0.12;
    constexpr double volume = h * h * h;
    constexpr double density = 1000.0;
    for (int z = 0; z < 4; ++z) {
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                MpmParticle particle;
                particle.id = id++;
                particle.restPosition = {
                    (static_cast<double>(x) - 1.5) * h,
                    (static_cast<double>(y) - 1.5) * h,
                    (static_cast<double>(z) - 1.5) * h};
                particle.position = particle.restPosition;
                particle.restVolume = volume;
                particle.mass = density * volume;
                particles.push_back(particle);
            }
        }
    }
    return particles;
}

vulkax::gaussian::GaussianSplat splat(Vec3 position) {
    vulkax::gaussian::GaussianSplat value;
    value.position = position;
    value.logScale = {std::log(0.06), std::log(0.045), std::log(0.035)};
    value.rotation = {1.0, 0.0, 0.0, 0.0};
    value.opacityLogit = 4.0;
    return value;
}

GaussianCloud world() {
    GaussianCloud result;
    result.splats.push_back(splat({-0.08, 0.04, 0.02}));
    result.splats.push_back(splat({0.09, -0.06, 0.03}));
    result.splats.push_back(splat({0.04, 0.10, -0.07}));
    result.splats.push_back(splat({-0.05, -0.08, -0.06}));
    return result;
}

MpmGridSettings grid() {
    MpmGridSettings result;
    result.origin = {-0.8, -0.8, -0.8};
    result.nx = 22;
    result.ny = 22;
    result.nz = 22;
    result.cellSize = 0.08;
    result.boundaryCells = 0;
    return result;
}

Matrix3 deformation(double shear, double axial) {
    return {1.0 + axial, shear, 0.0,
            0.0, 1.0, 0.0,
            0.0, 0.0, 1.0};
}

std::vector<double> simulate(
    double E,
    double nu,
    double shear,
    double axial,
    MpmTransferScheme scheme,
    double dt) {
    if (std::abs(shear) < 1e-15 && std::abs(axial) < 1e-15) {
        const auto particles = body();
        constexpr std::array<std::size_t, 4> markers{5U, 18U, 45U, 58U};
        std::vector<double> out;
        out.reserve(12);
        for (const auto marker : markers) {
            const auto& position = particles.at(marker).restPosition;
            out.push_back(position.x);
            out.push_back(position.y);
            out.push_back(position.z);
        }
        return out;
    }

    NonlinearDeformableWorldSettings settings;
    settings.steps = static_cast<std::size_t>(std::llround(kHorizon / dt));
    settings.dt = dt;
    settings.material = {1000.0, E, nu};
    settings.initialDeformation = deformation(shear, axial);
    settings.couplingNeighborCount = 20;
    settings.transferScheme = scheme;

    std::vector<MpmParticle> finalParticles;
    (void)vulkax::research::runNonlinearDeformableWorld(
        world(),
        {0, 1, 2, 3},
        body(),
        grid(),
        settings,
        {},
        [&](const auto& frame, const GaussianCloud&, const std::vector<MpmParticle>& particles) {
            if (frame.step == settings.steps) finalParticles = particles;
        });

    if (finalParticles.size() != 64U)
        throw std::runtime_error("visual trajectory replay missing final nonlinear state");

    constexpr std::array<std::size_t, 4> markers{5U, 18U, 45U, 58U};
    std::vector<double> out;
    out.reserve(12);
    for (const auto marker : markers) {
        const auto& position = finalParticles.at(marker).position;
        out.push_back(position.x);
        out.push_back(position.y);
        out.push_back(position.z);
    }
    return out;
}

std::vector<double> addNoise(std::vector<double> values, int salt) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        const double q =
            0.719 * static_cast<double>(index + 1U) +
            1.173 * static_cast<double>(salt + 1);
        values[index] +=
            kNoise * (0.59 * std::sin(q) + 0.41 * std::cos(1.43 * q));
    }
    return values;
}

double rms(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    if (lhs.size() != rhs.size() || lhs.empty())
        throw std::invalid_argument("visual replay RMS size mismatch");
    long double sum = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const long double d =
            static_cast<long double>(lhs[index]) -
            static_cast<long double>(rhs[index]);
        sum += d * d;
    }
    return std::sqrt(static_cast<double>(sum / static_cast<long double>(lhs.size())));
}

double datasetRms(
    const std::vector<std::vector<double>>& lhs,
    const std::vector<std::vector<double>>& rhs) {
    if (lhs.size() != rhs.size() || lhs.empty())
        throw std::invalid_argument("visual replay dataset mismatch");
    long double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t row = 0; row < lhs.size(); ++row) {
        if (lhs[row].size() != rhs[row].size())
            throw std::invalid_argument("visual replay response width mismatch");
        for (std::size_t column = 0; column < lhs[row].size(); ++column) {
            const long double d =
                static_cast<long double>(lhs[row][column]) -
                static_cast<long double>(rhs[row][column]);
            sum += d * d;
            ++count;
        }
    }
    return std::sqrt(static_cast<double>(sum / static_cast<long double>(count)));
}

std::vector<std::pair<double, double>> calibrationPoints() {
    return {
        { kCalibrationAmplitude, 0.0},
        {-kCalibrationAmplitude, 0.0},
        {0.0,  kCalibrationAmplitude},
        {0.0, -kCalibrationAmplitude}
    };
}

std::vector<std::pair<double, double>> heldoutPoints() {
    return {
        { kHeldoutAmplitude,  kHeldoutAmplitude},
        {-kHeldoutAmplitude,  kHeldoutAmplitude},
        { kHeldoutAmplitude, -kHeldoutAmplitude}
    };
}

std::vector<std::vector<double>> simulatePairs(
    double E,
    double nu,
    MpmTransferScheme scheme,
    double dt,
    const std::vector<std::pair<double, double>>& points) {
    std::vector<std::vector<double>> result;
    result.reserve(points.size());
    for (const auto [shear, axial] : points)
        result.push_back(simulate(E, nu, shear, axial, scheme, dt));
    return result;
}

Fit fitVariant(
    MpmTransferScheme scheme,
    const std::vector<std::vector<double>>& calibrationTruth) {
    constexpr std::array<double, 3> Es{12000.0, 15000.0, 18000.0};
    constexpr std::array<double, 3> Nus{0.20, 0.30, 0.40};
    Fit best;
    for (const double E : Es) {
        for (const double nu : Nus) {
            const auto predicted =
                simulatePairs(E, nu, scheme, kCandidateDt, calibrationPoints());
            const double objective = datasetRms(predicted, calibrationTruth);
            if (objective < best.objective) best = {E, nu, objective};
        }
    }
    return best;
}

Candidate replayCandidate(
    std::string name,
    MpmTransferScheme scheme,
    const std::vector<std::vector<double>>& calibrationTruth,
    const std::vector<std::vector<double>>& heldoutTruth,
    const std::vector<double>& targetTruth) {
    Candidate result;
    result.name = std::move(name);
    result.scheme = scheme;
    result.fit = fitVariant(scheme, calibrationTruth);
    result.heldoutError = datasetRms(
        simulatePairs(
            result.fit.E,
            result.fit.nu,
            scheme,
            kCandidateDt,
            heldoutPoints()),
        heldoutTruth);
    result.targetError = rms(
        simulate(
            result.fit.E,
            result.fit.nu,
            kTargetShear,
            kTargetAxial,
            scheme,
            kCandidateDt),
        targetTruth);
    return result;
}

bool closeEnough(double actual, double expected) {
    const double scale = std::max(std::abs(expected), 1e-12);
    return std::abs(actual - expected) <= 2e-7 * scale + 5e-13;
}

void requireReplay(double actual, double expected, std::string_view name) {
    if (!closeEnough(actual, expected)) {
        throw std::runtime_error(
            std::string("frozen hero replay mismatch for ") +
            std::string(name) + ": actual=" + std::to_string(actual) +
            " expected=" + std::to_string(expected));
    }
}

std::array<Direction, 4> directions() {
    return {{
        {"px", { kForcePerTopParticle, 0.0, 0.0}},
        {"nx", {-kForcePerTopParticle, 0.0, 0.0}},
        {"py", {0.0,  kForcePerTopParticle, 0.0}},
        {"pz", {0.0, 0.0,  kForcePerTopParticle}}
    }};
}

Compliance compliance(
    const std::vector<MpmParticle>& initial,
    const std::vector<MpmParticle>& particles) {
    constexpr double bottomY = -0.18;
    constexpr double topY = 0.18;
    Compliance result;
    std::size_t topCount = 0;
    std::size_t interiorCount = 0;

    for (std::size_t index = 0; index < particles.size(); ++index) {
        const auto displacement =
            particles[index].position - initial[index].restPosition;
        if (std::abs(initial[index].restPosition.y - topY) < 1e-12) {
            result.top += displacement;
            ++topCount;
        } else if (
            initial[index].restPosition.y > bottomY + 1e-12 &&
            initial[index].restPosition.y < topY - 1e-12) {
            result.interior += displacement;
            ++interiorCount;
        }
    }

    if (topCount == 0U || interiorCount == 0U)
        throw std::runtime_error("visual replay has empty force observation region");

    result.top = result.top / static_cast<double>(topCount);
    result.interior = result.interior / static_cast<double>(interiorCount);
    return result;
}

std::vector<double> flatten(const Compliance& value) {
    return {
        value.top.x, value.top.y, value.top.z,
        value.interior.x, value.interior.y, value.interior.z
    };
}

void writeParticleRows(
    std::ofstream& out,
    std::string_view directionName,
    std::string_view modelName,
    std::size_t frame,
    double time,
    const std::vector<MpmParticle>& initial,
    const std::vector<MpmParticle>& particles) {
    constexpr double bottomY = -0.18;
    constexpr double topY = 0.18;
    for (std::size_t index = 0; index < particles.size(); ++index) {
        const auto& p = particles[index];
        const auto& r = initial[index].restPosition;
        out << directionName << ','
            << modelName << ','
            << frame << ','
            << time << ','
            << p.id << ','
            << r.x << ',' << r.y << ',' << r.z << ','
            << p.position.x << ',' << p.position.y << ',' << p.position.z << ','
            << p.velocity.x << ',' << p.velocity.y << ',' << p.velocity.z << ','
            << vulkax::solvers::deformationDeterminant(p) << ','
            << (std::abs(r.y - bottomY) < 1e-12 ? 1 : 0) << ','
            << (std::abs(r.y - topY) < 1e-12 ? 1 : 0)
            << '\n';
    }
}

Compliance exportForceTrajectory(
    std::ofstream& out,
    std::string_view directionName,
    std::string_view modelName,
    double E,
    double nu,
    MpmTransferScheme scheme,
    double dt,
    Vec3 force) {
    auto particles = body();
    const auto initial = particles;
    const auto gridSettings = grid();
    const MpmMaterial material{1000.0, E, nu};
    constexpr double bottomY = -0.18;
    constexpr double topY = 0.18;

    std::vector<PrescribedParticleTarget> bottom;
    for (const auto& particle : initial) {
        if (std::abs(particle.restPosition.y - bottomY) < 1e-12) {
            bottom.push_back({
                particle.id,
                particle.restPosition,
                {0.0, 0.0, 0.0},
                true});
        }
    }
    if (bottom.empty())
        throw std::runtime_error("visual replay missing fixed bottom layer");

    const std::size_t steps =
        static_cast<std::size_t>(std::llround(kForceHorizon / dt));
    const std::size_t stride =
        static_cast<std::size_t>(std::llround(kCandidateDt / dt));
    if (steps == 0U || stride == 0U || steps % stride != 0U)
        throw std::runtime_error("visual replay cannot synchronize trajectory frames");

    writeParticleRows(
        out, directionName, modelName, 0U, 0.0, initial, particles);

    for (std::size_t step = 1; step <= steps; ++step) {
        for (std::size_t index = 0; index < particles.size(); ++index) {
            particles[index].externalForce = {0.0, 0.0, 0.0};
            if (std::abs(initial[index].restPosition.y - topY) < 1e-12)
                particles[index].externalForce = force;
        }

        (void)vulkax::research::stepMpmWithPrescribedParticles(
            particles,
            gridSettings,
            material,
            dt,
            bottom,
            {0.0, 0.0, 0.0},
            scheme);

        if (step % stride == 0U) {
            writeParticleRows(
                out,
                directionName,
                modelName,
                step / stride,
                static_cast<double>(step) * dt,
                initial,
                particles);
        }
    }

    return compliance(initial, particles);
}

void writeCompliance(
    std::ofstream& out,
    std::string_view direction,
    Vec3 force,
    const Compliance& truth,
    const Compliance& baseline,
    const Compliance& repair) {
    const auto tv = flatten(truth);
    const auto bv = flatten(baseline);
    const auto rv = flatten(repair);
    out << direction << ','
        << force.x << ',' << force.y << ',' << force.z;
    for (const double value : tv) out << ',' << value;
    for (const double value : bv) out << ',' << value;
    for (const double value : rv) out << ',' << value;
    out << ',' << rms(bv, tv)
        << ',' << rms(rv, tv)
        << '\n';
}

void writeManifest(
    const std::filesystem::path& path,
    const Candidate& baseline,
    const Candidate& repair) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot open trajectory manifest");
    out << std::setprecision(17);
    out << "{\n"
        << "  \"schema\": \"vulkax.visualization.solver_trajectory\",\n"
        << "  \"version\": 1,\n"
        << "  \"evidence_role\": \"presentation_replay_of_frozen_case\",\n"
        << "  \"state_class\": \"raw_solver_state_before_synthetic_observation_noise\",\n"
        << "  \"hero_truth_id\": " << kTruthId << ",\n"
        << "  \"truth\": {\"E_Pa\": " << kTruthE
        << ", \"nu\": " << kTruthNu
        << ", \"scheme\": \"APIC\", \"dt_s\": " << kTruthDt << "},\n"
        << "  \"baseline\": {\"name\": \"apic\", \"fit_E_Pa\": "
        << baseline.fit.E << ", \"fit_nu\": " << baseline.fit.nu
        << ", \"scheme\": \"APIC\", \"dt_s\": " << kCandidateDt
        << ", \"heldout_error_m\": " << baseline.heldoutError
        << ", \"target_error_m\": " << baseline.targetError << "},\n"
        << "  \"repair\": {\"name\": \"pic\", \"fit_E_Pa\": "
        << repair.fit.E << ", \"fit_nu\": " << repair.fit.nu
        << ", \"scheme\": \"PIC\", \"dt_s\": " << kCandidateDt
        << ", \"heldout_error_m\": " << repair.heldoutError
        << ", \"target_error_m\": " << repair.targetError << "},\n"
        << "  \"force_per_top_particle_N\": " << kForcePerTopParticle << ",\n"
        << "  \"force_horizon_s\": " << kForceHorizon << ",\n"
        << "  \"export_frame_dt_s\": " << kCandidateDt << ",\n"
        << "  \"frames_per_direction\": "
        << (static_cast<std::size_t>(std::llround(kForceHorizon / kCandidateDt)) + 1U)
        << ",\n"
        << "  \"directions\": [\"px\", \"nx\", \"py\", \"pz\"],\n"
        << "  \"replay_guard\": \"matched_frozen_hero_row\"\n"
        << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path outDir =
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::path("build/visualization-trajectory");
        std::filesystem::create_directories(outDir);

        auto calibrationTruth = simulatePairs(
            kTruthE,
            kTruthNu,
            MpmTransferScheme::APIC,
            kTruthDt,
            calibrationPoints());
        for (std::size_t index = 0; index < calibrationTruth.size(); ++index) {
            calibrationTruth[index] = addNoise(
                std::move(calibrationTruth[index]),
                kTruthId * 127 + static_cast<int>(index));
        }

        auto heldoutTruth = simulatePairs(
            kTruthE,
            kTruthNu,
            MpmTransferScheme::APIC,
            kTruthDt,
            heldoutPoints());
        for (std::size_t index = 0; index < heldoutTruth.size(); ++index) {
            heldoutTruth[index] = addNoise(
                std::move(heldoutTruth[index]),
                kTruthId * 211 + static_cast<int>(index));
        }

        const auto targetTruth = addNoise(
            simulate(
                kTruthE,
                kTruthNu,
                kTargetShear,
                kTargetAxial,
                MpmTransferScheme::APIC,
                kTruthDt),
            kTruthId * 719);

        const Candidate baseline = replayCandidate(
            "apic",
            MpmTransferScheme::APIC,
            calibrationTruth,
            heldoutTruth,
            targetTruth);
        const Candidate repair = replayCandidate(
            "pic",
            MpmTransferScheme::PIC,
            calibrationTruth,
            heldoutTruth,
            targetTruth);

        requireReplay(
            baseline.heldoutError,
            kExpectedBaselineHeldout,
            "baseline heldout error");
        requireReplay(
            repair.heldoutError,
            kExpectedRepairHeldout,
            "repair heldout error");
        requireReplay(
            baseline.targetError,
            kExpectedBaselineTarget,
            "baseline target error");
        requireReplay(
            repair.targetError,
            kExpectedRepairTarget,
            "repair target error");

        if (!(repair.heldoutError < baseline.heldoutError))
            throw std::runtime_error("hero replay no longer proposes PIC repair");
        if (!(repair.targetError > baseline.targetError))
            throw std::runtime_error("hero replay no longer labels repair deceptive");

        std::ofstream trajectory(outDir / "particle_trajectories.csv");
        if (!trajectory)
            throw std::runtime_error("cannot open particle trajectory output");
        trajectory << std::setprecision(17);
        trajectory
            << "direction,model,frame,time_s,particle_id,"
               "rest_x,rest_y,rest_z,x,y,z,vx,vy,vz,J,is_bottom,is_top\n";

        std::ofstream summary(outDir / "direction_summary.csv");
        if (!summary)
            throw std::runtime_error("cannot open force direction summary");
        summary << std::setprecision(17);
        summary
            << "direction,force_x_N,force_y_N,force_z_N,"
               "truth_top_dx,truth_top_dy,truth_top_dz,"
               "truth_interior_dx,truth_interior_dy,truth_interior_dz,"
               "baseline_top_dx,baseline_top_dy,baseline_top_dz,"
               "baseline_interior_dx,baseline_interior_dy,baseline_interior_dz,"
               "repair_top_dx,repair_top_dy,repair_top_dz,"
               "repair_interior_dx,repair_interior_dy,repair_interior_dz,"
               "baseline_raw_rms_to_truth_m,repair_raw_rms_to_truth_m\n";

        for (const auto& direction : directions()) {
            const auto truth = exportForceTrajectory(
                trajectory,
                direction.name,
                "truth",
                kTruthE,
                kTruthNu,
                MpmTransferScheme::APIC,
                kTruthDt,
                direction.force);
            const auto baselineResponse = exportForceTrajectory(
                trajectory,
                direction.name,
                "baseline_apic",
                baseline.fit.E,
                baseline.fit.nu,
                baseline.scheme,
                kCandidateDt,
                direction.force);
            const auto repairResponse = exportForceTrajectory(
                trajectory,
                direction.name,
                "repair_pic",
                repair.fit.E,
                repair.fit.nu,
                repair.scheme,
                kCandidateDt,
                direction.force);

            writeCompliance(
                summary,
                direction.name,
                direction.force,
                truth,
                baselineResponse,
                repairResponse);
        }

        writeManifest(outDir / "trajectory_manifest.json", baseline, repair);

        std::cout << std::setprecision(17)
                  << "VISUAL_TRAJECTORY_REPLAY PASS\n"
                  << "truth_id " << kTruthId << '\n'
                  << "baseline_fit " << baseline.fit.E << ' ' << baseline.fit.nu << '\n'
                  << "repair_fit " << repair.fit.E << ' ' << repair.fit.nu << '\n'
                  << "baseline_heldout " << baseline.heldoutError << '\n'
                  << "repair_heldout " << repair.heldoutError << '\n'
                  << "baseline_target " << baseline.targetError << '\n'
                  << "repair_target " << repair.targetError << '\n'
                  << "output " << outDir.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "VISUAL_TRAJECTORY_REPLAY FAIL: " << error.what() << '\n';
        return 1;
    }
}
