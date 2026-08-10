from pathlib import Path


def replace(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise RuntimeError(f"patch anchor not found in {path}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))

# ProblemIR: typed operating parameters are first-class and hashed.
replace(
    "include/vulkax/problem/problem_ir.hpp",
    "struct AccuracyTarget { std::string observableId; double relativeTolerance{1.0e-3}; std::optional<double> absoluteTolerance; };\nstruct ComputeBudget",
    "struct AccuracyTarget { std::string observableId; double relativeTolerance{1.0e-3}; std::optional<double> absoluteTolerance; };\nstruct NamedParameter { std::string id; units::Quantity value; };\nstruct ComputeBudget",
)
replace(
    "include/vulkax/problem/problem_ir.hpp",
    "    ComputeBudget computeBudget;\n};\n\n[[nodiscard]] std::uint64_t stableProblemHash(const ProblemIR& problem);",
    "    ComputeBudget computeBudget;\n    std::vector<NamedParameter> parameters;\n};\n\n[[nodiscard]] std::uint64_t stableProblemHash(const ProblemIR& problem);\n[[nodiscard]] const NamedParameter* findParameter(const ProblemIR& problem, const std::string& id) noexcept;\n[[nodiscard]] double requireParameterSI(const ProblemIR& problem, const std::string& id,\n                                        units::Dimension expectedDimension);",
)
replace(
    "src/problem/problem_ir.cpp",
    "#include <cstdint>\n#include <string_view>",
    "#include <cstdint>\n#include <stdexcept>\n#include <string_view>",
)
replace(
    "src/problem/problem_ir.cpp",
    "std::uint64_t stableProblemHash(const ProblemIR& problem) {",
    "const NamedParameter* findParameter(const ProblemIR& problem, const std::string& id) noexcept {\n    const auto it = std::find_if(problem.parameters.begin(), problem.parameters.end(),\n                                 [&](const auto& p) { return p.id == id; });\n    return it == problem.parameters.end() ? nullptr : &*it;\n}\n\ndouble requireParameterSI(const ProblemIR& problem, const std::string& id,\n                          units::Dimension expectedDimension) {\n    const auto* parameter = findParameter(problem, id);\n    if (!parameter) throw std::invalid_argument(\"missing problem parameter: \" + id);\n    if (!(parameter->value.dimension == expectedDimension))\n        throw std::invalid_argument(\"problem parameter has wrong physical dimension: \" + id);\n    return parameter->value.valueSI;\n}\n\nstd::uint64_t stableProblemHash(const ProblemIR& problem) {",
)
replace(
    "src/problem/problem_ir.cpp",
    "    if (problem.computeBudget.wallSeconds)",
    "    for (const NamedParameter* parameter : sortedById(problem.parameters)) {\n        hashString(hash, parameter->id);\n        hashPod(hash, std::bit_cast<std::uint64_t>(parameter->value.valueSI));\n        hashDimension(hash, parameter->value.dimension);\n    }\n\n    if (problem.computeBudget.wallSeconds)",
)

# Problem document grammar: parameter \"id\" value <7 SI exponents>.
replace(
    "src/problem/document.cpp",
    "            } else if (command == \"domain\") {",
    "            } else if (command == \"parameter\") {\n                NamedParameter parameter;\n                if (!(stream >> std::quoted(parameter.id) >> parameter.value.valueSI))\n                    throw std::invalid_argument(\"invalid parameter record\");\n                parameter.value.dimension = readDimension(stream);\n                result.parameters.push_back(std::move(parameter));\n            } else if (command == \"domain\") {",
)
replace(
    "src/problem/document.cpp",
    "    stream << \"name \" << std::quoted(problem.name) << '\\n';\n    for (const auto& domain : problem.domains)",
    "    stream << \"name \" << std::quoted(problem.name) << '\\n';\n    for (const auto& parameter : problem.parameters) {\n        stream << \"parameter \" << std::quoted(parameter.id) << ' ' << parameter.value.valueSI;\n        writeDimension(stream, parameter.value.dimension);\n        stream << '\\n';\n    }\n    for (const auto& domain : problem.domains)",
)

# Generic parameter validation.
replace(
    "src/problem/validation.cpp",
    "    validateUniqueIds(problem.objectives, \"objectives\", result);",
    "    validateUniqueIds(problem.objectives, \"objectives\", result);\n    validateUniqueIds(problem.parameters, \"parameters\", result);\n    for (std::size_t i = 0; i < problem.parameters.size(); ++i) {\n        if (!std::isfinite(problem.parameters[i].value.valueSI))\n            result.issues.push_back({ValidationSeverity::Error,\n                                     \"parameters[\" + std::to_string(i) + \"].value\",\n                                     \"parameter must be finite\"});\n    }",
)

Path("include/vulkax/execution/problem_runner.hpp").write_text(r'''#pragma once
#include "vulkax/problem/problem_ir.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace vulkax::execution {
struct ProblemRunOptions { std::string outputDirectory{"vulkax-run"}; std::size_t frameCount{4}; std::uint32_t width{640}; std::uint32_t height{480}; };
struct ProblemRunResult { std::string resultCertificatePath; std::vector<std::string> framePaths; std::string simulationBackend; std::string visualizationBackend; };
[[nodiscard]] ProblemRunResult runProblem(const problem::ProblemIR& problem, const ProblemRunOptions& options = {});
} // namespace vulkax::execution
''')

Path("src/execution/problem_runner.cpp").write_text(r'''#include "vulkax/execution/problem_runner.hpp"
#include "vulkax/backend/backend.hpp"
#include "vulkax/problem/validation.hpp"
#include "vulkax/render/camera.hpp"
#include "vulkax/render/capture.hpp"
#include "vulkax/render/headless.hpp"
#include "vulkax/solvers/rotating_drum.hpp"
#include "vulkax/verify/result_certificate.hpp"
#include "vulkax/visualization/scientific.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <stdexcept>
namespace vulkax::execution {
namespace {
const units::Dimension angularRateDimension = units::makeDimension(0, 0, -1);
const units::Dimension stiffnessDimension = units::makeDimension(0, 1, -2);
const units::Dimension dampingDimension = units::makeDimension(0, 1, -1);
const units::Dimension densityDimension = units::makeDimension(-3, 1, 0);
const units::Quantity* materialProperty(const problem::ProblemIR& problem, const std::string& name) {
    for (const auto& material : problem.materials)
        for (const auto& property : material.properties)
            if (property.name == name) return &property.value;
    return nullptr;
}
double requireMaterialProperty(const problem::ProblemIR& problem, const std::string& name, units::Dimension dimension) {
    const auto* value = materialProperty(problem, name);
    if (!value) throw std::invalid_argument("missing material property: " + name);
    if (!(value->dimension == dimension)) throw std::invalid_argument("material property has wrong dimension: " + name);
    return value->valueSI;
}
backend::BackendKind chooseRenderer() {
    const auto available = render::availableHeadlessRenderBackends();
    if (available.empty()) throw std::runtime_error("no native headless renderer was built");
    const auto has = [&](backend::BackendKind kind) { return std::find(available.begin(), available.end(), kind) != available.end(); };
    if (backend::currentPlatform() == backend::PlatformKind::MacOS && has(backend::BackendKind::Metal)) return backend::BackendKind::Metal;
    if (has(backend::BackendKind::Vulkan)) return backend::BackendKind::Vulkan;
    return available.front();
}
std::vector<solvers::DemParticle> makeMillParticles(const problem::ProblemIR& problem, double drumRadius, double halfLength, double particleRadius) {
    const double requested = problem::requireParameterSI(problem, "particle_count", units::dimensionless);
    const auto count = static_cast<std::size_t>(std::llround(requested));
    if (count == 0 || count > 50000 || std::abs(requested - static_cast<double>(count)) > 1e-9)
        throw std::invalid_argument("particle_count must be an integer in [1, 50000] for the CPU reference runner");
    const double density = requireMaterialProperty(problem, "density", densityDimension);
    const double mass = density * (4.0 / 3.0) * std::numbers::pi * particleRadius * particleRadius * particleRadius;
    const double spacing = particleRadius * 2.15;
    std::vector<solvers::DemParticle> particles;
    particles.reserve(count);
    for (double z = -halfLength + particleRadius; z <= halfLength - particleRadius && particles.size() < count; z += spacing)
        for (double y = -drumRadius + particleRadius; y <= drumRadius - particleRadius && particles.size() < count; y += spacing)
            for (double x = -drumRadius + particleRadius; x <= drumRadius - particleRadius && particles.size() < count; x += spacing) {
                if (std::hypot(x, y) + particleRadius > drumRadius * 0.82) continue;
                particles.push_back({{x, y, z}, {0, 0, 0}, particleRadius, mass});
            }
    if (particles.size() != count) throw std::invalid_argument("requested particle_count does not fit inside the configured drum");
    return particles;
}
ProblemRunResult runRotatingMill(const problem::ProblemIR& problem, const ProblemRunOptions& options) {
    const double radius = problem::requireParameterSI(problem, "drum_radius", units::length);
    const double halfLength = problem::requireParameterSI(problem, "drum_half_length", units::length);
    const double omega = problem::requireParameterSI(problem, "angular_velocity", angularRateDimension);
    const double particleRadius = problem::requireParameterSI(problem, "particle_radius", units::length);
    const double dt = problem::requireParameterSI(problem, "dt", units::time);
    const double duration = problem::requireParameterSI(problem, "duration", units::time);
    const double stiffness = problem::requireParameterSI(problem, "normal_stiffness", stiffnessDimension);
    const double damping = problem::requireParameterSI(problem, "normal_damping", dampingDimension);
    const double friction = requireMaterialProperty(problem, "friction", units::dimensionless);
    const double restitution = problem::requireParameterSI(problem, "wall_restitution", units::dimensionless);
    if (radius <= 0 || halfLength <= 0 || particleRadius <= 0 || particleRadius >= radius || dt <= 0 || duration <= 0 || stiffness <= 0 || damping < 0 || friction < 0 || restitution < 0 || restitution > 1)
        throw std::invalid_argument("invalid rotating-mill operating parameters");
    auto particles = makeMillParticles(problem, radius, halfLength, particleRadius);
    const solvers::RotatingDrum drum{radius, halfLength, omega};
    const solvers::DemConfig config{dt, {0, -9.80665, 0}, stiffness, damping, friction, restitution};
    solvers::DrumDiagnostics aggregate;
    double simulated = 0.0;
    auto advanceTo = [&](double target) {
        target = std::min(target, duration);
        while (simulated + 0.5 * dt < target) {
            const auto d = solvers::advanceRotatingDrum(particles, drum, config, 1);
            aggregate.wallCollisions += d.wallCollisions;
            aggregate.particleContacts += d.particleContacts;
            aggregate.broadphaseCandidates += d.broadphaseCandidates;
            aggregate.wallImpactEnergy += d.wallImpactEnergy;
            aggregate.wallEnergyTransfer += d.wallEnergyTransfer;
            simulated += dt;
        }
    };
    std::filesystem::create_directories(options.outputDirectory);
    ProblemRunResult result;
    result.simulationBackend = "CPU reference / spatial-hash DEM";
    if (!render::availableHeadlessRenderBackends().empty()) {
        const auto renderer = chooseRenderer();
        result.visualizationBackend = std::string(backend::toString(renderer));
        render::CameraTrack track;
        track.setKeyframes({{0.0, {{0, 0, 3.2 * radius}, {0, 0, 0}, {0, 1, 0}, 42, 1.0}},
                            {duration, {{0.35 * radius, 0.18 * radius, 3.0 * radius}, {0, 0, 0}, {0, 1, 0}, 48, 1.0}}});
        const double fps = options.frameCount > 1 ? static_cast<double>(options.frameCount - 1) / duration : 1.0 / duration;
        const render::CaptureSettings captureSettings{options.width, options.height, fps, options.frameCount,
            (std::filesystem::path(options.outputDirectory) / "frames").string(), {0.008F, 0.010F, 0.016F, 1.0F}};
        const auto capture = render::captureParticleSequence(renderer, [&](double time) {
            advanceTo(time);
            double maxSpeed = 1.0;
            for (const auto& p : particles) maxSpeed = std::max(maxSpeed, math::length(p.velocity));
            return visualization::makeParticleInstances(particles, 0.0, maxSpeed, visualization::ColorMap::Inferno);
        }, track, captureSettings);
        result.framePaths = capture.framePaths;
    } else {
        result.visualizationBackend = "none (simulation-only build)";
    }
    advanceTo(duration);
    const auto diagnostics = solvers::measureDem(particles, {{-radius, -radius, -halfLength}, {radius, radius, halfLength}}, config);
    double maxRadialExcess = 0.0, maxAxialExcess = 0.0;
    for (const auto& p : particles) {
        maxRadialExcess = std::max(maxRadialExcess, std::hypot(p.position.x, p.position.y) + p.radius - radius);
        maxAxialExcess = std::max(maxAxialExcess, std::abs(p.position.z) + p.radius - halfLength);
    }
    verify::ResultCertificate certificate;
    certificate.problemHash = problem::stableProblemHash(problem);
    certificate.solverHash = certificate.problemHash ^ 0x44454d5f4452554dULL;
    certificate.backend = result.simulationBackend;
    certificate.device = "host CPU";
    certificate.criteria.push_back({"maximum radial boundary excess", std::max(0.0, maxRadialExcess), 1e-9, verify::CriterionRelation::LessEqual, true});
    certificate.criteria.push_back({"maximum axial boundary excess", std::max(0.0, maxAxialExcess), 1e-9, verify::CriterionRelation::LessEqual, true});
    certificate.criteria.push_back({"maximum particle overlap / radius", diagnostics.maximumOverlap / particleRadius, 0.6, verify::CriterionRelation::LessEqual, true});
    certificate.notes.push_back("visualization_backend=" + result.visualizationBackend);
    certificate.notes.push_back("particle_count=" + std::to_string(particles.size()));
    certificate.notes.push_back("wall_collisions=" + std::to_string(aggregate.wallCollisions));
    certificate.notes.push_back("particle_contacts=" + std::to_string(aggregate.particleContacts));
    certificate.notes.push_back("wall_impact_energy=" + std::to_string(aggregate.wallImpactEnergy));
    certificate.notes.push_back("wall_energy_transfer=" + std::to_string(aggregate.wallEnergyTransfer));
    certificate.updateTrustState(false);
    result.resultCertificatePath = (std::filesystem::path(options.outputDirectory) / "result.json").string();
    std::ofstream output(result.resultCertificatePath, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("failed to create result certificate");
    output << certificate.toJson();
    return result;
}
} // namespace
ProblemRunResult runProblem(const problem::ProblemIR& problem, const ProblemRunOptions& options) {
    const auto validation = problem::validateProblem(problem);
    if (!validation.ok()) throw std::invalid_argument("cannot run an invalid problem document");
    if (options.outputDirectory.empty() || options.frameCount == 0 || options.width == 0 || options.height == 0)
        throw std::invalid_argument("invalid problem run output settings");
    const bool dem = std::any_of(problem.operators.begin(), problem.operators.end(), [](const auto& op) { return op.family == "dem"; });
    if (dem) return runRotatingMill(problem, options);
    throw std::runtime_error("this ProblemIR family is not yet connected to the end-to-end runner");
}
} // namespace vulkax::execution
''')

Path("tests/problem_runner_tests.cpp").write_text(r'''#include "vulkax/problem/document.hpp"
#include "vulkax/problem/problem_ir.hpp"
#include <cassert>
#include <cmath>
int main() {
    using namespace vulkax;
    const auto parsed = problem::parseProblemDocument("vulkax 1\nid \"p\"\nname \"P\"\nparameter \"drum_radius\" 1 1 0 0 0 0 0 0\nparameter \"angular_velocity\" 4 0 0 -1 0 0 0 0\ndomain \"particles\" particles 3\nfield \"velocity\" \"particles\" vector 3 1 0 -1 0 0 0 0\noperator \"contact\" \"Contact\" \"velocity\" \"dem\" \"contact(velocity)\" \"velocity\"\nobjective \"impact\" \"Impact\" maximize \"impact()\"\n");
    assert(std::abs(problem::requireParameterSI(parsed, "drum_radius", units::length) - 1.0) < 1e-12);
    assert(std::abs(problem::requireParameterSI(parsed, "angular_velocity", units::makeDimension(0, 0, -1)) - 4.0) < 1e-12);
    auto changed = parsed;
    const auto hash = problem::stableProblemHash(parsed);
    changed.parameters[0].value.valueSI = 1.1;
    assert(problem::stableProblemHash(changed) != hash);
    return 0;
}
''')

Path("examples/rotating_mill.vkx").write_text('''vulkax 1
id "rotating-mill"
name "Granular rotating mill"
parameter "drum_radius" 1 1 0 0 0 0 0 0
parameter "drum_half_length" 0.35 1 0 0 0 0 0 0
parameter "angular_velocity" 4 0 0 -1 0 0 0 0
parameter "particle_radius" 0.07 1 0 0 0 0 0 0
parameter "particle_count" 128 0 0 0 0 0 0 0
parameter "dt" 0.0005 0 0 1 0 0 0 0
parameter "duration" 0.03 0 0 1 0 0 0 0
parameter "normal_stiffness" 20000 0 1 -2 0 0 0 0
parameter "normal_damping" 15 0 1 -1 0 0 0 0
parameter "wall_restitution" 0.35 0 0 0 0 0 0 0
domain "particles" particles 3
field "position" "particles" vector 3 1 0 0 0 0 0 0
field "velocity" "particles" vector 3 1 0 -1 0 0 0 0
operator "contact" "Particle contact mechanics" "velocity" "dem" "m*dv/dt - contact(position,velocity) - gravity" "position" "velocity"
material "granular"
property "granular" "density" 2700 -3 1 0 0 0 0 0
property "granular" "friction" 0.45 0 0 0 0 0 0 0
objective "impact-energy" "Useful impact energy" maximize "high_energy_collision_rate()"
accuracy "impact-energy" 0.03 0 0
budget_wall 60
budget_memory 8589934592
''')

# Main CLI: add run command without disturbing probe/conformance behavior.
replace("src/main.cpp", "#include \"vulkax/core/units.hpp\"", "#include \"vulkax/core/units.hpp\"\n#include \"vulkax/execution/problem_runner.hpp\"")
replace("src/main.cpp", "if (command != \"validate\" && command != \"inspect\" && command != \"plan\") return -1;", "if (command != \"validate\" && command != \"inspect\" && command != \"plan\" && command != \"run\") return -1;")
replace("src/main.cpp", "    if (command == \"inspect\") {", '''    if (command == "run") {
        vulkax::execution::ProblemRunOptions options;
        for (int i = 3; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--output" && i + 1 < argc) options.outputDirectory = argv[++i];
            else if (argument == "--frames" && i + 1 < argc) options.frameCount = std::stoull(argv[++i]);
            else if (argument == "--width" && i + 1 < argc) options.width = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            else if (argument == "--height" && i + 1 < argc) options.height = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            else throw std::invalid_argument("unknown or incomplete run option: " + argument);
        }
        const auto run = vulkax::execution::runProblem(problem, options);
        std::cout << "Simulation: " << run.simulationBackend << '\n'
                  << "Visualization: " << run.visualizationBackend << '\n'
                  << "Certificate: " << run.resultCertificatePath << '\n'
                  << "Frames: " << run.framePaths.size() << '\n';
        return 0;
    }
    if (command == "inspect") {''')
replace("src/main.cpp", '                  << "\\n  operators: " << graph.operators().size()\n                  << "\\n  objectives: " << problem.objectives.size() << \'\\n\';', '                  << "\\n  operators: " << graph.operators().size()\n                  << "\\n  parameters: " << problem.parameters.size()\n                  << "\\n  objectives: " << problem.objectives.size() << \'\\n\';')
replace("src/main.cpp", '                  << "  vulkax plan <problem.vkx>\\n"', '                  << "  vulkax plan <problem.vkx>\\n"\n                  << "  vulkax run <problem.vkx> [--output dir --frames N --width W --height H]\\n"')

# Build graph.
replace("CMakeLists.txt", "project(Vulkax VERSION 0.17.0 LANGUAGES CXX)", "project(Vulkax VERSION 0.18.0 LANGUAGES CXX)")
replace("CMakeLists.txt", "    src/execution/experiment.cpp src/experiment/design.cpp", "    src/execution/experiment.cpp src/execution/problem_runner.cpp src/experiment/design.cpp")
replace("CMakeLists.txt", "camera_capture mac3d autodiff)", "camera_capture mac3d autodiff problem_runner)")

# Restore normal CI and add an end-to-end mill gate.
Path(".github/workflows/ci.yml").write_text('''name: Vulkax Next CI

on:
  push:
  pull_request:

jobs:
  linux:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v6
      - name: Install Vulkan runtime and shader compiler
        run: |
          sudo apt-get update
          sudo apt-get install -y libvulkan-dev mesa-vulkan-drivers glslang-tools
      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=ON
      - name: Build
        run: cmake --build build --parallel
      - name: Test
        run: ctest --test-dir build --output-on-failure
      - name: Runtime Vulkan discovery
        run: ./build/vulkax --require-backend Vulkan
      - name: Vulkan compute conformance
        run: ./build/vulkax --conformance Vulkan
      - name: End-to-end granular problem
        run: |
          ./build/vulkax validate examples/rotating_mill.vkx
          ./build/vulkax run examples/rotating_mill.vkx --output build/mill-run --frames 2 --width 160 --height 120
          test -s build/mill-run/result.json
          test -s build/mill-run/frames/frame_000001.ppm
          grep -q '\"trust_state\": \"converging\"' build/mill-run/result.json

  macos:
    runs-on: macos-15
    steps:
      - uses: actions/checkout@v6
      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=ON
      - name: Build
        run: cmake --build build --parallel
      - name: Test
        run: ctest --test-dir build --output-on-failure
      - name: Runtime Metal discovery
        run: ./build/vulkax --require-backend Metal
      - name: Metal compute conformance
        run: ./build/vulkax --conformance Metal
      - name: End-to-end granular problem
        run: |
          ./build/vulkax validate examples/rotating_mill.vkx
          ./build/vulkax run examples/rotating_mill.vkx --output build/mill-run --frames 2 --width 160 --height 120
          test -s build/mill-run/result.json
          test -s build/mill-run/frames/frame_000001.ppm
          grep -q '\"trust_state\": \"converging\"' build/mill-run/result.json

  windows:
    runs-on: windows-2025
    steps:
      - uses: actions/checkout@v6
      - name: Configure
        run: cmake -S . -B build -DVULKAX_BUILD_TESTS=ON
      - name: Build
        run: cmake --build build --config Release --parallel
      - name: Test
        run: ctest --test-dir build -C Release --output-on-failure
''')
