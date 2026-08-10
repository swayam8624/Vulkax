#include "vulkax/solvers/diffusion.hpp"

#include <stdexcept>

namespace vulkax::solvers {

double explicitDiffusionStabilityLimit(const numerics::ScalarGrid3D& grid, double diffusivity) {
    if (diffusivity < 0.0) {
        throw std::invalid_argument("diffusivity cannot be negative");
    }
    if (diffusivity == 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    const auto h = grid.spacing();
    const double inverseSquaredSum = 1.0 / (h.x * h.x) + 1.0 / (h.y * h.y) +
                                     1.0 / (h.z * h.z);
    return 1.0 / (2.0 * diffusivity * inverseSquaredSum);
}

void advanceDiffusion(numerics::ScalarGrid3D& grid, const DiffusionConfig& config) {
    if (config.dt <= 0.0 || config.stabilitySafety <= 0.0 || config.stabilitySafety > 1.0) {
        throw std::invalid_argument("invalid diffusion timestep or stability safety");
    }
    const double limit = explicitDiffusionStabilityLimit(grid, config.diffusivity);
    if (config.dt > config.stabilitySafety * limit) {
        throw std::invalid_argument("explicit diffusion timestep exceeds stability limit");
    }

    numerics::ScalarGrid3D next = grid;
    for (std::size_t step = 0; step < config.steps; ++step) {
        for (std::size_t z = 0; z < grid.nz(); ++z) {
            for (std::size_t y = 0; y < grid.ny(); ++y) {
                for (std::size_t x = 0; x < grid.nx(); ++x) {
                    next.at(x, y, z) = grid.at(x, y, z) +
                                       config.dt * config.diffusivity *
                                           numerics::laplacianAt(grid, x, y, z, config.boundary);
                }
            }
        }
        grid = next;
    }
}

} // namespace vulkax::solvers
