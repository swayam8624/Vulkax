#include "vulkax/adapt/goal_oriented.hpp"

#include <cassert>
#include <vector>

int main() {
    using vulkax::adapt::CellEvidence;
    // Cell 0 has the largest raw error, but it cannot affect the requested observable. Cell 2 is
    // the correct place to spend resolution because its error is strongly coupled to J.
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
    assert(plan.selectedFraction > 0.8);
    return 0;
}
