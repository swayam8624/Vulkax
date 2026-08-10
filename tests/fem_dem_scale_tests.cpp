#include "vulkax/solvers/dem_broadphase.hpp"
#include "vulkax/solvers/fem.hpp"

#include <cassert>
#include <cmath>
#include <vector>

int main() {
    using namespace vulkax;

    std::vector<solvers::FemNode> nodes(4);
    nodes[0].position = {0.0, 0.0, 0.0};
    nodes[1].position = {1.0, 0.0, 0.0};
    nodes[2].position = {0.0, 1.0, 0.0};
    nodes[3].position = {0.0, 0.0, 1.0};
    nodes[0].fixed = {true, true, true};
    nodes[1].fixed = {true, true, true};
    nodes[2].fixed = {true, true, true};
    nodes[3].force = {0.0, 0.0, -1000.0};
    const auto fem = solvers::solveLinearTetrahedralElasticity(nodes, {{{0, 1, 2, 3}}}, {2.0e6, 0.3});
    assert(fem.displacement.size() == 4);
    assert(fem.displacement[3].z < 0.0);
    assert(fem.vonMisesStress.size() == 1);
    assert(fem.vonMisesStress[0] > 0.0);
    assert(fem.strainEnergy > 0.0);

    std::vector<solvers::DemParticle> particles;
    const int side = 8;
    for (int z = 0; z < side; ++z) {
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                particles.push_back({{-0.7 + 0.18 * x, 0.25 + 0.18 * y, -0.7 + 0.18 * z},
                                     {0.0, 0.0, 0.0}, 0.07, 1.0});
            }
        }
    }
    const std::size_t n = particles.size();
    const auto stats = solvers::advanceDemSpatialHash(
        particles, {{-1.0, 0.0, -1.0}, {1.0, 2.0, 1.0}},
        {1.0e-4, {0.0, -9.80665, 0.0}, 1.0e4, 10.0, 0.3, 0.4}, 2);
    assert(stats.candidatePairs < n * (n - 1) / 4);
    for (const auto& particle : particles) {
        assert(std::isfinite(particle.position.x));
        assert(particle.position.y >= particle.radius - 1.0e-12);
    }
    return 0;
}
