#include "vulkax/world/reality_loop.hpp"

#include "vulkax/numerics/dense.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace vulkax::world {
namespace {

struct Evaluation {
    std::vector<double> residuals;
    double objective{};
    ResidualSummary summary;
};

bool hasRole(const std::vector<ObservationRole>& roles, ObservationRole role) {
    return std::find(roles.begin(), roles.end(), role) != roles.end();
}

bool worldHasRole(const WorldIR& world, ObservationRole role) {
    return std::any_of(world.observations.begin(), world.observations.end(),
                       [role](const auto& observation) { return observation.role == role; });
}

void validateSettings(const RealityLoopSettings& settings) {
    if (settings.maxIterations == 0 || settings.maxBacktrackingSteps == 0 || settings.fittingRoles.empty()) {
        throw std::invalid_argument("reality-loop iteration counts must be positive");
    }
    if (!(settings.relativeFiniteDifferenceStep > 0.0) ||
        !(settings.absoluteFiniteDifferenceStep > 0.0) ||
        !(settings.initialDamping > 0.0) || !(settings.dampingGrowth > 1.0) ||
        !(settings.dampingShrink > 0.0 && settings.dampingShrink < 1.0) ||
        settings.objectiveTolerance < 0.0 || settings.relativeImprovementTolerance < 0.0 ||
        settings.parameterStepTolerance < 0.0) {
        throw std::invalid_argument("invalid reality-loop numerical settings");
    }
}

void validateIdentifiabilitySettings(const IdentifiabilitySettings& settings) {
    if (settings.roles.empty() || !(settings.relativeFiniteDifferenceStep > 0.0) ||
        !(settings.absoluteFiniteDifferenceStep > 0.0) ||
        !(settings.relativeRankTolerance > 0.0) || !(settings.relativeRankTolerance < 1.0) ||
        !(settings.absoluteRankTolerance >= 0.0) || !std::isfinite(settings.relativeRankTolerance) ||
        !std::isfinite(settings.absoluteRankTolerance)) {
        throw std::invalid_argument("invalid local-identifiability settings");
    }
}

Evaluation evaluate(const WorldIR& world, const ForwardModel& forwardModel,
                    const std::vector<ObservationRole>& roles) {
    if (!forwardModel) throw std::invalid_argument("reality loop requires a forward model");
    if (world.observations.empty()) throw std::invalid_argument("reality loop requires observations");

    const auto predictions = forwardModel(world);
    std::unordered_map<std::string, const ObservationPrediction*> byId;
    byId.reserve(predictions.size());
    for (const auto& prediction : predictions) {
        if (prediction.observationId.empty() || !byId.emplace(prediction.observationId, &prediction).second) {
            throw std::runtime_error("forward model returned empty or duplicate observation id");
        }
    }

    Evaluation result;
    for (const auto& observation : world.observations) {
        if (!hasRole(roles, observation.role)) continue;
        const auto it = byId.find(observation.id);
        if (it == byId.end()) {
            throw std::runtime_error("forward model did not predict observation: " + observation.id);
        }
        if (it->second->space != observation.space) {
            throw std::runtime_error("forward-model observation space mismatch: " + observation.id);
        }
        const auto& predicted = it->second->values;
        if (predicted.size() != observation.values.size()) {
            throw std::runtime_error("forward-model prediction dimension mismatch: " + observation.id);
        }
        for (std::size_t component = 0; component < predicted.size(); ++component) {
            if (!std::isfinite(predicted[component])) {
                throw std::runtime_error("forward model returned non-finite prediction: " + observation.id);
            }
            const double sigma = observation.standardDeviation.empty()
                                     ? 1.0
                                     : observation.standardDeviation[component];
            const double residual = (predicted[component] - observation.values[component]) / sigma;
            result.residuals.push_back(residual);
            result.objective += 0.5 * residual * residual;
            result.summary.weightedMaximum = std::max(result.summary.weightedMaximum, std::abs(residual));
        }
    }
    result.summary.scalarCount = result.residuals.size();
    if (result.residuals.empty()) {
        throw std::invalid_argument("reality loop selected no observations for the requested role set");
    }
    result.summary.weightedRms =
        std::sqrt(2.0 * result.objective / static_cast<double>(result.residuals.size()));
    return result;
}

double finiteDifferenceStep(double value, double relativeStep, double absoluteStep) {
    return std::max(std::abs(value) * relativeStep, absoluteStep);
}

std::vector<double> parameterValues(const WorldIR& world,
                                    const std::vector<ParameterAddress>& parameters) {
    std::vector<double> values;
    values.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        const auto value = world.parameterValue(parameter);
        if (!value.has_value()) throw std::invalid_argument("reality-loop parameter does not exist");
        values.push_back(*value);
    }
    return values;
}

