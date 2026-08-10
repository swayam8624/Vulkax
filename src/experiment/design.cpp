#include "vulkax/experiment/design.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vulkax::experiment {

ExperimentChoice selectNextUniaxialExperiment(
    const std::vector<inverse::UniaxialSample>& existingSamples,
    const std::vector<double>& candidateStretches,
    double assumedNoiseStandardDeviation) {
    if (existingSamples.empty() || candidateStretches.empty() || assumedNoiseStandardDeviation <= 0.0) {
        throw std::invalid_argument("experiment design requires data, candidates, and positive noise");
    }
    const auto fits = inverse::rankHyperelasticModels(existingSamples);
    if (fits.size() < 2) {
        throw std::invalid_argument("model discrimination requires at least two fitted model families");
    }

    const double noiseVariance = assumedNoiseStandardDeviation * assumedNoiseStandardDeviation;
    ExperimentChoice best;
    best.combinedScore = -std::numeric_limits<double>::infinity();

    for (double stretch : candidateStretches) {
        if (stretch <= 0.0) {
            throw std::invalid_argument("candidate stretch must be positive");
        }
        std::vector<double> predictions;
        double information = 0.0;
        for (const auto& fit : fits) {
            predictions.push_back(inverse::predictUniaxialNominal(fit.model, fit.parameters, stretch));
            const auto sensitivity = inverse::uniaxialSensitivityBasis(fit.model, stretch);
            double sensitivityNormSquared = 0.0;
            for (double value : sensitivity) {
                sensitivityNormSquared += value * value;
            }
            information += std::log1p(sensitivityNormSquared / noiseVariance);
        }
        double mean = 0.0;
        for (double prediction : predictions) {
            mean += prediction;
        }
        mean /= static_cast<double>(predictions.size());
        double variance = 0.0;
        for (double prediction : predictions) {
            const double delta = prediction - mean;
            variance += delta * delta;
        }
        variance /= static_cast<double>(predictions.size());
        const double discrimination = variance / noiseVariance;
        information /= static_cast<double>(fits.size());
        const double combined = discrimination + 0.05 * information;
        if (combined > best.combinedScore) {
            best = {stretch, discrimination, information, combined};
        }
    }
    return best;
}

} // namespace vulkax::experiment
