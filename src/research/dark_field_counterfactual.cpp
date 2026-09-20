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

using Matrix = std::vector<std::vector<double>>;

Matrix invertSquareMatrix(Matrix matrix) {
    const std::size_t n = matrix.size();
    if (n == 0) throw std::invalid_argument("DCS cannot invert an empty matrix");
    for (const auto& row : matrix)
        if (row.size() != n) throw std::invalid_argument("DCS inversion requires a square matrix");
    Matrix inverse(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) inverse[i][i] = 1.0;

    constexpr double pivotTolerance = 1.0e-12;
    for (std::size_t column = 0; column < n; ++column) {
        std::size_t pivot = column;
        double magnitude = std::abs(matrix[pivot][column]);
        for (std::size_t row = column + 1; row < n; ++row) {
            const double candidate = std::abs(matrix[row][column]);
            if (candidate > magnitude) {
                magnitude = candidate;
                pivot = row;
            }
        }
        if (magnitude < pivotTolerance)
            throw std::invalid_argument("DCS intervention moments are rank deficient");
        if (pivot != column) {
            std::swap(matrix[pivot], matrix[column]);
            std::swap(inverse[pivot], inverse[column]);
        }
        const double invPivot = 1.0 / matrix[column][column];
        for (std::size_t j = 0; j < n; ++j) {
            matrix[column][j] *= invPivot;
            inverse[column][j] *= invPivot;
        }
        for (std::size_t row = 0; row < n; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            for (std::size_t j = 0; j < n; ++j) {
                matrix[row][j] -= factor * matrix[column][j];
                inverse[row][j] -= factor * inverse[column][j];
            }
        }
    }
    return inverse;
}

std::vector<double> projectMomentNullspace(
    const std::vector<double>& vector,
    const Matrix& moments,
    const Matrix& gramInverse) {
    const std::size_t rows = moments.size();
    const std::size_t columns = vector.size();
    std::vector<double> av(rows, 0.0);
    for (std::size_t row = 0; row < rows; ++row)
        for (std::size_t column = 0; column < columns; ++column)
            av[row] += moments[row][column] * vector[column];

    std::vector<double> solved(rows, 0.0);
    for (std::size_t row = 0; row < rows; ++row)
        for (std::size_t column = 0; column < rows; ++column)
            solved[row] += gramInverse[row][column] * av[column];

    std::vector<double> projected = vector;
    for (std::size_t column = 0; column < columns; ++column)
        for (std::size_t row = 0; row < rows; ++row)
            projected[column] -= moments[row][column] * solved[row];
    return projected;
}

double vectorNorm(const std::vector<double>& values) {
    long double sum = 0.0L;
    for (const double value : values) sum += static_cast<long double>(value) * value;
    return std::sqrt(static_cast<double>(sum));
}

void normalize(std::vector<double>& values) {
    const double norm = vectorNorm(values);
    if (!(norm > 1.0e-15))
        throw std::runtime_error("DCS projected disagreement has no usable nullspace direction");
    for (double& value : values) value /= norm;
}

std::vector<double> applyModelDisagreement(
    const std::vector<double>& weights,
    const std::vector<std::vector<Response>>& modelResponses) {
    const std::size_t modelCount = modelResponses.size();
    const std::size_t pointCount = weights.size();
    const std::size_t observableCount = modelResponses.front().front().size();

    std::vector<Response> means(pointCount, Response(observableCount, 0.0));
    for (const auto& model : modelResponses)
        for (std::size_t point = 0; point < pointCount; ++point)
            for (std::size_t component = 0; component < observableCount; ++component)
                means[point][component] += model[point][component];
    for (auto& mean : means)
        for (double& value : mean) value /= static_cast<double>(modelCount);

    std::vector<double> result(pointCount, 0.0);
    const double scale = 1.0 / static_cast<double>(modelCount * observableCount);
    for (const auto& model : modelResponses) {
        for (std::size_t component = 0; component < observableCount; ++component) {
            double coefficient = 0.0;
            for (std::size_t point = 0; point < pointCount; ++point)
                coefficient += weights[point] *
                    (model[point][component] - means[point][component]);
            for (std::size_t point = 0; point < pointCount; ++point)
                result[point] += scale * coefficient *
                    (model[point][component] - means[point][component]);
        }
    }
    return result;
}

