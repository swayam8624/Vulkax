#include "vulkax/core/units.hpp"
#include "vulkax/execution/experiment.hpp"

#include <cassert>
#include <string>

namespace {

vulkax::backend::BackendCapabilities fakeBackend() {
    vulkax::backend::BackendCapabilities capability;
    capability.kind = vulkax::backend::BackendKind::Vulkan;
    capability.available = true;
    capability.dedicatedGpu = true;
    capability.driverQuality = 0.9;
    capability.deviceMemoryBytes = 8ull << 30u;
    capability.deviceName = "Reference GPU";
    capability.features = {vulkax::backend::Feature::Compute,
                           vulkax::backend::Feature::StorageBuffers,
                           vulkax::backend::Feature::Atomics};
    return capability;
}

} // namespace

int main() {
    using namespace vulkax;
    problem::ProblemIR problem;
    problem.id = "transport-experiment";
    problem.name = "periodic diffusion experiment";
    problem.domains.push_back({"domain", problem::DomainKind::Volume, 3});
    problem.fields.push_back({"temperature", "domain", problem::FieldRank::Scalar, 1, units::temperature});
    problem.operators.push_back({"diffusion", "Diffusion", "temperature", {"temperature"},
                                 "alpha*laplacian(temperature)", "transport"});
    problem.objectives.push_back({"integral", "Integral", "integral(temperature)",
                                  problem::ObjectiveDirection::Observe});
    problem.accuracyTargets.push_back({"integral", 1.0e-9, std::nullopt});
    problem.computeBudget.wallSeconds = 100.0;

    numerics::ScalarGrid3D state(10, 10, 10, {0.1, 0.1, 0.1});
    state.at(5, 5, 5) = 10.0;
    const double dtLimit = solvers::explicitDiffusionStabilityLimit(state, 0.1);
    execution::ExperimentRequest request{
        problem,
        execution::DiffusionExperiment{state, {0.1, dtLimit * 0.5, 10,
                                               numerics::BoundaryMode::Periodic, 0.95}},
        {fakeBackend()},
        true};
    const auto result = execution::executeExperiment(std::move(request));
    assert(result.certificate.state == verify::TrustState::Verified);
    assert(result.certificate.problemHash == problem::stableProblemHash(problem));
    assert(result.certificate.solverHash != 0);
    assert(!result.metrics.empty());
    const std::string json = result.certificate.toJson();
    assert(json.find("verified") != std::string::npos);
    return 0;
}
