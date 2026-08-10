#include "vulkax/core/units.hpp"
#include "vulkax/operators/operator_graph.hpp"
#include "vulkax/problem/problem_ir.hpp"
#include "vulkax/problem/validation.hpp"

#include <iomanip>
#include <iostream>

int main() {
    using namespace vulkax;

    problem::ProblemIR problem;
    problem.id = "bootstrap-transport";
    problem.name = "Bootstrap transport problem";
    problem.domains.push_back({"domain", problem::DomainKind::Volume, 3});
    problem.fields.push_back(
        {"state", "domain", problem::FieldRank::Scalar, 1, units::temperature});
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
    std::cout << "Vulkax Next bootstrap\n";
    std::cout << "Problem hash: 0x" << std::hex << problem::stableProblemHash(problem) << std::dec << '\n';
    std::cout << "Residual operators: " << graph.operators().size() << '\n';
    std::cout << "Validation: clean\n";
    std::cout << "No demo-specific visualizer mode exists in the core.\n";
    return 0;
}
