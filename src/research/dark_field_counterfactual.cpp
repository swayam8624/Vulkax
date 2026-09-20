#include "vulkax/research/dark_field_counterfactual.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace vulkax::research::dcs {
namespace {

void requireSameResponseSize(const std::vector<Response>& responses) {
    if (responses.empty()) throw std::invalid_argument("DCS requires at least one response");
    const std::size_t width = responses.front().size();
    if (width == 0) throw std::invalid_argument("DCS responses cannot be empty");
    for (const auto& response : responses)
        if (response.size() != width)
            throw std::invalid_argument("DCS response dimensions must match");
}

double integerPower(double base, std::size_t exponent) {
    double result = 1.0;
    for (std::size_t i = 0; i < exponent; ++i) result *= base;
    return result;
}

void enumerateExponentsRecursive(
    std::size_t dimension,
    std::size_t axis,
    std::size_t remainingDegree,
    std::vector<std::size_t>& current,
    std::vector<std::vector<std::size_t>>& out) {
    if (axis + 1 == dimension) {
        current[axis] = remainingDegree;
        out.push_back(current);
        return;
    }
    for (std::size_t exponent = 0; exponent <= remainingDegree; ++exponent) {
        current[axis] = exponent;
        enumerateExponentsRecursive(
            dimension, axis + 1, remainingDegree - exponent, current, out);
    }
}

std::vector<std::vector<std::size_t>> exponentVectors(
    std::size_t dimension,
    std::size_t totalDegree) {
    std::vector<std::vector<std::size_t>> out;
    std::vector<std::size_t> current(dimension, 0U);
    enumerateExponentsRecursive(dimension, 0U, totalDegree, current, out);
    return out;
}

double monomial(
    const std::vector<double>& coordinates,
    const std::vector<std::size_t>& exponents) {
    if (coordinates.size() != exponents.size())
        throw std::invalid_argument("DCS monomial dimension mismatch");
    double result = 1.0;
    for (std::size_t axis = 0; axis < coordinates.size(); ++axis)
        result *= integerPower(coordinates[axis], exponents[axis]);
    return result;
}

} // namespace

double responseNorm(const Response& response) {
    long double sum = 0.0L;
    for (const double value : response) {
        if (!std::isfinite(value))
            throw std::invalid_argument("DCS response contains a non-finite value");
        sum += static_cast<long double>(value) * value;
    }
    return std::sqrt(static_cast<double>(sum));
}

double responseDistance(const Response& lhs, const Response& rhs) {
    if (lhs.size() != rhs.size())
        throw std::invalid_argument("DCS response dimensions must match");
    long double sum = 0.0L;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (!std::isfinite(lhs[i]) || !std::isfinite(rhs[i]))
            throw std::invalid_argument("DCS response contains a non-finite value");
        const long double delta = static_cast<long double>(lhs[i]) - rhs[i];
        sum += delta * delta;
    }
    return std::sqrt(static_cast<double>(sum));
}

Response counterfactualCumulant(
    const std::vector<Response>& subsetResponses,
    std::size_t interventionCount) {
    if (interventionCount == 0 || interventionCount >= 63)
        throw std::invalid_argument("DCS intervention count must lie in [1, 62]");
    const std::uint64_t subsetCount = std::uint64_t{1} << interventionCount;
    if (subsetResponses.size() != subsetCount)
        throw std::invalid_argument("DCS cumulant requires one response per intervention subset");
    requireSameResponseSize(subsetResponses);

    Response result(subsetResponses.front().size(), 0.0);
    for (std::uint64_t mask = 0; mask < subsetCount; ++mask) {
        const std::size_t included = static_cast<std::size_t>(std::popcount(mask));
        const bool positive = ((interventionCount - included) % 2U) == 0U;
        const double sign = positive ? 1.0 : -1.0;
        const auto& response = subsetResponses[static_cast<std::size_t>(mask)];
        for (std::size_t component = 0; component < result.size(); ++component)
            result[component] += sign * response[component];
    }
    return result;
}

