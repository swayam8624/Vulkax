#pragma once

#include "vulkax/solvers/dem.hpp"

#include <cstddef>
#include <vector>

namespace vulkax::solvers {

struct DemBroadphaseStats {
    std::size_t candidatePairs{};
    std::size_t contacts{};
};

// Uniform spatial hashing is the CPU reference for the GPU sort/scan broad phase. It guarantees
// that each candidate pair is emitted at most once and only searches the 27 neighboring cells.
[[nodiscard]] DemBroadphaseStats advanceDemSpatialHash(std::vector<DemParticle>& particles,
                                                       const DemBox& box,
                                                       const DemConfig& config,
                                                       std::size_t steps = 1);

} // namespace vulkax::solvers
