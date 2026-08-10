#include "vulkax/research/operator_influence.hpp"

#include <cassert>
#include <cmath>

int main() {
    using vulkax::numerics::DenseMatrix;
    using namespace vulkax::research;

    DenseMatrix diffusion(2, 2);
    diffusion(0, 0) = 2.0;
    diffusion(0, 1) = -1.0;
    diffusion(1, 0) = -1.0;
    diffusion(1, 1) = 2.0;

    DenseMatrix reaction = DenseMatrix::identity(2);

    LinearInfluenceProblem problem;
    problem.terms.push_back({"diffusion", diffusion, 1.5});
    problem.terms.push_back({"reaction", reaction, 0.8});
    problem.rhs = {1.0, 0.25};
    problem.objective = {0.0, 1.0};

    const auto result = computeOperatorInfluence(problem);
    assert(result.state.size() == 2);
    assert(result.adjoint.size() == 2);
    assert(result.influences.size() == 2);
    assert(std::isfinite(result.observable));
    for (const auto& influence : result.influences) {
        assert(std::isfinite(influence.derivative));
        assert(std::isfinite(influence.finiteDifferenceDerivative));
        assert(influence.relativeVerificationError < 1.0e-7);
    }
    return 0;
}
