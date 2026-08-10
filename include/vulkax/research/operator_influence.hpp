#pragma once

#include "vulkax/numerics/dense.hpp"

#include <string>
#include <vector>

namespace vulkax::research {

// Linear prototype of the Operator Influence Field formulation. A system matrix is decomposed
// into named physical mechanisms A(alpha) = sum_k alpha_k A_k. For an observable J = c^T u,
// the adjoint lambda solves A^T lambda = c and dJ/dalpha_k = -lambda^T A_k u.
struct LinearOperatorTerm {
    std::string id;
    numerics::DenseMatrix matrix;
    double scale{1.0};
};

struct LinearInfluenceProblem {
    std::vector<LinearOperatorTerm> terms;
    std::vector<double> rhs;
    std::vector<double> objective;
};

struct OperatorInfluence {
    std::string id;
    double derivative{};
    double finiteDifferenceDerivative{};
    double relativeVerificationError{};
};

struct OperatorInfluenceResult {
    std::vector<double> state;
    std::vector<double> adjoint;
    double observable{};
    std::vector<OperatorInfluence> influences;
};

[[nodiscard]] OperatorInfluenceResult computeOperatorInfluence(
    const LinearInfluenceProblem& problem, double finiteDifferenceStep = 1.0e-6);

} // namespace vulkax::research
