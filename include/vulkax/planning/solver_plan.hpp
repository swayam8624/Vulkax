#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/problem/problem_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vulkax::planning {

enum class FidelityTier : std::uint8_t { Preview, Engineering, Verification };
enum class DiscretizationKind : std::uint8_t { StructuredGrid, Particles, TetrahedralMesh, Rays };
enum class SolverKind : std::uint8_t { ExplicitField, ProjectionFluid, DEM, LinearFEM, NonlinearFEM, RayIntegrator };

struct SolverPlan {
    FidelityTier fidelity{FidelityTier::Preview};
    DiscretizationKind discretization{DiscretizationKind::StructuredGrid};
    SolverKind solver{SolverKind::ExplicitField};
    std::size_t characteristicResolution{64};
    double relativeTolerance{1.0e-2};
    double estimatedWallSeconds{};
    std::uint64_t estimatedMemoryBytes{};
    std::vector<std::string> reasons;
};

[[nodiscard]] std::vector<SolverPlan> makeFidelityLadder(
    const problem::ProblemIR& problem, const backend::BackendCapabilities& backend);
[[nodiscard]] SolverPlan selectBestPlan(const std::vector<SolverPlan>& ladder,
                                        const problem::ComputeBudget& budget);

} // namespace vulkax::planning
