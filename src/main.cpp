#include "vulkax/backend/backend.hpp"
#include "vulkax/backend/probe.hpp"
#include "vulkax/compute/conformance.hpp"
#include "vulkax/core/units.hpp"
#include "vulkax/operators/operator_graph.hpp"
#include "vulkax/problem/problem_ir.hpp"
#include "vulkax/problem/validation.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <optional>
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
    if (selected.kind) {
        std::cout << "Selected for generic compute: " << toString(*selected.kind) << "\n";
    } else {
        std::cout << "No backend satisfies generic compute requirements.\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    using namespace vulkax;

    std::optional<backend::BackendKind> requiredBackend;
    std::optional<backend::BackendKind> conformanceBackend;
    bool probeOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--probe-backends") {
            probeOnly = true;
        } else if ((argument == "--require-backend" || argument == "--conformance") && i + 1 < argc) {
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
        try {
            const auto result = compute::runConformance(*conformanceBackend);
            std::cout << "Compute conformance " << backend::toString(result.backend) << " | "
                      << result.deviceName << " | N=" << result.elementCount
                      << " | max_abs=" << std::setprecision(8) << result.maxAbsoluteError
                      << " | max_rel=" << result.maxRelativeError
                      << " | " << (result.passed ? "PASS" : "FAIL") << '\n';
            return result.passed ? 0 : 4;
        } catch (const std::exception& error) {
            std::cerr << "Compute conformance failed: " << error.what() << '\n';
            return 4;
        }
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

    problem::ProblemIR problem;
    problem.id = "bootstrap-transport";
    problem.name = "Bootstrap transport problem";
    problem.domains.push_back({"domain", problem::DomainKind::Volume, 3});
    problem.fields.push_back({"state", "domain", problem::FieldRank::Scalar, 1, units::temperature});
    problem.operators.push_back({"diffusion", "Diffusion", "state", {"state"},
                                 "d(state)/dt - alpha * laplacian(state)", "transport"});
    problem.boundaryConditions.push_back(
        {"wall-temperature", "domain", "state", "dirichlet", {300.0}, units::temperature});
    problem.objectives.push_back(
        {"mean-state", "Mean state", "mean(state)", problem::ObjectiveDirection::Observe});
    problem.accuracyTargets.push_back({"mean-state", 0.01, std::nullopt});
    problem.computeBudget.wallSeconds = 5.0;

    const auto validation = problem::validateProblem(problem);
    if (!validation.ok()) {
        std::cerr << "Problem validation failed with " << validation.errorCount() << " error(s).\n";
        for (const auto& issue : validation.issues) {
            std::cerr << " - " << issue.path << ": " << issue.message << '\n';
        }
        return 1;
    }

    const operators::OperatorGraph graph(problem);
    std::cout << "Vulkax problem-driven core\n";
    std::cout << "Problem hash: 0x" << std::hex << problem::stableProblemHash(problem) << std::dec << '\n';
    std::cout << "Residual operators: " << graph.operators().size() << '\n';
    std::cout << "Validation: clean\n";
    printBackendProbe();
    std::cout << "No demo-specific visualizer mode exists in the core.\n";
    return 0;
}
