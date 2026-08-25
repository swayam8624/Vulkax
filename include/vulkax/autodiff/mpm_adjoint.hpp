#pragma once

#include "vulkax/solvers/mpm.hpp"

#include <cstddef>
#include <vector>

namespace vulkax::autodiff {

struct MpmMaterialScaleAdjointResult {
    double objective{};
    std::vector<double> particleScaleGradient;
    double minimumStencilKnotMargin{1.0};
};

// Reverse-mode derivative for the free-relaxation APIC/MPM path used by the
// captured-deformable benchmark. The objective is the projection of one
// particle's displacement over the requested number of steps. The returned
// gradient is dJ/ds_p for every particle-local Young's-modulus multiplier.
//
// Current scope is deliberately explicit: APIC, zero gravity/external forces,
// and boundaryCells == 0. This matches the captured-material reference path and
// avoids claiming derivatives through boundary clamps or FLIP branches before
// those reverse rules are implemented and verified.
[[nodiscard]] MpmMaterialScaleAdjointResult differentiateMpmApicMaterialScales(
    std::vector<solvers::MpmParticle> initialParticles,
    const solvers::MpmGridSettings& grid,
    const solvers::MpmMaterial& material,
    double dt,
    std::size_t steps,
    std::size_t objectiveParticleIndex,
    math::Vec3 objectiveDirection);

} // namespace vulkax::autodiff