void requireUniqueParameters(const std::vector<ParameterAddress>& parameters) {
    if (parameters.empty()) throw std::invalid_argument("reality loop requires at least one parameter");
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (parameters[i].name.empty()) throw std::invalid_argument("reality-loop parameter name is empty");
        for (std::size_t j = 0; j < i; ++j) {
            if (parameters[i] == parameters[j]) {
                throw std::invalid_argument("reality-loop parameter list contains duplicates");
            }
        }
    }
}

struct DifferenceColumn {
    std::vector<double> residualDerivative;
    double objectiveDerivative{};
};

DifferenceColumn finiteDifferenceColumn(const WorldIR& world,
                                        const ParameterAddress& parameter,
                                        const ForwardModel& forwardModel,
                                        double relativeStep,
                                        double absoluteStep,
                                        const std::vector<ObservationRole>& roles) {
    const auto baseValue = world.parameterValue(parameter);
    if (!baseValue.has_value()) throw std::invalid_argument("finite-difference parameter does not exist");
    const double requestedStep = finiteDifferenceStep(*baseValue, relativeStep, absoluteStep);
    const double plusValue = world.clampToBelief(parameter, *baseValue + requestedStep);
    const double minusValue = world.clampToBelief(parameter, *baseValue - requestedStep);

    DifferenceColumn column;
    const Evaluation base = evaluate(world, forwardModel, roles);
    column.residualDerivative.assign(base.residuals.size(), 0.0);
    if (plusValue == minusValue) return column;

    WorldIR plus = world;
    WorldIR minus = world;
    if (!plus.setParameterValue(parameter, plusValue) || !minus.setParameterValue(parameter, minusValue)) {
        throw std::invalid_argument("failed to perturb reality-loop parameter");
    }
    const Evaluation plusEvaluation = evaluate(plus, forwardModel, roles);
    const Evaluation minusEvaluation = evaluate(minus, forwardModel, roles);
    if (plusEvaluation.residuals.size() != base.residuals.size() ||
        minusEvaluation.residuals.size() != base.residuals.size()) {
        throw std::runtime_error("forward model changed residual dimension during finite difference");
    }
    const double denominator = plusValue - minusValue;
    for (std::size_t row = 0; row < column.residualDerivative.size(); ++row) {
        column.residualDerivative[row] =
            (plusEvaluation.residuals[row] - minusEvaluation.residuals[row]) / denominator;
    }
    column.objectiveDerivative =
        (plusEvaluation.objective - minusEvaluation.objective) / denominator;
    return column;
}

double identifiabilityParameterScale(const WorldIR& world, const ParameterAddress& parameter) {
    const auto* belief = world.findParameterBelief(parameter);
    if (belief != nullptr && belief->lowerBound.has_value() && belief->upperBound.has_value()) {
        const double span = *belief->upperBound - *belief->lowerBound;
        if (std::isfinite(span) && span > 0.0) return span;
    }
    const auto value = world.parameterValue(parameter);
    if (!value.has_value() || !std::isfinite(*value)) {
        throw std::invalid_argument("identifiability parameter does not exist or is non-finite");
    }
    return std::max(std::abs(*value), 1.0);
}

struct SymmetricEigenResult {
    std::vector<double> values;
    numerics::DenseMatrix vectors;
};

