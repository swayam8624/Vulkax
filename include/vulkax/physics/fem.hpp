#pragma once

#include "vulkax/field/field.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace vulkax::physics::fem {

struct NeoHookeanMaterial {
    double density{1000.0};
    double shearModulus{1.0e6};
    double bulkModulus{1.0e8};
};

struct TetMesh {
    std::vector<field::Vec3> restPositions;
    std::vector<field::Vec3> positions;
    std::vector<field::Vec3> velocities;
    std::vector<std::array<std::uint32_t, 4>> tetrahedra;
    std::vector<bool> fixed;
};

struct StepStatistics {
    double elasticEnergy{};
    double kineticEnergy{};
    double maximumForce{};
    bool invertedElement{false};
};

class Solver {
public:
    Solver(TetMesh mesh, NeoHookeanMaterial material);
    void step(double dt, field::Vec3 gravity = {0.0, -9.81, 0.0});
    [[nodiscard]] const TetMesh& mesh() const noexcept { return mesh_; }
    [[nodiscard]] const StepStatistics& statistics() const noexcept { return statistics_; }
    [[nodiscard]] std::vector<field::Vec3> elasticForces() const;

private:
    TetMesh mesh_;
    NeoHookeanMaterial material_;
    std::vector<double> masses_;
    StepStatistics statistics_{};
};

struct UniaxialDatum { double stretch{1.0}; double nominalStress{}; double weight{1.0}; };
struct CalibrationResult { double shearModulus{}; double rmsError{}; double information{}; bool valid{false}; };
[[nodiscard]] CalibrationResult calibrateIncompressibleNeoHookean(const std::vector<UniaxialDatum>& data,
                                                                  double noiseStdDev = 1.0);

} // namespace vulkax::physics::fem