MomentValidation validateAnnihilatingStencil(
    const AnnihilatingStencil& stencil,
    double tolerance) {
    if (stencil.order == 0)
        throw std::invalid_argument("DCS stencil order must be positive");
    if (!std::isfinite(tolerance) || tolerance < 0.0)
        throw std::invalid_argument("DCS stencil tolerance must be finite and non-negative");
    if (stencil.points.empty() || stencil.points.size() != stencil.weights.size())
        throw std::invalid_argument("DCS stencil point/weight cardinality mismatch");
    const std::size_t dimension = stencil.points.front().coordinates.size();
    if (dimension == 0)
        throw std::invalid_argument("DCS intervention points need at least one coordinate");
    for (const auto& point : stencil.points) {
        if (point.coordinates.size() != dimension)
            throw std::invalid_argument("DCS intervention point dimensions must match");
        for (const double value : point.coordinates)
            if (!std::isfinite(value))
                throw std::invalid_argument("DCS intervention coordinates must be finite");
    }
    for (const double weight : stencil.weights)
        if (!std::isfinite(weight))
            throw std::invalid_argument("DCS stencil weights must be finite");

    MomentValidation result;
    result.maximumResidualByDegree.resize(stencil.order, 0.0);
    for (std::size_t degree = 0; degree < stencil.order; ++degree) {
        for (const auto& exponents : exponentVectors(dimension, degree)) {
            long double residual = 0.0L;
            for (std::size_t sample = 0; sample < stencil.points.size(); ++sample)
                residual += static_cast<long double>(stencil.weights[sample]) *
                    monomial(stencil.points[sample].coordinates, exponents);
            const double absolute = std::abs(static_cast<double>(residual));
            result.maximumResidualByDegree[degree] =
                std::max(result.maximumResidualByDegree[degree], absolute);
            result.maximumAbsoluteResidual =
                std::max(result.maximumAbsoluteResidual, absolute);
        }
    }
    result.valid = result.maximumAbsoluteResidual <= tolerance;
    return result;
}

Response applyAnnihilatingStencil(
    const std::vector<Response>& responses,
    const AnnihilatingStencil& stencil,
    double tolerance) {
    if (responses.size() != stencil.points.size())
        throw std::invalid_argument("DCS stencil response cardinality mismatch");
    requireSameResponseSize(responses);
    const auto validation = validateAnnihilatingStencil(stencil, tolerance);
    if (!validation.valid)
        throw std::invalid_argument("DCS stencil does not annihilate its declared lower orders");

    Response result(responses.front().size(), 0.0);
    for (std::size_t sample = 0; sample < responses.size(); ++sample)
        for (std::size_t component = 0; component < result.size(); ++component)
            result[component] += stencil.weights[sample] * responses[sample][component];
    return result;
}

MechanismContactResult mechanismOrderOfContact(
    const std::vector<Response>& referenceByOrder,
    const std::vector<Response>& candidateByOrder,
    double relativeTolerance,
    double absoluteFloor) {
    if (referenceByOrder.empty() || referenceByOrder.size() != candidateByOrder.size())
        throw std::invalid_argument("DCS mechanism spectra must have equal non-zero order count");
    if (!std::isfinite(relativeTolerance) || relativeTolerance < 0.0 ||
        !std::isfinite(absoluteFloor) || absoluteFloor <= 0.0)
        throw std::invalid_argument("DCS contact tolerances are invalid");

    MechanismContactResult result;
    result.normalizedDistanceByOrder.reserve(referenceByOrder.size());
    for (std::size_t order = 0; order < referenceByOrder.size(); ++order) {
        const double distance = responseDistance(referenceByOrder[order], candidateByOrder[order]);
        const double scale = std::max(responseNorm(referenceByOrder[order]), absoluteFloor);
        const double normalized = distance / scale;
        result.normalizedDistanceByOrder.push_back(normalized);
        if (!result.separated && normalized > relativeTolerance) {
            result.separated = true;
            result.separatingOrder = order + 1U;
        }
    }
    return result;
}

