#include "vulkax/backend/backend.hpp"
#include "vulkax/core/units.hpp"
#include "vulkax/planning/solver_plan.hpp"
#include "vulkax/problem/problem_ir.hpp"
#include "vulkax/solvers/incompressible2d.hpp"

#include <cassert>
#include <cmath>

int main() {
    using namespace vulkax;

    problem::ProblemIR problem;
    problem.id = "flow";
    problem.name = "incompressible test";
    problem.domains.push_back({"air", problem::DomainKind::Volume, 3});
    problem.fields.push_back({"velocity", "air", problem::FieldRank::Vector, 3, units::velocity});
    problem.operators.push_back({"momentum", "Momentum", "velocity", {"velocity"}, "navier-stokes", "fluid"});
    problem.computeBudget.wallSeconds = 10.0;
    backend::BackendCapabilities gpu;
    gpu.available = true;
    gpu.kind = backend::BackendKind::Vulkan;
    gpu.dedicatedGpu = true;
    gpu.deviceMemoryBytes = 12ull << 30u;
    gpu.features = {backend::Feature::Compute, backend::Feature::StorageBuffers};
    const auto ladder = planning::makeFidelityLadder(problem, gpu);
    assert(ladder.size() == 3);
    assert(ladder.front().solver == planning::SolverKind::ProjectionFluid);
    assert(ladder[2].characteristicResolution > ladder[0].characteristicResolution);
    const auto selected = planning::selectBestPlan(ladder, problem.computeBudget);
    assert(selected.fidelity != planning::FidelityTier::Verification);

    solvers::FlowGrid2D flow(48, 32, 1.0 / 47.0, 1.0 / 31.0);
    for (std::size_t y = 1; y + 1 < flow.ny(); ++y) {
        for (std::size_t x = 1; x + 1 < flow.nx(); ++x) {
            const double px = static_cast<double>(x) * flow.dx();
            const double py = static_cast<double>(y) * flow.dy();
            flow.u(x, y) = 0.4 * std::sin(5.0 * px) + 0.15 * py;
            flow.v(x, y) = 0.3 * std::cos(4.0 * py) - 0.10 * px;
            if (x > 20 && x < 25 && y > 12 && y < 19) flow.solid(x, y) = 1;
        }
    }
    const auto diagnostics = solvers::projectIncompressible(flow, {0.01, 1.225, 0.0, 400});
    assert(diagnostics.divergenceL2Before > 1.0e-4);
    assert(diagnostics.divergenceL2After < diagnostics.divergenceL2Before * 0.75);
    assert(std::isfinite(diagnostics.maxSpeed));
    return 0;
}
