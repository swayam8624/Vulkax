#pragma once

#include "vulkax/core/math.hpp"

#include <cstddef>
#include <vector>

namespace vulkax::solvers {

struct DemParticle {
    math::Vec3 position{};
    math::Vec3 velocity{};
    double radius{0.01};
    double mass{1.0};
};

struct DemBox {
    math::Vec3 minimum{-1.0, -1.0, -1.0};
    math::Vec3 maximum{1.0, 1.0, 1.0};
};

struct DemConfig {
    double dt{1.0e-4};
    math::Vec3 gravity{0.0, -9.80665, 0.0};
    double normalStiffness{1.0e5};
    double normalDamping{25.0};
    double friction{0.4};
    double wallRestitution{0.4};
};

struct DemDiagnostics {
    std::size_t contacts{};
    double kineticEnergy{};
    double potentialEnergy{};
    double maximumOverlap{};
};

void advanceDem(std::vector<DemParticle>& particles, const DemBox& box, const DemConfig& config,
                std::size_t steps = 1);
[[nodiscard]] DemDiagnostics measureDem(const std::vector<DemParticle>& particles,
                                        const DemBox& box, const DemConfig& config);

} // namespace vulkax::solvers
