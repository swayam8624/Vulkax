#pragma once

namespace vulkax::verify {

struct ConvergenceEstimate {
    double observedOrder{};
    double richardsonExtrapolated{};
    double fineRelativeErrorEstimate{};
    double gridConvergenceIndex{};
};

[[nodiscard]] ConvergenceEstimate estimateThreeLevelConvergence(double coarseValue,
                                                                 double mediumValue,
                                                                 double fineValue,
                                                                 double refinementRatio,
                                                                 double safetyFactor = 1.25);

} // namespace vulkax::verify
