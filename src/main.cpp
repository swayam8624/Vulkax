#include "vulkax/backend/backend.hpp"
#include "vulkax/backend/probe.hpp"
#include "vulkax/cli/deformable_reference.hpp"
#include "vulkax/compute/conformance.hpp"
#include "vulkax/core/units.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/operators/operator_graph.hpp"
#include "vulkax/planning/solver_plan.hpp"
#include "vulkax/problem/document.hpp"
#include "vulkax/problem/problem_ir.hpp"
#include "vulkax/problem/validation.hpp"
#include "vulkax/render/gaussian.hpp"
#include "vulkax/render/headless.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::optional<vulkax::backend::BackendKind> parseBackend(std::string_view name) {
    using vulkax::backend::BackendKind;
    if (name == "Vulkan") return BackendKind::Vulkan;
    if (name == "Metal") return BackendKind::Metal;
    if (name == "OpenGL") return BackendKind::OpenGL;
    return std::nullopt;
}

const char* fidelityName(vulkax::planning::FidelityTier value) {
    using vulkax::planning::FidelityTier;
    switch (value) {
        case FidelityTier::Preview: return "preview";
        case FidelityTier::Engineering: return "engineering";
        case FidelityTier::Verification: return "verification";
    }
    return "unknown";
}

const char* solverName(vulkax::planning::SolverKind value) {
    using vulkax::planning::SolverKind;
    switch (value) {
        case SolverKind::ExplicitField: return "explicit-field";
        case SolverKind::ProjectionFluid: return "projection-fluid";
        case SolverKind::DEM: return "DEM";
        case SolverKind::LinearFEM: return "linear-FEM";
        case SolverKind::NonlinearFEM: return "nonlinear-FEM";
        case SolverKind::RayIntegrator: return "ray-integrator";
    }
    return "unknown";
}

void printBackendProbe() {
    using namespace vulkax::backend;
    const auto candidates = probeAvailableBackends();
    std::cout << "Detected GPU backends:\n";
    if (candidates.empty()) {
        std::cout << "  (none probed)\n";
        return;
    }
    for (const auto& candidate : candidates) {
        std::cout << "  " << toString(candidate.kind) << " | " << candidate.deviceName << " | "
                  << candidate.deviceMemoryBytes / (1024ull * 1024ull) << " MiB | "
                  << candidate.features.size() << " capabilities\n";
    }
    WorkloadRequirements requirements;
    requirements.requiredFeatures = {Feature::StorageBuffers, Feature::Atomics};
    const auto selected = selectBackend(candidates, requirements, currentPlatform());
    if (selected.kind) std::cout << "Selected for generic compute: " << toString(*selected.kind) << "\n";
    else std::cout << "No backend satisfies generic compute requirements.\n";
}

struct GaussianBounds {
    vulkax::math::Vec3 minimum;
    vulkax::math::Vec3 maximum;
};

GaussianBounds gaussianBounds(const vulkax::gaussian::GaussianCloud& cloud) {
    if (cloud.empty()) throw std::invalid_argument("Gaussian scene is empty");
    GaussianBounds result{
        {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
         std::numeric_limits<double>::infinity()},
        {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
         -std::numeric_limits<double>::infinity()},
    };
    for (const auto& splat : cloud.splats) {
        result.minimum.x = std::min(result.minimum.x, splat.position.x);
        result.minimum.y = std::min(result.minimum.y, splat.position.y);
        result.minimum.z = std::min(result.minimum.z, splat.position.z);
        result.maximum.x = std::max(result.maximum.x, splat.position.x);
        result.maximum.y = std::max(result.maximum.y, splat.position.y);
        result.maximum.z = std::max(result.maximum.z, splat.position.z);
    }
    return result;
}

int gaussianInfoCommand(int argc, char** argv) {
    if (argc < 3 || std::string_view(argv[1]) != "gaussian-info") return -1;
    const auto cloud = vulkax::gaussian::load3dgsPly(argv[2]);
    const auto bounds = gaussianBounds(cloud);
    double opacitySum = 0.0;
    for (const auto& splat : cloud.splats) opacitySum += splat.opacity();

    std::cout << "3D Gaussian scene\n"
              << "  splats: " << cloud.size() << '\n'
              << "  sh_rest_coefficients_per_splat: " << cloud.shRestCoefficientsPerSplat << '\n'
              << std::setprecision(9)
              << "  bounds_min: " << bounds.minimum.x << ' ' << bounds.minimum.y << ' ' << bounds.minimum.z << '\n'
              << "  bounds_max: " << bounds.maximum.x << ' ' << bounds.maximum.y << ' ' << bounds.maximum.z << '\n'
              << "  mean_opacity: " << opacitySum / static_cast<double>(cloud.size()) << '\n';
    return 0;
}

