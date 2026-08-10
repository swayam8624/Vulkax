#pragma once

#include "vulkax/inverse/hyperelastic.hpp"

#include <vector>

namespace vulkax::experiment {

struct ExperimentChoice {
    double stretch{1.0};
    double modelDiscriminationScore{};
    double parameterInformationScore{};
    double combinedScore{};
};

[[nodiscard]] ExperimentChoice selectNextUniaxialExperiment(
    const std::vector<inverse::UniaxialSample>& existingSamples,
    const std::vector<double>& candidateStretches,
    double assumedNoiseStandardDeviation);

} // namespace vulkax::experiment
