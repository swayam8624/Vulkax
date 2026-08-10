#pragma once

#include "vulkax/solvers/dem.hpp"

#include <cstddef>
#include <vector>

namespace vulkax::solvers {

struct RotatingDrum {
    double radius{1.0};
    double halfLength{0.5};
    double angularVelocity{1.0}; // radians / second around +Z
};

struct DrumDiagnostics {
    std::size_t wallCollisions{};
    std::size_t particleContacts{};
    std::size_t broadphaseCandidates{};
    double wallImpactEnergy{};
    double wallEnergyTransfer{};
    double meanSpeed{};
    double meanTangentialVelocity{};
};

[[nodiscard]] DrumDiagnostics advanceRotatingDrum(std::vector<DemParticle>& particles,
                                                  const RotatingDrum& drum,
                                                  const DemConfig& config,
                                                  std::size_t steps);

} // namespace vulkax::solvers