int gaussianRenderCommand(int argc, char** argv) {
    if (argc < 4 || std::string_view(argv[1]) != "gaussian-render") return -1;
    const auto cloud = vulkax::gaussian::load3dgsPly(argv[2]);
    const auto bounds = gaussianBounds(cloud);

    const auto available = vulkax::render::availableHeadlessRenderBackends();
    if (available.empty()) throw std::runtime_error("no native headless render backend is available");

    std::optional<vulkax::backend::BackendKind> selected;
    if (argc >= 5) {
        selected = parseBackend(argv[4]);
        if (!selected) throw std::invalid_argument("unknown Gaussian render backend");
        if (std::find(available.begin(), available.end(), *selected) == available.end())
            throw std::runtime_error("requested Gaussian render backend is not available in this build");
    } else {
        const auto metal = std::find(available.begin(), available.end(), vulkax::backend::BackendKind::Metal);
        const auto vulkan = std::find(available.begin(), available.end(), vulkax::backend::BackendKind::Vulkan);
        if (vulkax::backend::currentPlatform() == vulkax::backend::PlatformKind::MacOS && metal != available.end())
            selected = *metal;
        else if (vulkan != available.end())
            selected = *vulkan;
        else
            selected = available.front();
    }

    const vulkax::math::Vec3 center = (bounds.minimum + bounds.maximum) * 0.5;
    const vulkax::math::Vec3 extent = bounds.maximum - bounds.minimum;
    const double sceneSpan = std::max({extent.x, extent.y, extent.z, 1.0e-3});

    vulkax::render::GaussianRenderSettings settings;
    settings.image.width = 1280;
    settings.image.height = 720;
    settings.camera.target = center;
    settings.camera.position = {center.x, center.y, center.z + 2.5 * sceneSpan};
    settings.camera.up = {0.0, 1.0, 0.0};
    settings.camera.verticalFovDegrees = 50.0;
    settings.nearPlane = std::max(1.0e-5, sceneSpan * 1.0e-5);

    const auto result = vulkax::render::renderGaussianCloudHeadless(*selected, cloud, settings);
    vulkax::render::writePpm(result.image, argv[3]);
    std::cout << "Gaussian render\n"
              << "  backend: " << vulkax::backend::toString(*selected) << '\n'
              << "  input_splats: " << result.stats.inputSplats << '\n'
              << "  visible_splats: " << result.stats.visibleSplats << '\n'
              << "  culled_behind: " << result.stats.culledBehindCamera << '\n'
              << "  culled_opacity: " << result.stats.culledOpacity << '\n'
              << "  culled_outside: " << result.stats.culledOutsideImage << '\n'
              << "  output: " << argv[3] << '\n';
    return 0;
}

