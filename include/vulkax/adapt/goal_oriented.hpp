#pragma once

#include <cstddef>
#include <vector>

namespace vulkax::adapt {

struct CellEvidence {
    std::size_t cellIndex{};
    double localErrorEstimate{};
    double observableInfluence{};
    double currentCost{1.0};
};

struct RefinementDecision {
    std::size_t cellIndex{};
    double objectiveErrorScore{};
    double scorePerCost{};
};

struct RefinementPlan {
    std::vector<RefinementDecision> refine;
    double selectedObjectiveErrorMass{};
    double totalObjectiveErrorMass{};
    double selectedFraction{};
};

// Goal-oriented score: |local discretization error * dJ/d(local intervention)|. The planner then
// chooses the highest expected reduction per unit of computational cost.
[[nodiscard]] RefinementPlan selectGoalOrientedRefinement(
    const std::vector<CellEvidence>& evidence, std::size_t maximumCells);

} // namespace vulkax::adapt
