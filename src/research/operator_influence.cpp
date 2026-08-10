#include "vulkax/research/operator_influence.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::research {

namespace {

numerics::DenseMatrix assemble(const LinearInfluenceProblem& problem,
                               const std::vector<double>& scales) {
    if (problem.terms.empty() || scales.size() != problem.terms.size()) {
        throw std::invalid_argument("operator influence problem requires terms and matching scales");
    }
    const std::size_t n = problem.rhs.size();
    numerics::DenseMatrix result(n, n);
    for (std::size_t termIndex = 0; termIndex < problem.terms.size(); ++termIndex) {
        const auto& term = problem.terms[termIndex];
        if (term.matrix.rows() != n || term.matrix.cols() != n) {
            throw std::invalid_argument("operator matrices must match the system size");
        }
        for (std::size_t row = 0; row < n; ++row) {
            for (std::size_t col = 0; col < n; ++col) {
                result(row, col) += scales[termIndex] * term.matrix(row, col);
            }
        }
    }
    return result;
}

double observableForScales(const LinearInfluenceProblem& problem,
                           const std::vector<double>& scales) {
    const auto state = numerics::solveGaussian(assemble(problem, scales), problem.rhs);
    return numerics::dot(problem.objective, state);
}

} // namespace

OperatorInfluenceResult computeOperatorInfluence(const LinearInfluenceProblem& problem,
                                                 double finiteDifferenceStep) {
    if (problem.rhs.empty() || problem.objective.size() != problem.rhs.size() ||
        finiteDifferenceStep <= 0.0) {
        throw std::invalid_argument("invalid operator influence problem");
    }

    std::vector<double> scales;
    scales.reserve(problem.terms.size());
    for (const auto& term : problem.terms) {
        if (term.id.empty()) {
            throw std::invalid_argument("operator influence terms require stable ids");
        }
        scales.push_back(term.scale);
    }

    const numerics::DenseMatrix system = assemble(problem, scales);
    OperatorInfluenceResult result;
    result.state = numerics::solveGaussian(system, problem.rhs);
    result.observable = numerics::dot(problem.objective, result.state);
    result.adjoint = numerics::solveGaussian(system.transposed(), problem.objective);

    for (std::size_t index = 0; index < problem.terms.size(); ++index) {
        const auto action = problem.terms[index].matrix.multiply(result.state);
        const double derivative = -numerics::dot(result.adjoint, action);

        auto plus = scales;
        auto minus = scales;
        const double step = finiteDifferenceStep * std::max(1.0, std::abs(scales[index]));
        plus[index] += step;
        minus[index] -= step;
        const double finiteDifference =
            (observableForScales(problem, plus) - observableForScales(problem, minus)) /
            (2.0 * step);
        const double denominator = std::max({1.0e-12, std::abs(derivative), std::abs(finiteDifference)});
        result.influences.push_back({problem.terms[index].id,
                                     derivative,
                                     finiteDifference,
                                     std::abs(derivative - finiteDifference) / denominator});
    }
    return result;
}

} // namespace vulkax::research
