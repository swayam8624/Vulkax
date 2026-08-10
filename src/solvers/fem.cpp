#include "vulkax/solvers/fem.hpp"

#include "vulkax/numerics/dense.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace vulkax::solvers {

namespace {

double determinant3(math::Vec3 a, math::Vec3 b, math::Vec3 c) {
    return math::dot(a, math::cross(b, c));
}

struct ElementData {
    double volume{};
    numerics::DenseMatrix b{6, 12};
    numerics::DenseMatrix stiffness{12, 12};
};

numerics::DenseMatrix elasticityMatrix(LinearElasticMaterial material) {
    if (material.youngModulus <= 0.0 || material.poissonRatio <= -1.0 || material.poissonRatio >= 0.5) {
        throw std::invalid_argument("linear elastic material requires E>0 and -1<nu<0.5");
    }
    const double mu = material.youngModulus / (2.0 * (1.0 + material.poissonRatio));
    const double lambda = material.youngModulus * material.poissonRatio /
                          ((1.0 + material.poissonRatio) * (1.0 - 2.0 * material.poissonRatio));
    numerics::DenseMatrix c(6, 6);
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) c(row, col) = lambda;
        c(row, row) += 2.0 * mu;
    }
    c(3, 3) = mu;
    c(4, 4) = mu;
    c(5, 5) = mu;
    return c;
}

ElementData buildElement(const std::vector<FemNode>& nodes, const Tetrahedron& element,
                         const numerics::DenseMatrix& constitutive) {
    std::array<math::Vec3, 4> p{};
    for (std::size_t i = 0; i < 4; ++i) {
        if (element.node[i] >= nodes.size()) throw std::invalid_argument("tetrahedron references invalid node");
        p[i] = nodes[element.node[i]].position;
    }
    const double signedSixVolume = determinant3(p[1] - p[0], p[2] - p[0], p[3] - p[0]);
    const double volume = std::abs(signedSixVolume) / 6.0;
    if (volume <= 1.0e-15) throw std::invalid_argument("degenerate tetrahedral element");

    numerics::DenseMatrix interpolation(4, 4);
    for (std::size_t row = 0; row < 4; ++row) {
        interpolation(row, 0) = 1.0;
        interpolation(row, 1) = p[row].x;
        interpolation(row, 2) = p[row].y;
        interpolation(row, 3) = p[row].z;
    }

    std::array<math::Vec3, 4> gradient{};
    for (std::size_t shape = 0; shape < 4; ++shape) {
        std::vector<double> rhs(4, 0.0);
        rhs[shape] = 1.0;
        const auto coefficients = numerics::solveGaussian(interpolation, rhs, 1.0e-15);
        gradient[shape] = {coefficients[1], coefficients[2], coefficients[3]};
    }

    ElementData data;
    data.volume = volume;
    for (std::size_t node = 0; node < 4; ++node) {
        const std::size_t col = node * 3;
        const double gx = gradient[node].x;
        const double gy = gradient[node].y;
        const double gz = gradient[node].z;
        data.b(0, col) = gx;
        data.b(1, col + 1) = gy;
        data.b(2, col + 2) = gz;
        data.b(3, col) = gy; data.b(3, col + 1) = gx;
        data.b(4, col + 1) = gz; data.b(4, col + 2) = gy;
        data.b(5, col) = gz; data.b(5, col + 2) = gx;
    }

    for (std::size_t i = 0; i < 12; ++i) {
        for (std::size_t j = 0; j < 12; ++j) {
            double value = 0.0;
            for (std::size_t alpha = 0; alpha < 6; ++alpha) {
                for (std::size_t beta = 0; beta < 6; ++beta) {
                    value += data.b(alpha, i) * constitutive(alpha, beta) * data.b(beta, j);
                }
            }
            data.stiffness(i, j) = volume * value;
        }
    }
    return data;
}

double vonMises(const std::vector<double>& stress) {
    const double sx = stress[0], sy = stress[1], sz = stress[2];
    const double txy = stress[3], tyz = stress[4], tzx = stress[5];
    return std::sqrt(0.5 * ((sx - sy) * (sx - sy) + (sy - sz) * (sy - sz) +
                            (sz - sx) * (sz - sx)) +
                     3.0 * (txy * txy + tyz * tyz + tzx * tzx));
}

} // namespace

LinearElasticResult solveLinearTetrahedralElasticity(const std::vector<FemNode>& nodes,
                                                      const std::vector<Tetrahedron>& elements,
                                                      LinearElasticMaterial material) {
    if (nodes.empty() || elements.empty()) throw std::invalid_argument("FEM mesh cannot be empty");
    const auto constitutive = elasticityMatrix(material);
    const std::size_t dofCount = nodes.size() * 3;
    numerics::DenseMatrix global(dofCount, dofCount);
    std::vector<double> rhs(dofCount, 0.0);
    std::vector<ElementData> elementData;
    elementData.reserve(elements.size());

    for (std::size_t node = 0; node < nodes.size(); ++node) {
        rhs[node * 3] = nodes[node].force.x;
        rhs[node * 3 + 1] = nodes[node].force.y;
        rhs[node * 3 + 2] = nodes[node].force.z;
    }
    for (const auto& element : elements) {
        auto data = buildElement(nodes, element, constitutive);
        for (std::size_t localI = 0; localI < 12; ++localI) {
            const std::size_t nodeI = element.node[localI / 3];
            const std::size_t globalI = nodeI * 3 + localI % 3;
            for (std::size_t localJ = 0; localJ < 12; ++localJ) {
                const std::size_t nodeJ = element.node[localJ / 3];
                const std::size_t globalJ = nodeJ * 3 + localJ % 3;
                global(globalI, globalJ) += data.stiffness(localI, localJ);
            }
        }
        elementData.push_back(std::move(data));
    }

    for (std::size_t node = 0; node < nodes.size(); ++node) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!nodes[node].fixed[axis]) continue;
            const std::size_t dof = node * 3 + axis;
            for (std::size_t col = 0; col < dofCount; ++col) global(dof, col) = 0.0;
            for (std::size_t row = 0; row < dofCount; ++row) global(row, dof) = 0.0;
            global(dof, dof) = 1.0;
            rhs[dof] = 0.0;
        }
    }

    const auto solution = numerics::solveGaussian(global, rhs, 1.0e-14);
    LinearElasticResult result;
    result.displacement.resize(nodes.size());
    for (std::size_t node = 0; node < nodes.size(); ++node) {
        result.displacement[node] = {solution[node * 3], solution[node * 3 + 1], solution[node * 3 + 2]};
    }

    result.vonMisesStress.reserve(elements.size());
    for (std::size_t elementIndex = 0; elementIndex < elements.size(); ++elementIndex) {
        const auto& element = elements[elementIndex];
        const auto& data = elementData[elementIndex];
        std::vector<double> local(12, 0.0);
        for (std::size_t localDof = 0; localDof < 12; ++localDof) {
            const std::size_t globalNode = element.node[localDof / 3];
            local[localDof] = solution[globalNode * 3 + localDof % 3];
        }
        const auto strain = data.b.multiply(local);
        const auto stress = constitutive.multiply(strain);
        result.vonMisesStress.push_back(vonMises(stress));
        const auto force = data.stiffness.multiply(local);
        result.strainEnergy += 0.5 * numerics::dot(local, force);
    }
    return result;
}

} // namespace vulkax::solvers