// Jacobi diagonalization is intentionally small/deterministic here. Reality-loop
// parameter sets are expected to be modest, and keeping this reference diagnostic
// independent of an external BLAS/LAPACK stack makes it available in every CI path.
SymmetricEigenResult symmetricEigenJacobi(numerics::DenseMatrix matrix) {
    if (matrix.rows() != matrix.cols()) {
        throw std::invalid_argument("Jacobi eigensolver requires a square matrix");
    }
    const std::size_t n = matrix.rows();
    numerics::DenseMatrix vectors = numerics::DenseMatrix::identity(n);
    if (n <= 1U) {
        return {{n == 1U ? matrix(0, 0) : 0.0}, std::move(vectors)};
    }

    const std::size_t maximumIterations = std::max<std::size_t>(64U, 32U * n * n);
    for (std::size_t iteration = 0; iteration < maximumIterations; ++iteration) {
        std::size_t p = 0U;
        std::size_t q = 1U;
        double largestOffDiagonal = 0.0;
        double diagonalScale = 1.0;
        for (std::size_t row = 0; row < n; ++row) {
            diagonalScale = std::max(diagonalScale, std::abs(matrix(row, row)));
            for (std::size_t column = row + 1U; column < n; ++column) {
                const double magnitude = std::abs(matrix(row, column));
                if (magnitude > largestOffDiagonal) {
                    largestOffDiagonal = magnitude;
                    p = row;
                    q = column;
                }
            }
        }
        if (largestOffDiagonal <= 1.0e-14 * diagonalScale) break;

        const double app = matrix(p, p);
        const double aqq = matrix(q, q);
        const double apq = matrix(p, q);
        if (apq == 0.0) continue;
        const double tau = (aqq - app) / (2.0 * apq);
        const double t = (tau >= 0.0 ? 1.0 : -1.0) /
                         (std::abs(tau) + std::sqrt(1.0 + tau * tau));
        const double c = 1.0 / std::sqrt(1.0 + t * t);
        const double s = t * c;

        for (std::size_t k = 0; k < n; ++k) {
            if (k == p || k == q) continue;
            const double mkp = matrix(k, p);
            const double mkq = matrix(k, q);
            const double newKp = c * mkp - s * mkq;
            const double newKq = s * mkp + c * mkq;
            matrix(k, p) = newKp;
            matrix(p, k) = newKp;
            matrix(k, q) = newKq;
            matrix(q, k) = newKq;
        }
        matrix(p, p) = c * c * app - 2.0 * s * c * apq + s * s * aqq;
        matrix(q, q) = s * s * app + 2.0 * s * c * apq + c * c * aqq;
        matrix(p, q) = 0.0;
        matrix(q, p) = 0.0;

        for (std::size_t k = 0; k < n; ++k) {
            const double vkp = vectors(k, p);
            const double vkq = vectors(k, q);
            vectors(k, p) = c * vkp - s * vkq;
            vectors(k, q) = s * vkp + c * vkq;
        }
    }

    std::vector<double> values(n, 0.0);
    for (std::size_t index = 0; index < n; ++index) values[index] = matrix(index, index);
    return {std::move(values), std::move(vectors)};
}

} // namespace

