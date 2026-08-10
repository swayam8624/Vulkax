#pragma once

#include "vulkax/problem/problem_ir.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vulkax::planner {

enum class SolverFamily : std::uint8_t { FieldEvolution, IncompressibleCFD, DEM, FEM, RayIntegration, RigidDynamics, Unknown };
struct SolverPlan {
    SolverFamily family{SolverFamily::Unknown};
    std::string method;
    std::vector<std::string> reasons;
    std::vector<std::string> verificationEvidence;
    bool executableWithCurrentReferenceSolver{false};
};
[[nodiscard]] SolverPlan planSolver(const problem::ProblemIR& problem);

} // namespace vulkax::planner
