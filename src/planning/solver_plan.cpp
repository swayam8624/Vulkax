#include "vulkax/planning/solver_plan.hpp"

#include <algorithm>
#include <stdexcept>

namespace vulkax::planning {

namespace {

struct FamilyChoice {
    DiscretizationKind discretization;
    SolverKind solver;
    std::string reason;
};

FamilyChoice chooseFamily(const problem::ProblemIR& problem) {
    const bool particles = std::any_of(problem.domains.begin(), problem.domains.end(), [](const auto& domain) {
        return domain.kind == problem::DomainKind::ParticleSet;
    });
    if (particles) {
        return {DiscretizationKind::Particles, SolverKind::DEM, "particle domain selects discrete-element mechanics"};
    }
    const bool rays = std::any_of(problem.domains.begin(), problem.domains.end(), [](const auto& domain) {
        return domain.kind == problem::DomainKind::RayBundle;
    });
    if (rays) {
        return {DiscretizationKind::Rays, SolverKind::RayIntegrator, "ray-bundle domain selects trajectory integration"};
    }
    const bool solid = std::any_of(problem.operators.begin(), problem.operators.end(), [](const auto& op) {
        return op.family == "solid" || op.family == "elasticity" || op.family == "hyperelastic";
    });
    if (solid) {
        const bool nonlinear = std::any_of(problem.operators.begin(), problem.operators.end(), [](const auto& op) {
            return op.family == "hyperelastic";
        });
        return {DiscretizationKind::TetrahedralMesh,
                nonlinear ? SolverKind::NonlinearFEM : SolverKind::LinearFEM,
                nonlinear ? "hyperelastic law selects nonlinear FEM" : "solid mechanics selects FEM"};
    }
    const bool fluid = std::any_of(problem.operators.begin(), problem.operators.end(), [](const auto& op) {
        return op.family == "fluid" || op.family == "incompressible" || op.family == "navier-stokes";
    });
    if (fluid) {
        return {DiscretizationKind::StructuredGrid, SolverKind::ProjectionFluid,
                "incompressible/fluid operators select projection CFD"};
    }
    return {DiscretizationKind::StructuredGrid, SolverKind::ExplicitField,
            "generic field operators select structured-grid field solver"};
}

std::uint64_t estimateMemory(DiscretizationKind discretization, std::size_t resolution) {
    const std::uint64_t r = static_cast<std::uint64_t>(resolution);
    switch (discretization) {
    case DiscretizationKind::StructuredGrid: return r * r * r * 12ull * sizeof(float);
    case DiscretizationKind::Particles: return r * r * 96ull;
    case DiscretizationKind::TetrahedralMesh: return r * r * r * 160ull;
    case DiscretizationKind::Rays: return r * r * 128ull;
    }
    return 0;
}

} // namespace

std::vector<SolverPlan> makeFidelityLadder(const problem::ProblemIR& problem,
                                           const backend::BackendCapabilities& backend) {
    if (!backend.available) {
        throw std::invalid_argument("solver planning requires an available backend");
    }
    const auto family = chooseFamily(problem);
    const bool strongGpu = backend.dedicatedGpu || backend.deviceMemoryBytes >= (8ull << 30u);
    const std::array<FidelityTier, 3> tiers{FidelityTier::Preview, FidelityTier::Engineering,
                                           FidelityTier::Verification};
    const std::array<std::size_t, 3> baseResolution{64, 128, 256};
    const std::array<double, 3> tolerances{5.0e-2, 5.0e-3, 5.0e-4};
    const std::array<double, 3> timeFactors{1.0, 7.0, 45.0};

    std::vector<SolverPlan> result;
    for (std::size_t index = 0; index < tiers.size(); ++index) {
        std::size_t resolution = baseResolution[index];
        if (strongGpu && index > 0) resolution = static_cast<std::size_t>(static_cast<double>(resolution) * 1.25);
        SolverPlan plan;
        plan.fidelity = tiers[index];
        plan.discretization = family.discretization;
        plan.solver = family.solver;
        plan.characteristicResolution = resolution;
        plan.relativeTolerance = tolerances[index];
        plan.estimatedMemoryBytes = estimateMemory(family.discretization, resolution);
        const double backendFactor = backend.nativePlatformBackend ? 0.8 : 1.0;
        plan.estimatedWallSeconds = timeFactors[index] * backendFactor;
        plan.reasons.push_back(family.reason);
        plan.reasons.push_back(index == 0 ? "interactive preview target" :
                               (index == 1 ? "engineering tolerance target" : "verification/refinement target"));
        result.push_back(std::move(plan));
    }
    return result;
}

SolverPlan selectBestPlan(const std::vector<SolverPlan>& ladder, const problem::ComputeBudget& budget) {
    if (ladder.empty()) {
        throw std::invalid_argument("cannot select from an empty solver ladder");
    }
    const auto fits = [&](const SolverPlan& plan) {
        return (!budget.wallSeconds || plan.estimatedWallSeconds <= *budget.wallSeconds) &&
               (!budget.gpuMemoryBytes || plan.estimatedMemoryBytes <= *budget.gpuMemoryBytes);
    };
    for (auto iterator = ladder.rbegin(); iterator != ladder.rend(); ++iterator) {
        if (fits(*iterator)) return *iterator;
    }
    SolverPlan fallback = ladder.front();
    fallback.reasons.push_back("compute budget is below preview estimate; selected minimum available plan");
    return fallback;
}

} // namespace vulkax::planning
