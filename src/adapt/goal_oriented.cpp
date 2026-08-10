#include "vulkax/adapt/goal_oriented.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::adapt {

RefinementPlan selectGoalOrientedRefinement(const std::vector<CellEvidence>& evidence,
                                             std::size_t maximumCells) {
    if (evidence.empty() || maximumCells == 0) {
        throw std::invalid_argument("goal-oriented refinement requires evidence and a positive budget");
    }
    RefinementPlan result;
    std::vector<RefinementDecision> candidates;
    candidates.reserve(evidence.size());
    for (const auto& cell : evidence) {
        if (!std::isfinite(cell.localErrorEstimate) || !std::isfinite(cell.observableInfluence) ||
            !std::isfinite(cell.currentCost) || cell.currentCost <= 0.0) {
            throw std::invalid_argument("invalid goal-oriented cell evidence");
        }
        const double score = std::abs(cell.localErrorEstimate * cell.observableInfluence);
        result.totalObjectiveErrorMass += score;
        candidates.push_back({cell.cellIndex, score, score / cell.currentCost});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.scorePerCost != rhs.scorePerCost) return lhs.scorePerCost > rhs.scorePerCost;
        return lhs.objectiveErrorScore > rhs.objectiveErrorScore;
    });
    const std::size_t count = std::min(maximumCells, candidates.size());
    result.refine.assign(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(count));
    for (const auto& decision : result.refine) result.selectedObjectiveErrorMass += decision.objectiveErrorScore;
    result.selectedFraction = result.totalObjectiveErrorMass > 0.0
                                  ? result.selectedObjectiveErrorMass / result.totalObjectiveErrorMass
                                  : 0.0;
    return result;
}

} // namespace vulkax::adapt