RealityLoopResult fitWorldHypothesis(WorldIR initialWorld,
                                     const std::vector<ParameterAddress>& parameters,
                                     const ForwardModel& forwardModel,
                                     const RealityLoopSettings& settings) {
    validateSettings(settings);
    requireUniqueParameters(parameters);
    const HypothesisValidation validation = initialWorld.validateHypothesis();
    if (!validation.valid) {
        throw std::invalid_argument("WorldIR hypothesis validation failed: " + validation.errors.front());
    }
    (void)parameterValues(initialWorld, parameters);

    WorldIR current = std::move(initialWorld);
    Evaluation currentEvaluation = evaluate(current, forwardModel, settings.fittingRoles);
    RealityLoopResult result;
    result.initialObjective = currentEvaluation.objective;
    double damping = settings.initialDamping;
    result.trace.push_back(
        {0, currentEvaluation.objective, damping, true, parameterValues(current, parameters)});

    if (currentEvaluation.objective <= settings.objectiveTolerance) result.converged = true;

    for (std::size_t iteration = 1; iteration <= settings.maxIterations && !result.converged; ++iteration) {
        const std::size_t residualCount = currentEvaluation.residuals.size();
        const std::size_t parameterCount = parameters.size();
        numerics::DenseMatrix jacobian(residualCount, parameterCount, 0.0);
        for (std::size_t columnIndex = 0; columnIndex < parameterCount; ++columnIndex) {
            const auto column = finiteDifferenceColumn(current,
                                                       parameters[columnIndex],
                                                       forwardModel,
                                                       settings.relativeFiniteDifferenceStep,
                                                       settings.absoluteFiniteDifferenceStep,
                                                       settings.fittingRoles);
            for (std::size_t row = 0; row < residualCount; ++row) {
                jacobian(row, columnIndex) = column.residualDerivative[row];
            }
        }

        numerics::DenseMatrix normal(parameterCount, parameterCount, 0.0);
        std::vector<double> rhs(parameterCount, 0.0);
        for (std::size_t row = 0; row < residualCount; ++row) {
            for (std::size_t col = 0; col < parameterCount; ++col) {
                const double j = jacobian(row, col);
                rhs[col] -= j * currentEvaluation.residuals[row];
                for (std::size_t other = 0; other < parameterCount; ++other) {
                    normal(col, other) += j * jacobian(row, other);
                }
            }
        }
        for (std::size_t diagonal = 0; diagonal < parameterCount; ++diagonal) {
            const double scale = std::max(normal(diagonal, diagonal), 1.0);
            normal(diagonal, diagonal) += damping * scale;
        }

        std::vector<double> delta;
        try {
            delta = numerics::solveGaussian(std::move(normal), std::move(rhs));
        } catch (const std::runtime_error&) {
            damping *= settings.dampingGrowth;
            result.trace.push_back({iteration, currentEvaluation.objective, damping, false,
                                    parameterValues(current, parameters)});
            continue;
        }

        const auto oldValues = parameterValues(current, parameters);
        bool accepted = false;
        double acceptedStepNorm = 0.0;
        double acceptedImprovement = 0.0;
        for (std::size_t backtrack = 0; backtrack < settings.maxBacktrackingSteps; ++backtrack) {
            const double scale = std::ldexp(1.0, -static_cast<int>(backtrack));
            WorldIR trial = current;
            double stepSquared = 0.0;
            for (std::size_t parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex) {
                const double candidate = trial.clampToBelief(
                    parameters[parameterIndex], oldValues[parameterIndex] + scale * delta[parameterIndex]);
                if (!trial.setParameterValue(parameters[parameterIndex], candidate)) {
                    throw std::runtime_error("failed to apply reality-loop trial parameter");
                }
                const double actualStep = candidate - oldValues[parameterIndex];
                stepSquared += actualStep * actualStep;
            }
            const Evaluation trialEvaluation = evaluate(trial, forwardModel, settings.fittingRoles);
            if (trialEvaluation.objective < currentEvaluation.objective) {
                acceptedStepNorm = std::sqrt(stepSquared);
                acceptedImprovement = currentEvaluation.objective - trialEvaluation.objective;
                current = std::move(trial);
                currentEvaluation = trialEvaluation;
                accepted = true;
                break;
            }
        }

        if (accepted) {
            damping = std::max(damping * settings.dampingShrink,
                               std::numeric_limits<double>::epsilon());
            result.trace.push_back({iteration, currentEvaluation.objective, damping, true,
                                    parameterValues(current, parameters)});
            const double improvementScale = std::max(result.trace[result.trace.size() - 2].objective, 1.0);
            if (currentEvaluation.objective <= settings.objectiveTolerance ||
                acceptedStepNorm <= settings.parameterStepTolerance ||
                acceptedImprovement <= settings.relativeImprovementTolerance * improvementScale) {
                result.converged = true;
            }
        } else {
            damping *= settings.dampingGrowth;
            result.trace.push_back({iteration, currentEvaluation.objective, damping, false,
                                    parameterValues(current, parameters)});
        }
    }

    result.world = std::move(current);
    result.finalObjective = currentEvaluation.objective;
    result.residual = currentEvaluation.summary;
    if (worldHasRole(result.world, ObservationRole::Validation)) {
        const Evaluation validationEvaluation =
            evaluate(result.world, forwardModel, {ObservationRole::Validation});
        result.validationObjective = validationEvaluation.objective;
        result.validationResidual = validationEvaluation.summary;
    }
    return result;
}

std::vector<ParameterSensitivity> rankParameterSensitivity(
    const WorldIR& world,
    const std::vector<ParameterAddress>& parameters,
    const ForwardModel& forwardModel,
    double relativeStep,
    double absoluteStep) {
    requireUniqueParameters(parameters);
    if (!(relativeStep > 0.0) || !(absoluteStep > 0.0)) {
        throw std::invalid_argument("sensitivity finite-difference steps must be positive");
    }
    const HypothesisValidation validation = world.validateHypothesis();
    if (!validation.valid) {
        throw std::invalid_argument("WorldIR hypothesis validation failed: " + validation.errors.front());
    }

    std::vector<ParameterSensitivity> sensitivities;
    sensitivities.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        const auto column = finiteDifferenceColumn(world, parameter, forwardModel, relativeStep, absoluteStep,
                                                   {ObservationRole::Fit});
        sensitivities.push_back(
            {parameter, column.objectiveDerivative, numerics::l2Norm(column.residualDerivative)});
    }
    std::stable_sort(sensitivities.begin(), sensitivities.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.weightedPredictionL2Derivative != rhs.weightedPredictionL2Derivative) {
            return lhs.weightedPredictionL2Derivative > rhs.weightedPredictionL2Derivative;
        }
        return std::abs(lhs.objectiveDerivative) > std::abs(rhs.objectiveDerivative);
    });
    return sensitivities;
}

