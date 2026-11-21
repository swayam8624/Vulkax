// SOURCE STUDY NOTE:
// Numerical convergence evaluation. Use verify/convergence.hpp and experiment/design.hpp to understand
// what is measured and how solver correctness differs from render quality or runtime speed.
// Read docs/SOURCE_READING_ORDER.md for preceding and dependent layers.

#include "vulkax/verify/convergence.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vulkax::verify {

// Numerical three-resolution estimate: infer an observed convergence
// order/extrapolated value from coarse/medium/fine measurements with a
// known refinement ratio. Reject nonmonotone or degenerate differences;
// a visually plausible result is not a valid convergence certificate.
ConvergenceEstimate estimateThreeLevelConvergence(double coarseValue, double mediumValue,
                                                    double fineValue, double refinementRatio,
                                                    double safetyFactor) {
    if (refinementRatio <= 1.0 || safetyFactor <= 0.0) {
        throw std::invalid_argument("invalid refinement ratio or convergence safety factor");
    }
    const double coarseDifference = coarseValue - mediumValue;
    const double fineDifference = mediumValue - fineValue;
    if (std::abs(coarseDifference) <= std::numeric_limits<double>::epsilon() ||
        std::abs(fineDifference) <= std::numeric_limits<double>::epsilon()) {
        throw std::invalid_argument("convergence estimate requires nonzero differences");
    }
    if (coarseDifference * fineDifference <= 0.0) {
        throw std::invalid_argument("three-level sequence is not monotonically convergent");
    }

    const double order = std::log(std::abs(coarseDifference / fineDifference)) /
                         std::log(refinementRatio);
    const double denominator = std::pow(refinementRatio, order) - 1.0;
    if (std::abs(denominator) <= std::numeric_limits<double>::epsilon()) {
        throw std::runtime_error("convergence extrapolation denominator is singular");
    }
    const double extrapolated = fineValue + (fineValue - mediumValue) / denominator;
    const double relative = std::abs((fineValue - mediumValue) / denominator) /
                            std::max(std::abs(fineValue), 1.0e-30);
    return {order, extrapolated, relative, safetyFactor * relative};
}

} // namespace vulkax::verify
