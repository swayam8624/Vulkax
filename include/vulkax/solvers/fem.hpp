#pragma once

#include "vulkax/core/math.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace vulkax::solvers {

struct FemNode {
    math::Vec3 position{};
    math::Vec3 force{};
    std::array<bool, 3> fixed{false, false, false};
};

struct Tetrahedron {
    std::array<std::size_t, 4> node{};
};

struct LinearElasticMaterial {
    double youngModulus{1.0e6};
    double poissonRatio{0.3};
};

struct LinearElasticResult {
    std::vector<math::Vec3> displacement;
    std::vector<double> vonMisesStress;
    double strainEnergy{};
};

[[nodiscard]] LinearElasticResult solveLinearTetrahedralElasticity(
    const std::vector<FemNode>& nodes, const std::vector<Tetrahedron>& elements,
    LinearElasticMaterial material);

} // namespace vulkax::solvers