LocalIdentifiabilityReport analyzeLocalIdentifiability(
    const WorldIR& world,
    const std::vector<ParameterAddress>& parameters,
    const ForwardModel& forwardModel,
    const IdentifiabilitySettings& settings) {
    requireUniqueParameters(parameters);
    validateIdentifiabilitySettings(settings);
    const HypothesisValidation validation = world.validateHypothesis();
    if (!validation.valid) {
        throw std::invalid_argument("WorldIR hypothesis validation failed: " + validation.errors.front());
    }

    const Evaluation base = evaluate(world, forwardModel, settings.roles);
    const std::size_t residualCount = base.residuals.size();
    const std::size_t parameterCount = parameters.size();
    numerics::DenseMatrix jacobian(residualCount, parameterCount, 0.0);
    std::vector<double> parameterScales(parameterCount, 1.0);
    for (std::size_t columnIndex = 0; columnIndex < parameterCount; ++columnIndex) {
        parameterScales[columnIndex] = identifiabilityParameterScale(world, parameters[columnIndex]);
        const auto column = finiteDifferenceColumn(
            world, parameters[columnIndex], forwardModel,
            settings.relativeFiniteDifferenceStep, settings.absoluteFiniteDifferenceStep,
            settings.roles);
        for (std::size_t row = 0; row < residualCount; ++row) {
            jacobian(row, columnIndex) =
                column.residualDerivative[row] * parameterScales[columnIndex];
        }
    }

    numerics::DenseMatrix gram(parameterCount, parameterCount, 0.0);
    for (std::size_t row = 0; row < residualCount; ++row) {
        for (std::size_t first = 0; first < parameterCount; ++first) {
            for (std::size_t second = first; second < parameterCount; ++second) {
                gram(first, second) += jacobian(row, first) * jacobian(row, second);
            }
        }
    }
    for (std::size_t first = 0; first < parameterCount; ++first) {
        for (std::size_t second = first + 1U; second < parameterCount; ++second) {
            gram(second, first) = gram(first, second);
        }
    }

    auto eigen = symmetricEigenJacobi(std::move(gram));
    std::vector<std::size_t> order(parameterCount);
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
        return eigen.values[lhs] > eigen.values[rhs];
    });

    LocalIdentifiabilityReport report;
    report.parameterOrder = parameters;
    report.parameterScales = std::move(parameterScales);
    report.scalarObservationCount = residualCount;
    report.singularValuesDescending.reserve(parameterCount);
    for (const std::size_t index : order) {
        const double eigenvalue = std::max(eigen.values[index], 0.0);
        report.singularValuesDescending.push_back(std::sqrt(eigenvalue));
    }
    report.largestSingularValue = report.singularValuesDescending.empty()
                                      ? 0.0
                                      : report.singularValuesDescending.front();
    const double threshold = std::max(
        settings.absoluteRankTolerance,
        settings.relativeRankTolerance * report.largestSingularValue);
    for (const double singularValue : report.singularValuesDescending) {
        if (singularValue > threshold) ++report.numericalRank;
    }
    report.locallyIdentifiable = report.numericalRank == parameterCount;
    if (report.numericalRank > 0U) {
        report.smallestResolvedSingularValue =
            report.singularValuesDescending[report.numericalRank - 1U];
    }
    if (report.locallyIdentifiable && report.smallestResolvedSingularValue > 0.0) {
        report.conditionNumber =
            report.largestSingularValue / report.smallestResolvedSingularValue;
    } else {
        report.conditionNumber = std::numeric_limits<double>::infinity();
    }

    for (std::size_t sortedIndex = report.numericalRank;
         sortedIndex < parameterCount; ++sortedIndex) {
        const std::size_t eigenIndex = order[sortedIndex];
        WeakParameterCombination weak;
        weak.singularValue = report.singularValuesDescending[sortedIndex];
        weak.normalizedCoefficients.resize(parameterCount, 0.0);
        std::size_t largestComponent = 0U;
        double largestMagnitude = 0.0;
        for (std::size_t parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex) {
            const double coefficient = eigen.vectors(parameterIndex, eigenIndex);
            weak.normalizedCoefficients[parameterIndex] = coefficient;
            if (std::abs(coefficient) > largestMagnitude) {
                largestMagnitude = std::abs(coefficient);
                largestComponent = parameterIndex;
            }
        }
        if (!weak.normalizedCoefficients.empty() &&
            weak.normalizedCoefficients[largestComponent] < 0.0) {
            for (double& coefficient : weak.normalizedCoefficients) coefficient = -coefficient;
        }
        report.weakCombinations.push_back(std::move(weak));
    }
    return report;
}

} // namespace vulkax::world
