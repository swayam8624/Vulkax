#include "vulkax/numerics/dense.hpp"
#include "vulkax/numerics/grid.hpp"
#include "vulkax/solvers/dem.hpp"
#include "vulkax/solvers/diffusion.hpp"
#include "vulkax/verify/convergence.hpp"

#include <cassert>
#include <cmath>
#include <stdexcept>
#include <vector>

int main() {
    using namespace vulkax;

    numerics::DenseMatrix matrix(2, 2);
    matrix(0, 0) = 3.0;
    matrix(0, 1) = 1.0;
    matrix(1, 0) = 1.0;
    matrix(1, 1) = 2.0;
    const auto solution = numerics::solveGaussian(matrix, {9.0, 8.0});
    assert(std::abs(solution[0] - 2.0) < 1.0e-12);
    assert(std::abs(solution[1] - 3.0) < 1.0e-12);

    numerics::ScalarGrid3D grid(12, 10, 8, {0.1, 0.1, 0.1}, 2.0);
    const double initialIntegral = numerics::integral(grid);
    const double limit = solvers::explicitDiffusionStabilityLimit(grid, 0.2);
    solvers::advanceDiffusion(grid, {0.2, 0.5 * limit, 20, numerics::BoundaryMode::Periodic, 0.95});
    for (double value : grid.values()) {
        assert(std::abs(value - 2.0) < 1.0e-12);
    }
    assert(std::abs(numerics::integral(grid) - initialIntegral) < 1.0e-12);

    bool rejectedUnstable = false;
    try {
        solvers::advanceDiffusion(grid, {0.2, 2.0 * limit, 1, numerics::BoundaryMode::Periodic, 0.95});
    } catch (const std::invalid_argument&) {
        rejectedUnstable = true;
    }
    assert(rejectedUnstable);

    std::vector<solvers::DemParticle> particles = {
        {{-0.05, 0.5, 0.0}, {0.4, 0.0, 0.0}, 0.08, 1.0},
        {{0.05, 0.5, 0.0}, {-0.4, 0.0, 0.0}, 0.08, 1.0},
    };
    const solvers::DemBox box{{-1.0, 0.0, -1.0}, {1.0, 2.0, 1.0}};
    const solvers::DemConfig demConfig{1.0e-4, {0.0, -9.80665, 0.0}, 2.0e4, 20.0, 0.3, 0.5};
    const auto before = solvers::measureDem(particles, box, demConfig);
    assert(before.contacts == 1);
    solvers::advanceDem(particles, box, demConfig, 100);
    const auto after = solvers::measureDem(particles, box, demConfig);
    assert(std::isfinite(after.kineticEnergy));
    assert(std::isfinite(after.potentialEnergy));
    for (const auto& particle : particles) {
        assert(particle.position.y >= box.minimum.y + particle.radius - 1.0e-12);
    }

    const auto convergence = verify::estimateThreeLevelConvergence(1.04, 1.01, 1.0025, 2.0);
    assert(std::abs(convergence.observedOrder - 2.0) < 1.0e-10);
    assert(convergence.gridConvergenceIndex > 0.0);
    return 0;
}
