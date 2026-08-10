#pragma once

#include "vulkax/numerics/grid.hpp"

#include <cstddef>

namespace vulkax::solvers {

struct DiffusionConfig {
    double diffusivity{1.0};
    double dt{1.0e-3};
    std::size_t steps{1};
    numerics::BoundaryMode boundary{numerics::BoundaryMode::Periodic};
    double stabilitySafety{0.95};
};

[[nodiscard]] double explicitDiffusionStabilityLimit(const numerics::ScalarGrid3D& grid,
                                                      double diffusivity);
void advanceDiffusion(numerics::ScalarGrid3D& grid, const DiffusionConfig& config);

} // namespace vulkax::solvers
