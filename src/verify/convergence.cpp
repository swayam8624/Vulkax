// TEACHING MODULE: Three-resolution convergence estimate for a scalar
// physical observable. Differences between coarse/medium/fine predictions
// estimate an empirical order p = log(|d_coarse/d_fine|)/log(r), then a
// Richardson-style limit and safety-inflated relative error estimate.
//
// The calculation requires compatible quantities and refinement ratio
// r > 1, nonzero and consistently signed consecutive differences, and a
// nonsingular extrapolation denominator. A returned number is conditional
// on the asymptotic-convergence assumption; it does not prove the solver
// converges for all scenes or that video sampling error is negligible.
// Scientific freeze evidence must track actual resolution/timestep inputs.
#include "vulkax/verify/convergence.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vulkax::verify {

// Estimate observed order and extrapolated fine-grid value. The test
// should include nonconvergent/oscillatory triples and compare against
// an analytical solution where available.
// Given a *consistent observable* evaluated on coarse/medium/fine levels with
// common refinement ratio r>1, estimate apparent order
// p=log(|(coarse-medium)/(medium-fine)|)/log(r). This is conditional on
// asymptotic behavior, not proof of convergence.
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
// Require consecutive differences with the same sign. Oscillation is a
// warning against simple Richardson extrapolation even if one of the three
// values happens to match a target observation.
    if (coarseDifference * fineDifference <= 0.0) {
        throw std::invalid_argument("three-level sequence is not monotonically convergent");
    }

// The ratio of successive level differences estimates a convergence order
// when step sizes decrease geometrically. Estimate can be negative or
// misleading outside asymptotic regime; compare with a refinement study, not
// one triplet alone.
    const double order = std::log(std::abs(coarseDifference / fineDifference)) /
                         std::log(refinementRatio);
    const double denominator = std::pow(refinementRatio, order) - 1.0;
    if (std::abs(denominator) <= std::numeric_limits<double>::epsilon()) {
        throw std::runtime_error("convergence extrapolation denominator is singular");
    }
// Richardson-style extrapolation estimates an infinite-resolution scalar
// limit. The safety-inflated relative correction is an *error estimate*, not
// independent ground truth or a substitute for temporal observation
// uncertainty.
    const double extrapolated = fineValue + (fineValue - mediumValue) / denominator;
    const double relative = std::abs((fineValue - mediumValue) / denominator) /
                            std::max(std::abs(fineValue), 1.0e-30);
    return {order, extrapolated, relative, safetyFactor * relative};
}

} // namespace vulkax::verify
