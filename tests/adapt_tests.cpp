#include "vulkax/adapt/goal_oriented.hpp"

#include <cassert>
#include <cmath>
#include <vector>

int main() {
    using vulkax::adapt::CellEvidence;
    // Cell 0 has the largest raw error, but it cannot materially affect the requested observable.
    // Cells 2 and 1 are the correct places to spend the two-cell refinement budget because their
    // goal-oriented scores |error * influence| are 2.0 and 0.8 respectively.
    const std::vector<CellEvidence> evidence = {
        {0, 10.0, 0.001, 1.0},
        {1, 2.0, 0.4, 1.0},
        {2, 1.0, 2.0, 1.0},
        {3, 0.7, 1.0, 4.0},
    };

    const auto plan = vulkax::adapt::selectGoalOrientedRefinement(evidence, 2);
    assert(plan.refine.size() == 2);
    assert(plan.refine[0].cellIndex == 2);
    assert(plan.refine[1].cellIndex == 1);

    const double expectedSelectedMass = 2.0 + 0.8;
    const double expectedTotalMass = 0.01 + 0.8 + 2.0 + 0.7;
    const double expectedFraction = expectedSelectedMass / expectedTotalMass;
    assert(std::abs(plan.selectedObjectiveErrorMass - expectedSelectedMass) < 1.0e-12);
    assert(std::abs(plan.totalObjectiveErrorMass - expectedTotalMass) < 1.0e-12);
    assert(std::abs(plan.selectedFraction - expectedFraction) < 1.0e-12);
    return 0;
}
