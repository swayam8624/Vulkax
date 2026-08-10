#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/numerics/grid.hpp"
#include "vulkax/planning/solver_plan.hpp"
#include "vulkax/problem/problem_ir.hpp"
#include "vulkax/solvers/dem.hpp"
#include "vulkax/solvers/fem.hpp"
#include "vulkax/solvers/incompressible2d.hpp"
#include "vulkax/solvers/diffusion.hpp"
#include "vulkax/verify/result_certificate.hpp"

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace vulkax::execution {

struct DiffusionExperiment {
    numerics::ScalarGrid3D state;
    solvers::DiffusionConfig config;
};

struct DemExperiment {
    std::vector<solvers::DemParticle> particles;
    solvers::DemBox box;
    solvers::DemConfig config;
    std::size_t steps{1};
};

struct FluidExperiment {
    solvers::FlowGrid2D state;
    solvers::Incompressible2DConfig config;
};

struct FemExperiment {
    std::vector<solvers::FemNode> nodes;
    std::vector<solvers::Tetrahedron> elements;
    solvers::LinearElasticMaterial material;
};

using ExperimentPayload = std::variant<DiffusionExperiment, DemExperiment, FluidExperiment, FemExperiment>;

struct ExperimentRequest {
    problem::ProblemIR problem;
    ExperimentPayload payload;
    std::vector<backend::BackendCapabilities> backendCandidates;
    bool convergenceStudyComplete{false};
};

struct Metric {
    std::string name;
    double value{};
};

struct ExperimentResult {
    planning::SolverPlan plan;
    verify::ResultCertificate certificate;
    std::vector<Metric> metrics;
    ExperimentPayload finalPayload;
};

[[nodiscard]] ExperimentResult executeExperiment(ExperimentRequest request);
[[nodiscard]] std::uint64_t stableSolverPlanHash(const planning::SolverPlan& plan);

} // namespace vulkax::execution
