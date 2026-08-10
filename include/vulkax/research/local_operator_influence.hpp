#pragma once

#include "vulkax/numerics/dense.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace vulkax::research {

// A physical mechanism is decomposed into local matrix contributions. For example, a diffusion
// operator can contribute one small stencil matrix per cell/edge. The intervention coefficient
// a[k][r] scales mechanism k in local region r.
struct LocalOperatorTerm {
    std::string id;
    std::vector<numerics::DenseMatrix> localMatrices;
    std::vector<double> scales;
};

struct LocalInfluenceProblem {
    std::vector<LocalOperatorTerm> terms;
    std::vector<double> rhs;
    std::vector<double> objective;
};

struct LocalInfluenceField {
    std::string operatorId;
    std::vector<double> derivative;
};

struct LocalInfluenceResult {
    std::vector<double> state;
    std::vector<double> adjoint;
    double observable{};
    std::vector<LocalInfluenceField> fields;
};

struct LocalIntervention {
    std::size_t operatorIndex{};
    std::size_t regionIndex{};
    double deltaScale{};
};

struct CounterfactualVerification {
    double baselineObservable{};
    double predictedObservable{};
    double actualObservable{};
    double absolutePredictionError{};
    double relativePredictionError{};
};

[[nodiscard]] LocalInfluenceResult computeLocalOperatorInfluence(const LocalInfluenceProblem& problem);
[[nodiscard]] CounterfactualVerification verifyLocalCounterfactual(
    const LocalInfluenceProblem& problem, const LocalInfluenceResult& influence,
    const std::vector<LocalIntervention>& interventions);

} // namespace vulkax::research