ScaleFlowResult analyzeWitnessScaleFlow(
    const std::vector<double>& scales,
    const std::vector<double>& witnessNorms) {
    if (scales.size() < 2 || scales.size() != witnessNorms.size())
        throw std::invalid_argument("DCS scale flow needs equal scale/witness arrays of length >= 2");
    ScaleFlowResult result;
    result.finite = true;
    result.adjacentLogSlopes.reserve(scales.size() - 1U);
    for (std::size_t i = 1; i < scales.size(); ++i) {
        if (!std::isfinite(scales[i - 1]) || !std::isfinite(scales[i]) ||
            !std::isfinite(witnessNorms[i - 1]) || !std::isfinite(witnessNorms[i]) ||
            scales[i - 1] <= 0.0 || scales[i] <= 0.0 ||
            witnessNorms[i - 1] <= 0.0 || witnessNorms[i] <= 0.0 ||
            scales[i - 1] == scales[i]) {
            result.finite = false;
            result.adjacentLogSlopes.push_back(std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        const double slope =
            std::log(witnessNorms[i] / witnessNorms[i - 1]) /
            std::log(scales[i] / scales[i - 1]);
        result.adjacentLogSlopes.push_back(slope);
    }
    result.tailLogSlope = result.adjacentLogSlopes.empty()
        ? std::numeric_limits<double>::quiet_NaN()
        : result.adjacentLogSlopes.back();
    return result;
}

DeceptiveRepairAssessment classifyDeceptiveRepair(
    double observationLossBefore,
    double observationLossAfter,
    double witnessErrorBefore,
    double witnessErrorAfter,
    double minimumMeaningfulChange) {
    for (const double value : {
             observationLossBefore, observationLossAfter,
             witnessErrorBefore, witnessErrorAfter, minimumMeaningfulChange})
        if (!std::isfinite(value))
            throw std::invalid_argument("DCS deceptive-repair inputs must be finite");
    if (minimumMeaningfulChange < 0.0)
        throw std::invalid_argument("DCS meaningful-change threshold cannot be negative");

    DeceptiveRepairAssessment result;
    result.observationLossChange = observationLossAfter - observationLossBefore;
    result.witnessErrorChange = witnessErrorAfter - witnessErrorBefore;
    result.observationImproved =
        result.observationLossChange < -minimumMeaningfulChange;
    result.witnessWorsened =
        result.witnessErrorChange > minimumMeaningfulChange;
    result.deceptive = result.observationImproved && result.witnessWorsened;
    return result;
}

double darkFieldDiscriminationScore(
    const std::vector<Response>& modelWitnesses,
    double measurementVariance,
    double numericalVariance,
    double acquisitionCost,
    double costWeight) {
    requireSameResponseSize(modelWitnesses);
    if (modelWitnesses.size() < 2)
        throw std::invalid_argument("DCS discrimination needs at least two model witnesses");
    for (const double value : {
             measurementVariance, numericalVariance, acquisitionCost, costWeight})
        if (!std::isfinite(value) || value < 0.0)
            throw std::invalid_argument("DCS discrimination denominator inputs must be finite and non-negative");

    Response mean(modelWitnesses.front().size(), 0.0);
    for (const auto& response : modelWitnesses)
        for (std::size_t component = 0; component < mean.size(); ++component)
            mean[component] += response[component];
    for (double& value : mean) value /= static_cast<double>(modelWitnesses.size());

    long double dispersion = 0.0L;
    for (const auto& response : modelWitnesses) {
        const double distance = responseDistance(response, mean);
        dispersion += static_cast<long double>(distance) * distance;
    }
    dispersion /= static_cast<long double>(modelWitnesses.size() * mean.size());

    const double denominator =
        measurementVariance + numericalVariance + costWeight * acquisitionCost;
    return static_cast<double>(dispersion) /
        std::max(denominator, std::numeric_limits<double>::epsilon());
}

} // namespace vulkax::research::dcs
