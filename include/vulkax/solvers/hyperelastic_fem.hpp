#pragma once

#include "vulkax/solvers/fem.hpp"

#include <cstddef>
#include <vector>

namespace vulkax::solvers {

struct NeoHookeanMaterial {
    double youngModulus{1.0e6};
    double poissonRatio{0.45};
};

struct NonlinearFemOptions {
    std::size_t maxNewtonIterations{30};
    double gradientTolerance{1.0e-6};
    double finiteDifferenceStep{1.0e-5};
    double minimumLineSearchScale{1.0e-5};
};

struct NonlinearFemResult {
    std::vector<math::Vec3> displacement;
    std::vector<double> vonMisesStress;
    double strainEnergy{};
    double totalPotential{};
    double finalGradientNorm{};
    std::size_t iterations{};
    bool converged{false};
};

[[nodiscard]] NonlinearFemResult solveNeoHookeanTetrahedral(
    const std::vector<FemNode>& nodes, const std::vector<Tetrahedron>& elements,
    NeoHookeanMaterial material, const NonlinearFemOptions& options = {});

} // namespace vulkax::solvers
