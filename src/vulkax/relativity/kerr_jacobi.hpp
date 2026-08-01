#pragma once

#include <array>
#include <cstdint>

#include "vulkax/relativity/kerr_geodesic.hpp"

namespace vulkax::relativity {

struct KerrJacobiConfig {
  double affineStep = 0.01;
  double affineDistance = 24.0;
  double launchDifferential = 1e-5;
  uint32_t maximumSteps = 20000;
};

struct KerrJacobiResult {
  KerrRayStatus status = KerrRayStatus::Unfinished;
  std::array<double, 4> finalPosition{};
  std::array<double, 4> finalWaveVector{};
  std::array<double, 4> horizontalDeviation{};
  std::array<double, 4> verticalDeviation{};
  std::array<double, 4> horizontalCovariantDerivative{};
  std::array<double, 4> verticalCovariantDerivative{};
  std::array<double, 4> sourceJacobian{};
  double sourceAreaScale = 0.0;
  double maximumTidalAcceleration = 0.0;
  double maximumNullConstraint = 0.0;
  double integratedAffineDistance = 0.0;
  uint32_t acceptedSteps = 0;
  bool valid = false;
};

// Integrates two screen-basis Jacobi fields along one central null geodesic.
// After launch, the bundle obeys the covariant geodesic-deviation equation.
[[nodiscard]] KerrJacobiResult integrateKerrJacobiBundle(
    const KerrGeodesicConfig& geodesic,
    double alpha,
    double beta,
    const KerrJacobiConfig& jacobi = {});

[[nodiscard]] double kerrKretschmannScalar(
    double mass, double spin, double radius, double polarRadians);

}  // namespace vulkax::relativity
