#include "vulkax/research/local_operator_influence.hpp"

#include <cassert>
#include <cmath>
#include <vector>

namespace {

vulkax::numerics::DenseMatrix localMass(std::size_t n, std::size_t i) {
    vulkax::numerics::DenseMatrix matrix(n, n);
    matrix(i, i) = 1.0;
    return matrix;
}

vulkax::numerics::DenseMatrix localEdgeDiffusion(std::size_t n, std::size_t i,
                                                  std::size_t j) {
    vulkax::numerics::DenseMatrix matrix(n, n);
    matrix(i, i) += 1.0;
    matrix(j, j) += 1.0;
    matrix(i, j) -= 1.0;
    matrix(j, i) -= 1.0;
    return matrix;
}

} // namespace

int main() {
    using namespace vulkax::research;
    constexpr std::size_t n = 20;
    LocalInfluenceProblem problem;
    problem.rhs.assign(n, 0.0);
    problem.objective.assign(n, 0.0);
    problem.rhs[3] = 1.0;
    problem.rhs[4] = 0.4;
    problem.objective[15] = 1.0;

    LocalOperatorTerm reaction;
    reaction.id = "reaction";
    for (std::size_t i = 0; i < n; ++i) {
        reaction.localMatrices.push_back(localMass(n, i));
        reaction.scales.push_back(0.8);
    }

    LocalOperatorTerm diffusion;
    diffusion.id = "diffusion";
    for (std::size_t i = 0; i + 1 < n; ++i) {
        diffusion.localMatrices.push_back(localEdgeDiffusion(n, i, i + 1));
        diffusion.scales.push_back(1.5);
    }
    // Add weak periodic closure so the operator is spatially connected.
    diffusion.localMatrices.push_back(localEdgeDiffusion(n, n - 1, 0));
    diffusion.scales.push_back(1.5);

    problem.terms.push_back(std::move(reaction));
    problem.terms.push_back(std::move(diffusion));

    const auto influence = computeLocalOperatorInfluence(problem);
    assert(influence.fields.size() == 2);
    assert(influence.fields[0].derivative.size() == n);
    assert(influence.fields[1].derivative.size() == n);
    for (const auto& field : influence.fields) {
        for (double value : field.derivative) assert(std::isfinite(value));
    }

    // A small painted intervention over multiple cells/mechanisms. The adjoint field predicts the
    // final-observable change without another solve; the full modified solve is used only to verify it.
    const std::vector<LocalIntervention> intervention = {
        {0, 11, 1.0e-4}, {0, 12, -8.0e-5}, {1, 10, 1.2e-4},
        {1, 11, 1.0e-4}, {1, 12, -7.0e-5},
    };
    const auto verification = verifyLocalCounterfactual(problem, influence, intervention);
    assert(std::isfinite(verification.actualObservable));
    assert(verification.relativePredictionError < 5.0e-4);

    // A location near the forcing should not have the same influence as a far-away location;
    // this guards against accidentally collapsing the field back to a global coefficient.
    assert(std::abs(influence.fields[0].derivative[3] -
                    influence.fields[0].derivative[10]) > 1.0e-8);
    return 0;
}
