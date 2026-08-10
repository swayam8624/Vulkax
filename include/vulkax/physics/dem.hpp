#pragma once

#include "vulkax/field/field.hpp"

#include <cstddef>
#include <cstdint>

namespace vulkax::physics::dem {

struct Material {
    double normalStiffness{2.0e5};
    double normalDamping{80.0};
    double tangentialDamping{30.0};
    double friction{0.5};
};

struct RotatingDrum {
    double radius{1.0};
    double halfHeight{0.5};
    double angularVelocity{0.0};
    Material wallMaterial{};
};

struct Settings {
    field::Vec3 gravity{0.0, -9.81, 0.0};
    Material particleMaterial{};
    RotatingDrum drum{};
};

struct Statistics {
    std::size_t particleContacts{};
    std::size_t wallContacts{};
    double kineticEnergy{};
    double maximumSpeed{};
    double dissipatedPower{};
};

class Solver {
public:
    Solver(field::ParticleSet particles, Settings settings = {});
    void step(double dt);
    [[nodiscard]] const field::ParticleSet& particles() const noexcept { return particles_; }
    [[nodiscard]] const Statistics& statistics() const noexcept { return statistics_; }

private:
    field::ParticleSet particles_;
    Settings settings_;
    Statistics statistics_{};
};

} // namespace vulkax::physics::dem