void validateModelResponseGrid(
    const std::vector<InterventionPoint>& points,
    const std::vector<std::vector<Response>>& modelResponses) {
    if (points.empty()) throw std::invalid_argument("DCS synthesis needs intervention points");
    if (modelResponses.size() < 2)
        throw std::invalid_argument("DCS synthesis needs at least two candidate models");
    const std::size_t pointCount = points.size();
    const std::size_t dimension = points.front().coordinates.size();
    if (dimension == 0) throw std::invalid_argument("DCS synthesis intervention dimension is zero");
    for (const auto& point : points) {
        if (point.coordinates.size() != dimension)
            throw std::invalid_argument("DCS synthesis intervention dimensions must match");
        for (const double value : point.coordinates)
            if (!std::isfinite(value))
                throw std::invalid_argument("DCS synthesis intervention coordinates must be finite");
    }
    std::size_t observableCount = 0;
    for (const auto& model : modelResponses) {
        if (model.size() != pointCount)
            throw std::invalid_argument("DCS synthesis model/point cardinality mismatch");
        requireSameResponseSize(model);
        if (observableCount == 0) observableCount = model.front().size();
        if (model.front().size() != observableCount)
            throw std::invalid_argument("DCS synthesis observable dimensions must match across models");
    }
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

JetContactEstimate estimateJetOrderOfContact(
    const std::vector<std::vector<Response>>& referenceWitnessesByOrder,
    const std::vector<std::vector<Response>>& candidateWitnessesByOrder,
    const std::vector<UncertaintyBudget>& uncertaintyByOrder,
    double separatingStandardizedDiscrepancy) {
    if (referenceWitnessesByOrder.empty() ||
        referenceWitnessesByOrder.size() != candidateWitnessesByOrder.size() ||
        referenceWitnessesByOrder.size() != uncertaintyByOrder.size())
        throw std::invalid_argument("DCS jet contact requires equal non-empty order arrays");
    if (!std::isfinite(separatingStandardizedDiscrepancy) ||
        separatingStandardizedDiscrepancy <= 0.0)
        throw std::invalid_argument("DCS jet-contact threshold must be positive and finite");

    JetContactEstimate result;
    result.maximumStandardizedDiscrepancyByOrder.reserve(referenceWitnessesByOrder.size());
    result.witnessCountByOrder.reserve(referenceWitnessesByOrder.size());

    for (std::size_t order = 0; order < referenceWitnessesByOrder.size(); ++order) {
        const auto& reference = referenceWitnessesByOrder[order];
        const auto& candidate = candidateWitnessesByOrder[order];
        if (reference.empty() || reference.size() != candidate.size())
            throw std::invalid_argument("DCS jet-contact witness bases must match and be non-empty");
        double maximum = 0.0;
        for (std::size_t witness = 0; witness < reference.size(); ++witness) {
            maximum = std::max(
                maximum,
                standardizedDarkFieldDiscrepancy(
                    reference[witness], candidate[witness], uncertaintyByOrder[order]));
        }
        result.maximumStandardizedDiscrepancyByOrder.push_back(maximum);
        result.witnessCountByOrder.push_back(reference.size());
        if (!result.separated && maximum >= separatingStandardizedDiscrepancy) {
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

SynthesizedStencil synthesizeAnnihilatingStencil(
    const std::vector<InterventionPoint>& points,
    std::size_t order,
    const std::vector<std::vector<Response>>& modelResponses,
    double momentTolerance,
    std::size_t maximumIterations) {
    if (order == 0 || maximumIterations == 0)
        throw std::invalid_argument("DCS synthesis order/iteration count must be positive");
    if (!std::isfinite(momentTolerance) || momentTolerance <= 0.0)
        throw std::invalid_argument("DCS synthesis moment tolerance must be positive and finite");
    validateModelResponseGrid(points, modelResponses);

    const std::size_t pointCount = points.size();
    const std::size_t dimension = points.front().coordinates.size();

    Matrix moments;
    for (std::size_t degree = 0; degree < order; ++degree) {
        for (const auto& exponents : exponentVectors(dimension, degree)) {
            std::vector<double> row(pointCount, 0.0);
            for (std::size_t point = 0; point < pointCount; ++point)
                row[point] = monomial(points[point].coordinates, exponents);
            moments.push_back(std::move(row));
        }
    }
    if (moments.size() >= pointCount)
        throw std::invalid_argument(
            "DCS synthesis needs more intervention points than lower-order moment constraints");

    Matrix gram(moments.size(), std::vector<double>(moments.size(), 0.0));
    for (std::size_t i = 0; i < moments.size(); ++i)
        for (std::size_t j = 0; j < moments.size(); ++j)
            for (std::size_t point = 0; point < pointCount; ++point)
                gram[i][j] += moments[i][point] * moments[j][point];
    const Matrix gramInverse = invertSquareMatrix(std::move(gram));

    bool found = false;
    SynthesizedStencil best;
    double bestEnergy = -std::numeric_limits<double>::infinity();

    for (std::size_t seed = 0; seed < pointCount; ++seed) {
        std::vector<double> basis(pointCount, 0.0);
        basis[seed] = 1.0;
        auto weights = projectMomentNullspace(basis, moments, gramInverse);
        const double seedNorm = vectorNorm(weights);
        if (!(seedNorm > 1.0e-15))
            continue;
        for (double& value : weights) value /= seedNorm;

        SynthesizedStencil candidate;
        bool usable = true;
        for (std::size_t iteration = 0; iteration < maximumIterations; ++iteration) {
            const auto projectedWeights =
                projectMomentNullspace(weights, moments, gramInverse);
            auto next = applyModelDisagreement(projectedWeights, modelResponses);
            next = projectMomentNullspace(next, moments, gramInverse);
            const double norm = vectorNorm(next);
            if (!(norm > 1.0e-15)) {
                usable = false;
                break;
            }
            for (double& value : next) value /= norm;

            long double same = 0.0L;
            long double opposite = 0.0L;
            for (std::size_t i = 0; i < pointCount; ++i) {
                const long double ds =
                    static_cast<long double>(next[i]) - weights[i];
                const long double do_ =
                    static_cast<long double>(next[i]) + weights[i];
                same += ds * ds;
                opposite += do_ * do_;
            }
            weights = std::move(next);
            candidate.powerIterations = iteration + 1U;
            if (std::sqrt(static_cast<double>(std::min(same, opposite))) < 1.0e-10) {
                candidate.converged = true;
                break;
            }
        }
        if (!usable)
            continue;

        double l1 = 0.0;
        for (const double value : weights) l1 += std::abs(value);
        if (!(l1 > 1.0e-15))
            continue;
        for (double& value : weights) value /= l1;

        candidate.stencil.order = order;
        candidate.stencil.points = points;
        candidate.stencil.weights = weights;
        candidate.momentValidation =
            validateAnnihilatingStencil(candidate.stencil, momentTolerance);
        if (!candidate.momentValidation.valid)
            continue;

        const auto disagreementApplied =
            applyModelDisagreement(weights, modelResponses);
        for (std::size_t i = 0; i < weights.size(); ++i)
            candidate.modelDisagreementEnergy +=
                weights[i] * disagreementApplied[i];
        candidate.independentNoiseGain = vectorNorm(weights);

        if (!std::isfinite(candidate.modelDisagreementEnergy))
            continue;
        if (!found || candidate.modelDisagreementEnergy > bestEnergy) {
            found = true;
            bestEnergy = candidate.modelDisagreementEnergy;
            best = std::move(candidate);
        }
    }

    if (!found)
        throw std::runtime_error(
            "DCS synthesis found no model disagreement after lower-order annihilation");
    return best;
}

double standardizedDarkFieldDiscrepancy(
    const Response& measured,
    const Response& predicted,
    const UncertaintyBudget& uncertainty) {
    if (measured.empty() || measured.size() != predicted.size())
        throw std::invalid_argument("DCS standardized discrepancy requires equal non-empty responses");
    const double variance = uncertainty.totalVariance();
    if (!std::isfinite(variance) || variance <= 0.0)
        throw std::invalid_argument("DCS uncertainty variance must be positive and finite");
    const double rms = responseDistance(measured, predicted) /
        std::sqrt(static_cast<double>(measured.size()));
    return rms / std::sqrt(variance);
}

UncertaintyBudget propagateStencilUncertainty(
    const AnnihilatingStencil& stencil,
    const UncertaintyBudget& perInterventionUncertainty) {
    if (stencil.weights.empty() || stencil.weights.size() != stencil.points.size())
        throw std::invalid_argument("DCS uncertainty propagation requires a valid non-empty stencil");
    for (const double value : {
             perInterventionUncertainty.measurementVariance,
             perInterventionUncertainty.numericalVariance,
             perInterventionUncertainty.repeatVariance}) {
        if (!std::isfinite(value) || value < 0.0)
            throw std::invalid_argument("DCS per-intervention uncertainty must be finite and non-negative");
    }

    long double l2Squared = 0.0L;
    long double l1 = 0.0L;
    for (const double weight : stencil.weights) {
        if (!std::isfinite(weight))
            throw std::invalid_argument("DCS stencil weight must be finite");
        l2Squared += static_cast<long double>(weight) * weight;
        l1 += std::abs(weight);
    }

    UncertaintyBudget result;
    const double independentGainSquared = static_cast<double>(l2Squared);
    const double conservativeGainSquared =
        static_cast<double>(l1 * l1);
    result.measurementVariance =
        perInterventionUncertainty.measurementVariance * independentGainSquared;
    result.repeatVariance =
        perInterventionUncertainty.repeatVariance * independentGainSquared;
    // Numerical error is generally structured/correlated across intervention
    // points. Use a conservative triangle-inequality bound rather than assuming
    // cancellation or independence.
    result.numericalVariance =
        perInterventionUncertainty.numericalVariance * conservativeGainSquared;
    return result;
}

double worstCaseStandardizedSeparation(
    const std::vector<Response>& modelWitnesses,
    const UncertaintyBudget& uncertainty) {
    requireSameResponseSize(modelWitnesses);
    if (modelWitnesses.size() < 2)
        throw std::invalid_argument("DCS maximin separation needs at least two model witnesses");
    const double variance = uncertainty.totalVariance();
    if (!std::isfinite(variance) || variance <= 0.0)
        throw std::invalid_argument("DCS uncertainty variance must be positive and finite");
    double worst = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < modelWitnesses.size(); ++i) {
        for (std::size_t j = i + 1; j < modelWitnesses.size(); ++j) {
            const double rms = responseDistance(modelWitnesses[i], modelWitnesses[j]) /
                std::sqrt(static_cast<double>(modelWitnesses[i].size()));
            worst = std::min(worst, rms / std::sqrt(variance));
        }
    }
    return worst;
}

double worstCasePairAwareStandardizedSeparation(
    const std::vector<Response>& modelWitnesses,
    const UncertaintyBudget& sharedObservationUncertainty,
    const std::vector<UncertaintyBudget>& modelNumericalUncertainty,
    const AnnihilatingStencil& stencil) {
    requireSameResponseSize(modelWitnesses);
    if (modelWitnesses.size() < 2 ||
        modelWitnesses.size() != modelNumericalUncertainty.size())
        throw std::invalid_argument(
            "DCS pair-aware separation requires one numerical budget per model");

    UncertaintyBudget shared = sharedObservationUncertainty;
    // Shared acquisition uncertainty is propagated once through the signed
    // measurement contrast.
    shared.numericalVariance = 0.0;
    shared = propagateStencilUncertainty(stencil, shared);

    std::vector<UncertaintyBudget> numerical;
    numerical.reserve(modelNumericalUncertainty.size());
    for (const auto& budget : modelNumericalUncertainty) {
        if (budget.measurementVariance != 0.0 || budget.repeatVariance != 0.0)
            throw std::invalid_argument(
                "DCS model-specific budgets may contain numerical variance only");
        numerical.push_back(propagateStencilUncertainty(stencil, budget));
    }

    double worst = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < modelWitnesses.size(); ++i) {
        for (std::size_t j = i + 1; j < modelWitnesses.size(); ++j) {
            const double variance =
                shared.measurementVariance +
                shared.repeatVariance +
                numerical[i].numericalVariance +
                numerical[j].numericalVariance;
            if (!std::isfinite(variance) || variance <= 0.0)
                throw std::invalid_argument(
                    "DCS pair-aware separation variance must be positive and finite");
            const double rms =
                responseDistance(modelWitnesses[i], modelWitnesses[j]) /
                std::sqrt(static_cast<double>(modelWitnesses[i].size()));
            worst = std::min(worst, rms / std::sqrt(variance));
        }
    }
    return worst;
}


double worstCaseWitnessSpaceStandardizedSeparation(
    const std::vector<Response>& nominalModelWitnesses,
    const std::vector<Response>& refinedModelWitnesses,
    const UncertaintyBudget& sharedObservationUncertainty,
    const AnnihilatingStencil& stencil) {
    requireSameResponseSize(nominalModelWitnesses);
    requireSameResponseSize(refinedModelWitnesses);
    if (nominalModelWitnesses.size() < 2 ||
        nominalModelWitnesses.size() != refinedModelWitnesses.size())
        throw std::invalid_argument(
            "DCS witness-space separation requires matched nominal/refined witnesses");

    UncertaintyBudget shared = sharedObservationUncertainty;
    if (shared.numericalVariance != 0.0)
        throw std::invalid_argument(
            "DCS witness-space shared uncertainty may contain measurement/repeat variance only");
    shared = propagateStencilUncertainty(stencil, shared);

    std::vector<double> numericalVariance(nominalModelWitnesses.size(), 0.0);
    for (std::size_t model = 0; model < nominalModelWitnesses.size(); ++model) {
        if (nominalModelWitnesses[model].size() != refinedModelWitnesses[model].size())
            throw std::invalid_argument("DCS witness-space response width mismatch");
        const double rms =
            responseDistance(nominalModelWitnesses[model], refinedModelWitnesses[model]) /
            std::sqrt(static_cast<double>(nominalModelWitnesses[model].size()));
        numericalVariance[model] = rms * rms;
    }

    double worst = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < nominalModelWitnesses.size(); ++i) {
        for (std::size_t j = i + 1; j < nominalModelWitnesses.size(); ++j) {
            const double variance =
                shared.measurementVariance +
                shared.repeatVariance +
                numericalVariance[i] +
                numericalVariance[j];
            if (!std::isfinite(variance) || variance <= 0.0)
                throw std::invalid_argument(
                    "DCS witness-space separation variance must be positive and finite");
            const double rms =
                responseDistance(nominalModelWitnesses[i], nominalModelWitnesses[j]) /
                std::sqrt(static_cast<double>(nominalModelWitnesses[i].size()));
            worst = std::min(worst, rms / std::sqrt(variance));
        }
    }
    return worst;
}

MechanismResolutionResult mechanismResolution(
    const std::vector<Response>& witnessByOrder,
    const std::vector<UncertaintyBudget>& uncertaintyByOrder,
    double minimumStandardizedSignal) {
    if (witnessByOrder.empty() || witnessByOrder.size() != uncertaintyByOrder.size())
        throw std::invalid_argument("DCS mechanism resolution requires equal non-empty order arrays");
    if (!std::isfinite(minimumStandardizedSignal) || minimumStandardizedSignal <= 0.0)
        throw std::invalid_argument("DCS mechanism-resolution threshold must be positive and finite");

    MechanismResolutionResult result;
    result.standardizedSignalByOrder.reserve(witnessByOrder.size());
    for (std::size_t order = 0; order < witnessByOrder.size(); ++order) {
        if (witnessByOrder[order].empty())
            throw std::invalid_argument("DCS mechanism-resolution witness cannot be empty");
        const double variance = uncertaintyByOrder[order].totalVariance();
        if (!std::isfinite(variance) || variance <= 0.0)
            throw std::invalid_argument("DCS mechanism-resolution variance must be positive and finite");
        const double rms = responseNorm(witnessByOrder[order]) /
            std::sqrt(static_cast<double>(witnessByOrder[order].size()));
        const double standardized = rms / std::sqrt(variance);
        result.standardizedSignalByOrder.push_back(standardized);
        if (standardized >= minimumStandardizedSignal) {
            result.anyObservableOrder = true;
            result.maximumObservableOrder = order + 1U;
        }
    }
    return result;
}

SynthesizedStencil synthesizeMaximinAnnihilatingStencil(
    const std::vector<InterventionPoint>& points,
    std::size_t order,
    const std::vector<std::vector<Response>>& modelResponses,
    const UncertaintyBudget& uncertainty,
    double momentTolerance,
    std::size_t maximumIterations) {
    if (order == 0 || maximumIterations == 0)
        throw std::invalid_argument("DCS maximin synthesis order/iteration count must be positive");
    const double variance = uncertainty.totalVariance();
    if (!std::isfinite(variance) || variance <= 0.0)
        throw std::invalid_argument("DCS maximin synthesis uncertainty must be positive and finite");
    validateModelResponseGrid(points, modelResponses);

    const std::size_t pointCount = points.size();
    const std::size_t dimension = points.front().coordinates.size();
    const std::size_t observableCount = modelResponses.front().front().size();

    Matrix moments;
    for (std::size_t degree = 0; degree < order; ++degree) {
        for (const auto& exponents : exponentVectors(dimension, degree)) {
            std::vector<double> row(pointCount, 0.0);
            for (std::size_t point = 0; point < pointCount; ++point)
                row[point] = monomial(points[point].coordinates, exponents);
            moments.push_back(std::move(row));
        }
    }
    if (moments.size() >= pointCount)
        throw std::invalid_argument("DCS maximin synthesis needs more points than lower-order constraints");

    Matrix gram(moments.size(), std::vector<double>(moments.size(), 0.0));
    for (std::size_t i = 0; i < moments.size(); ++i)
        for (std::size_t j = 0; j < moments.size(); ++j)
            for (std::size_t point = 0; point < pointCount; ++point)
                gram[i][j] += moments[i][point] * moments[j][point];
    const Matrix gramInverse = invertSquareMatrix(std::move(gram));

    const auto normalizeL1 = [](std::vector<double> weights) {
        double l1 = 0.0;
        for (const double value : weights) l1 += std::abs(value);
        if (!(l1 > 1.0e-15))
            throw std::runtime_error("DCS maximin synthesis produced a zero stencil");
        for (double& value : weights) value /= l1;
        return weights;
    };

    const auto pairOperator = [&](const std::vector<double>& weights,
                                  std::size_t modelA,
                                  std::size_t modelB) {
        std::vector<double> result(pointCount, 0.0);
        for (std::size_t component = 0; component < observableCount; ++component) {
            double coefficient = 0.0;
            for (std::size_t point = 0; point < pointCount; ++point) {
                const double delta =
                    modelResponses[modelA][point][component] -
                    modelResponses[modelB][point][component];
                coefficient += weights[point] * delta;
            }
            for (std::size_t point = 0; point < pointCount; ++point) {
                const double delta =
                    modelResponses[modelA][point][component] -
                    modelResponses[modelB][point][component];
                result[point] += coefficient * delta /
                    static_cast<double>(observableCount);
            }
        }
        return result;
    };

    const auto makeResult = [&](const std::vector<double>& rawWeights,
                                std::size_t iterations,
                                bool converged) {
        SynthesizedStencil candidate;
        candidate.stencil.order = order;
        candidate.stencil.points = points;
        candidate.stencil.weights = normalizeL1(rawWeights);
        candidate.momentValidation =
            validateAnnihilatingStencil(candidate.stencil, momentTolerance);
        if (!candidate.momentValidation.valid)
            throw std::runtime_error("DCS maximin stencil violates annihilation contract");
        candidate.independentNoiseGain = vectorNorm(candidate.stencil.weights);
        candidate.powerIterations = iterations;
        candidate.converged = converged;

        std::vector<Response> witnesses;
        witnesses.reserve(modelResponses.size());
        for (const auto& model : modelResponses)
            witnesses.push_back(applyAnnihilatingStencil(model, candidate.stencil, momentTolerance));
        const auto witnessUncertainty =
            propagateStencilUncertainty(candidate.stencil, uncertainty);
        candidate.worstCaseStandardizedSeparation =
            worstCaseStandardizedSeparation(witnesses, witnessUncertainty);

        const auto disagreementApplied =
            applyModelDisagreement(candidate.stencil.weights, modelResponses);
        for (std::size_t i = 0; i < candidate.stencil.weights.size(); ++i)
            candidate.modelDisagreementEnergy +=
                candidate.stencil.weights[i] * disagreementApplied[i];
        return candidate;
    };

    SynthesizedStencil best = synthesizeAnnihilatingStencil(
        points, order, modelResponses, momentTolerance, maximumIterations);
    {
        std::vector<Response> witnesses;
        witnesses.reserve(modelResponses.size());
        for (const auto& model : modelResponses)
            witnesses.push_back(applyAnnihilatingStencil(model, best.stencil, momentTolerance));
        const auto witnessUncertainty =
            propagateStencilUncertainty(best.stencil, uncertainty);
        best.worstCaseStandardizedSeparation =
            worstCaseStandardizedSeparation(witnesses, witnessUncertainty);
    }

    for (std::size_t modelA = 0; modelA < modelResponses.size(); ++modelA) {
        for (std::size_t modelB = modelA + 1; modelB < modelResponses.size(); ++modelB) {
            std::vector<double> weights(pointCount, 0.0);
            double bestSeedNorm = -1.0;
            for (std::size_t seed = 0; seed < pointCount; ++seed) {
                std::vector<double> basis(pointCount, 0.0);
                basis[seed] = 1.0;
                auto projected = projectMomentNullspace(basis, moments, gramInverse);
                const double norm = vectorNorm(projected);
                if (norm > bestSeedNorm) {
                    bestSeedNorm = norm;
                    weights = std::move(projected);
                }
            }
            normalize(weights);

            bool converged = false;
            std::size_t iterations = 0;
            try {
                for (std::size_t iteration = 0; iteration < maximumIterations; ++iteration) {
                    auto next = pairOperator(weights, modelA, modelB);
                    next = projectMomentNullspace(next, moments, gramInverse);
                    normalize(next);

                    long double same = 0.0L;
                    long double opposite = 0.0L;
                    for (std::size_t i = 0; i < pointCount; ++i) {
                        const long double ds =
                            static_cast<long double>(next[i]) - weights[i];
                        const long double do_ =
                            static_cast<long double>(next[i]) + weights[i];
                        same += ds * ds;
                        opposite += do_ * do_;
                    }
                    weights = std::move(next);
                    iterations = iteration + 1U;
                    if (std::sqrt(static_cast<double>(std::min(same, opposite))) < 1.0e-10) {
                        converged = true;
                        break;
                    }
                }
                auto candidate = makeResult(weights, iterations, converged);
                if (candidate.worstCaseStandardizedSeparation >
                    best.worstCaseStandardizedSeparation)
                    best = std::move(candidate);
            } catch (const std::runtime_error&) {
                // A particular pair can have no projected disagreement direction.
                // Other pairs and the global-variance baseline remain valid candidates.
            }
        }
    }
    return best;
}

SynthesizedStencil synthesizePairAwareMaximinStencil(
    const std::vector<InterventionPoint>& points,
    std::size_t order,
    const std::vector<std::vector<Response>>& modelResponses,
    const UncertaintyBudget& sharedObservationUncertainty,
    const std::vector<UncertaintyBudget>& modelNumericalUncertainty,
    double momentTolerance,
    std::size_t maximumIterations) {
    if (modelResponses.size() < 2 ||
        modelResponses.size() != modelNumericalUncertainty.size())
        throw std::invalid_argument(
            "DCS pair-aware synthesis requires one numerical budget per model");

    UncertaintyBudget seedUncertainty = sharedObservationUncertainty;
    seedUncertainty.numericalVariance = 0.0;
    if (!(seedUncertainty.totalVariance() > 0.0) ||
        !std::isfinite(seedUncertainty.totalVariance()))
        throw std::invalid_argument(
            "DCS pair-aware synthesis needs positive shared acquisition uncertainty");

    const auto evaluate = [&](SynthesizedStencil candidate) {
        std::vector<Response> witnesses;
        witnesses.reserve(modelResponses.size());
        for (const auto& model : modelResponses)
            witnesses.push_back(
                applyAnnihilatingStencil(model, candidate.stencil, momentTolerance));
        candidate.worstCaseStandardizedSeparation =
            worstCasePairAwareStandardizedSeparation(
                witnesses,
                sharedObservationUncertainty,
                modelNumericalUncertainty,
                candidate.stencil);
        return candidate;
    };

    SynthesizedStencil best = evaluate(
        synthesizeMaximinAnnihilatingStencil(
            points,
            order,
            modelResponses,
            seedUncertainty,
            momentTolerance,
            maximumIterations));

    // A maximin optimum frequently lies near a direction that strongly exposes
    // one currently limiting pair. Generate one exact-nullspace candidate per
    // pair, then score every candidate against all surviving worlds using the
    // pair-aware uncertainty objective.
    for (std::size_t i = 0; i < modelResponses.size(); ++i) {
        for (std::size_t j = i + 1; j < modelResponses.size(); ++j) {
            std::vector<std::vector<Response>> pair{
                modelResponses[i], modelResponses[j]
            };
            try {
                auto candidate = evaluate(
                    synthesizeMaximinAnnihilatingStencil(
                        points,
                        order,
                        pair,
                        seedUncertainty,
                        momentTolerance,
                        maximumIterations));
                if (candidate.worstCaseStandardizedSeparation >
                    best.worstCaseStandardizedSeparation)
                    best = std::move(candidate);
            } catch (const std::runtime_error&) {
                // This pair may have no disagreement direction after the
                // requested lower-order annihilation. Other pairs remain valid.
            }
        }
    }
    return best;
}


SynthesizedStencil synthesizeWitnessSpaceMaximinStencil(
    const std::vector<InterventionPoint>& points,
    std::size_t order,
    const std::vector<std::vector<Response>>& nominalModelResponses,
    const std::vector<std::vector<Response>>& refinedModelResponses,
    const UncertaintyBudget& sharedObservationUncertainty,
    double momentTolerance,
    std::size_t maximumIterations) {
    validateModelResponseGrid(points, nominalModelResponses);
    validateModelResponseGrid(points, refinedModelResponses);
    if (nominalModelResponses.size() != refinedModelResponses.size())
        throw std::invalid_argument(
            "DCS witness-space synthesis requires matched nominal/refined model sets");
    if (sharedObservationUncertainty.numericalVariance != 0.0)
        throw std::invalid_argument(
            "DCS witness-space synthesis shared uncertainty may not include numerical variance");

    UncertaintyBudget seedUncertainty = sharedObservationUncertainty;
    if (!(seedUncertainty.totalVariance() > 0.0) ||
        !std::isfinite(seedUncertainty.totalVariance()))
        throw std::invalid_argument(
            "DCS witness-space synthesis needs positive acquisition uncertainty");

    const auto evaluate = [&](SynthesizedStencil candidate) {
        std::vector<Response> nominalWitnesses;
        std::vector<Response> refinedWitnesses;
        nominalWitnesses.reserve(nominalModelResponses.size());
        refinedWitnesses.reserve(refinedModelResponses.size());
        for (std::size_t model = 0; model < nominalModelResponses.size(); ++model) {
            nominalWitnesses.push_back(
                applyAnnihilatingStencil(
                    nominalModelResponses[model], candidate.stencil, momentTolerance));
            refinedWitnesses.push_back(
                applyAnnihilatingStencil(
                    refinedModelResponses[model], candidate.stencil, momentTolerance));
        }
        candidate.worstCaseStandardizedSeparation =
            worstCaseWitnessSpaceStandardizedSeparation(
                nominalWitnesses,
                refinedWitnesses,
                sharedObservationUncertainty,
                candidate.stencil);
        return candidate;
    };

    SynthesizedStencil best = evaluate(
        synthesizeMaximinAnnihilatingStencil(
            points,
            order,
            nominalModelResponses,
            seedUncertainty,
            momentTolerance,
            maximumIterations));

    for (std::size_t i = 0; i < nominalModelResponses.size(); ++i) {
        for (std::size_t j = i + 1; j < nominalModelResponses.size(); ++j) {
            std::vector<std::vector<Response>> pair{
                nominalModelResponses[i], nominalModelResponses[j]
            };
            try {
                auto candidate = evaluate(
                    synthesizeMaximinAnnihilatingStencil(
                        points,
                        order,
                        pair,
                        seedUncertainty,
                        momentTolerance,
                        maximumIterations));
                if (candidate.worstCaseStandardizedSeparation >
                    best.worstCaseStandardizedSeparation)
                    best = std::move(candidate);
            } catch (const std::runtime_error&) {
                // No usable lower-order-nullspace disagreement for this pair.
            }
        }
    }
    return best;
}

} // namespace vulkax::research::dcs
