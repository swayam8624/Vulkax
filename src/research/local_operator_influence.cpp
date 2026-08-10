#include "vulkax/research/local_operator_influence.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace vulkax::research {

namespace {

void validate(const LocalInfluenceProblem& problem) {
    const std::size_t n = problem.rhs.size();
    if (n == 0 || problem.objective.size() != n || problem.terms.empty()) {
        throw std::invalid_argument("local influence problem has invalid dimensions");
    }
    for (const auto& term : problem.terms) {
        if (term.id.empty() || term.localMatrices.empty() ||
            term.localMatrices.size() != term.scales.size()) {
            throw std::invalid_argument("local influence terms require ids, matrices, and scales");
        }
        for (const auto& matrix : term.localMatrices) {
            if (matrix.rows() != n || matrix.cols() != n) {
                throw std::invalid_argument("local operator matrix has wrong system dimension");
            }
        }
    }
}

numerics::DenseMatrix assemble(const LocalInfluenceProblem& problem) {
    const std::size_t n = problem.rhs.size();
    numerics::DenseMatrix result(n, n);
    for (const auto& term : problem.terms) {
        for (std::size_t region = 0; region < term.localMatrices.size(); ++region) {
            const auto& matrix = term.localMatrices[region];
            const double scale = term.scales[region];
            for (std::size_t row = 0; row < n; ++row) {
                for (std::size_t col = 0; col < n; ++col) {
                    result(row, col) += scale * matrix(row, col);
                }
            }
        }
    }
    return result;
}

} // namespace

LocalInfluenceResult computeLocalOperatorInfluence(const LocalInfluenceProblem& problem) {
    validate(problem);
    const auto system = assemble(problem);
    LocalInfluenceResult result;
    result.state = numerics::solveGaussian(system, problem.rhs);
    result.adjoint = numerics::solveGaussian(system.transposed(), problem.objective);
    result.observable = numerics::dot(problem.objective, result.state);
    result.fields.reserve(problem.terms.size());

    for (const auto& term : problem.terms) {
        LocalInfluenceField field;
        field.operatorId = term.id;
        field.derivative.reserve(term.localMatrices.size());
        for (const auto& localMatrix : term.localMatrices) {
            const auto action = localMatrix.multiply(result.state);
            field.derivative.push_back(-numerics::dot(result.adjoint, action));
        }
        result.fields.push_back(std::move(field));
    }
    return result;
}

CounterfactualVerification verifyLocalCounterfactual(
    const LocalInfluenceProblem& problem, const LocalInfluenceResult& influence,
    const std::vector<LocalIntervention>& interventions) {
    validate(problem);
    if (influence.fields.size() != problem.terms.size()) {
        throw std::invalid_argument("influence result does not match problem operator count");
    }
    LocalInfluenceProblem modified = problem;
    double predictedDelta = 0.0;
    for (const auto& intervention : interventions) {
        if (intervention.operatorIndex >= modified.terms.size() ||
            intervention.regionIndex >= modified.terms[intervention.operatorIndex].scales.size()) {
            throw std::out_of_range("local intervention index out of range");
        }
        modified.terms[intervention.operatorIndex].scales[intervention.regionIndex] +=
            intervention.deltaScale;
        predictedDelta +=
            influence.fields[intervention.operatorIndex].derivative[intervention.regionIndex] *
            intervention.deltaScale;
    }
    const auto actualState = numerics::solveGaussian(assemble(modified), modified.rhs);
    const double actual = numerics::dot(modified.objective, actualState);
    const double predicted = influence.observable + predictedDelta;
    const double absolute = std::abs(predicted - actual);
    const double scale = std::max({1.0e-12, std::abs(actual - influence.observable),
                                   std::abs(predictedDelta)});
    return {influence.observable, predicted, actual, absolute, absolute / scale};
}

} // namespace vulkax::research