int problemCommand(int argc, char** argv) {
    if (argc < 3) return -1;
    const std::string command = argv[1];
    if (command != "validate" && command != "inspect" && command != "plan") return -1;
    const auto problem = vulkax::problem::loadProblemDocument(argv[2]);
    const auto validation = vulkax::problem::validateProblem(problem);
    if (!validation.ok()) {
        std::cerr << "INVALID " << problem.id << " | " << validation.errorCount() << " error(s)\n";
        for (const auto& issue : validation.issues) std::cerr << "  " << issue.path << ": " << issue.message << '\n';
        return 1;
    }
    if (command == "validate") {
        std::cout << "VALID " << problem.id << " | hash=0x" << std::hex
                  << vulkax::problem::stableProblemHash(problem) << std::dec
                  << " | warnings=" << validation.warningCount() << '\n';
        return 0;
    }
    if (command == "inspect") {
        const vulkax::operators::OperatorGraph graph(problem);
        std::cout << problem.name << "\n  id: " << problem.id << "\n  hash: 0x" << std::hex
                  << vulkax::problem::stableProblemHash(problem) << std::dec
                  << "\n  domains: " << problem.domains.size()
                  << "\n  fields: " << problem.fields.size()
                  << "\n  operators: " << graph.operators().size()
                  << "\n  objectives: " << problem.objectives.size() << '\n';
        return 0;
    }

    const auto candidates = vulkax::backend::probeAvailableBackends();
    vulkax::backend::WorkloadRequirements requirements;
    requirements.requiredFeatures = {vulkax::backend::Feature::StorageBuffers};
    const auto selected = vulkax::backend::selectBackend(candidates, requirements, vulkax::backend::currentPlatform());
    if (!selected.kind) {
        std::cerr << "No runtime backend can execute the requested compute workload.\n";
        return 3;
    }
    const auto it = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return candidate.available && candidate.kind == *selected.kind;
    });
    if (it == candidates.end()) return 3;
    const auto ladder = vulkax::planning::makeFidelityLadder(problem, *it);
    const auto best = vulkax::planning::selectBestPlan(ladder, problem.computeBudget);
    std::cout << "Backend: " << vulkax::backend::toString(*selected.kind) << " | " << it->deviceName << '\n';
    for (const auto& plan : ladder) {
        const bool chosen = plan.fidelity == best.fidelity;
        std::cout << (chosen ? "* " : "  ") << fidelityName(plan.fidelity) << " | " << solverName(plan.solver)
                  << " | resolution=" << plan.characteristicResolution << " | tol=" << plan.relativeTolerance
                  << " | memory~" << plan.estimatedMemoryBytes / (1024ull * 1024ull)
                  << " MiB | time~" << plan.estimatedWallSeconds << "s\n";
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    using namespace vulkax;
    try {
        const int deformableReferenceResult = cli::deformableReferenceCommand(argc, argv);
        if (deformableReferenceResult >= 0) return deformableReferenceResult;
        const int gaussianRenderResult = gaussianRenderCommand(argc, argv);
        if (gaussianRenderResult >= 0) return gaussianRenderResult;
        const int gaussianResult = gaussianInfoCommand(argc, argv);
        if (gaussianResult >= 0) return gaussianResult;
        const int documentResult = problemCommand(argc, argv);
        if (documentResult >= 0) return documentResult;

        std::optional<backend::BackendKind> requiredBackend;
        std::optional<backend::BackendKind> conformanceBackend;
        bool probeOnly = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--probe-backends") probeOnly = true;
            else if ((argument == "--require-backend" || argument == "--conformance") && i + 1 < argc) {
                const auto parsed = parseBackend(argv[++i]);
                if (!parsed) {
                    std::cerr << "Unknown backend name\n";
                    return 2;
                }
                if (argument == "--require-backend") requiredBackend = parsed;
                else conformanceBackend = parsed;
            }
        }

        if (conformanceBackend) {
            const auto result = compute::runConformance(*conformanceBackend);
            std::cout << "Compute conformance " << backend::toString(result.backend) << " | " << result.deviceName
                      << " | N=" << result.elementCount << " | max_abs=" << std::setprecision(8)
                      << result.maxAbsoluteError << " | max_rel=" << result.maxRelativeError
                      << " | " << (result.passed ? "PASS" : "FAIL") << '\n';
            return result.passed ? 0 : 4;
        }

        const auto candidates = backend::probeAvailableBackends();
        if (requiredBackend) {
            const bool found = std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
                return candidate.available && candidate.kind == *requiredBackend;
            });
            if (!found) {
                std::cerr << "Required backend " << backend::toString(*requiredBackend)
                          << " was not discovered at runtime.\n";
                return 3;
            }
        }
        if (probeOnly || requiredBackend) {
            printBackendProbe();
            return 0;
        }

        std::cout << "Vulkax computational physics and rewritable-reality research system\n"
                  << "Commands:\n"
                  << "  vulkax validate <problem.vkx>\n"
                  << "  vulkax inspect <problem.vkx>\n"
                  << "  vulkax plan <problem.vkx>\n"
                  << "  vulkax gaussian-info <point_cloud.ply>\n"
                  << "  vulkax gaussian-render <point_cloud.ply> <output.ppm> [Vulkan|Metal]\n"
                  << "  vulkax deformable-reference <output.csv>\n"
                  << "  vulkax deformable-nonlinear <output.csv>\n"
                  << "  vulkax --probe-backends\n"
                  << "  vulkax --conformance Vulkan|Metal\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vulkax error: " << error.what() << '\n';
        return 1;
    }
}
