#include "vulkax/backend/backend.hpp"
#include "vulkax/core/units.hpp"
#include "vulkax/operators/operator_graph.hpp"
#include "vulkax/problem/problem_ir.hpp"
#include "vulkax/problem/validation.hpp"
#include "vulkax/verify/result_certificate.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

vulkax::problem::ProblemIR validProblem() {
    using namespace vulkax;
    problem::ProblemIR p;
    p.id = "p";
    p.name = "test";
    p.domains.push_back({"d", problem::DomainKind::Volume, 3});
    p.fields.push_back({"u", "d", problem::FieldRank::Vector, 3, units::velocity});
    p.fields.push_back({"p", "d", problem::FieldRank::Scalar, 1, units::pressure});
    p.operators.push_back({"adv", "Advection", "u", {"u"}, "(u.grad)u", "transport"});
    p.operators.push_back({"pressure", "Pressure gradient", "u", {"p"}, "grad(p)", "constraint"});
    p.boundaryConditions.push_back(
        {"inlet", "d", "u", "dirichlet", {10.0, 0.0, 0.0}, units::velocity});
    p.objectives.push_back({"drag", "Drag", "surface_integral(pressure)",
                            problem::ObjectiveDirection::Observe});
    p.accuracyTargets.push_back({"drag", 0.02, std::nullopt});
    p.computeBudget.wallSeconds = 10.0;
    return p;
}

void testUnits() {
    using namespace vulkax::units;
    const auto speed = Quantity::from(36.0, kilometrePerHour);
    check(std::abs(speed.in(metrePerSecond) - 10.0) < 1.0e-12, "unit conversion 36 km/h = 10 m/s");

    bool rejected = false;
    try {
        static_cast<void>(speed.in(pascal));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "dimension mismatch must be rejected");
}

void testProblemValidationAndHash() {
    using namespace vulkax;
    auto p = validProblem();
    check(problem::validateProblem(p).ok(), "valid problem must pass structural validation");

    const auto hashA = problem::stableProblemHash(p);
    std::swap(p.fields[0], p.fields[1]);
    std::swap(p.operators[0], p.operators[1]);
    const auto hashB = problem::stableProblemHash(p);
    check(hashA == hashB, "problem hash must be independent of declaration order");

    p.boundaryConditions[0].physicalDimension = units::pressure;
    check(!problem::validateProblem(p).ok(), "dimensionally invalid boundary must fail validation");
}

void testOperatorGraph() {
    const auto p = validProblem();
    const vulkax::operators::OperatorGraph graph(p);
    const auto readers = graph.operatorsReading("p");
    check(readers.size() == 1 && readers.front() == "pressure", "operator graph must expose field readers");
    const auto writers = graph.operatorsWriting("u");
    check(writers.size() == 2, "operator graph must allow multiple residual mechanisms per field");
}

void testBackendSelection() {
    using namespace vulkax::backend;
    const std::vector<Feature> features{Feature::Compute, Feature::StorageBuffers, Feature::Atomics};
    std::vector<BackendCapabilities> candidates{
        {BackendKind::Vulkan, true, true, true, 0.9, 8ull << 30u, "vk", features},
        {BackendKind::Metal, true, true, false, 1.0, 8ull << 30u, "metal", features},
        {BackendKind::OpenGL, true, false, false, 0.8, 2ull << 30u, "gl", features},
    };
    WorkloadRequirements requirement;
    requirement.requiredFeatures = {Feature::StorageBuffers, Feature::Atomics};

    const auto mac = selectBackend(candidates, requirement, PlatformKind::MacOS);
    check(mac.kind == BackendKind::Metal, "Metal should win on macOS when it satisfies the workload");

    const auto linux = selectBackend(candidates, requirement, PlatformKind::Linux);
    check(linux.kind == BackendKind::Vulkan, "Vulkan should win on Linux when it satisfies the workload");

    candidates[0].available = false;
    candidates[1].available = false;
    const auto fallback = selectBackend(candidates, requirement, PlatformKind::Linux);
    check(fallback.kind == BackendKind::OpenGL, "OpenGL should remain an explicit compatibility fallback");
}

void testResultCertificate() {
    using namespace vulkax::verify;
    ResultCertificate certificate;
    certificate.criteria.push_back({"mass conservation", 1.0e-6, 1.0e-5,
                                    CriterionRelation::LessEqual, true});
    certificate.updateTrustState(false);
    check(certificate.state == TrustState::Converging,
          "passing evidence without convergence study is not verified");
    certificate.updateTrustState(true);
    check(certificate.state == TrustState::Verified, "verification requires passing evidence and convergence study");
    certificate.criteria[0].measured = 1.0e-2;
    certificate.updateTrustState(true);
    check(certificate.state == TrustState::Rejected, "failed required evidence must reject verification");
}

} // namespace

int main() {
    testUnits();
    testProblemValidationAndHash();
    testOperatorGraph();
    testBackendSelection();
    testResultCertificate();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Vulkax core tests passed\n";
    return 0;
}
