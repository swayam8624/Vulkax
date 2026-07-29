#pragma once

#include <array>
#include <cstdint>

namespace vulkax::relativity {

// Boyer-Lindquist Kerr parameters in geometric units (G = c = 1). The spin
// magnitude must remain below the mass so that an event horizon exists.
struct KerrGeodesicConfig {
  double mass = 1.0;
  double spin = 0.75;
  double observerRadius = 52.0;
  double observerInclinationRadians = 1.25;
  double affineStep = 0.04;
  double minimumAffineStep = 0.00125;
  double maximumAffineStep = 0.08;
  double relativeTolerance = 2e-5;
  double escapeRadius = 52.0;
  double diskInnerRadius = 3.0;
  double diskOuterRadius = 18.0;
  uint32_t maximumSteps = 120000;
};

struct KerrConstants {
  double energy = 1.0;
  double axialAngularMomentum = 0.0;
  double carterConstant = 0.0;
};

enum class KerrRayStatus : uint8_t {
  Unfinished,
  Captured,
  Escaped,
  Invalid,
};

struct KerrRayResult {
  KerrRayStatus status = KerrRayStatus::Unfinished;
  KerrConstants constants{};
  double horizonRadius = 0.0;
  double minimumRadius = 0.0;
  double finalRadius = 0.0;
  double finalPolarRadians = 0.0;
  double finalAzimuthRadians = 0.0;
  double firstDiskRadius = 0.0;
  uint32_t diskCrossings = 0;
  uint32_t radialTurningPoints = 0;
  uint32_t polarTurningPoints = 0;
  uint32_t integrationSteps = 0;
  double maximumPotentialViolation = 0.0;
};

// First-order source-space footprint from a central geodesic and differential
// image-plane neighbours. The Jacobian maps image-plane offsets to wrapped
// celestial-sphere coordinates (azimuth / 2pi, polar / pi).
struct KerrRayBundleResult {
  KerrRayResult central{};
  KerrRayResult imagePlaneX{};
  KerrRayResult imagePlaneY{};
  KerrRayResult imagePlaneNegativeX{};
  KerrRayResult imagePlaneNegativeY{};
  std::array<double, 4> sourceJacobian{};  // column-major 2x2
  double minimumSingularValue = 0.0;
  double maximumSingularValue = 0.0;
  double sourceAreaScale = 0.0;
  double magnification = 0.0;
  double shear = 0.0;
  double orientationRadians = 0.0;
  bool causticRisk = false;
  bool valid = false;
};

[[nodiscard]] double kerrOuterHorizonRadius(double mass, double spin);

// Converts Bardeen image-plane coordinates (alpha, beta) at a distant
// observer into the conserved null-geodesic quantities E, Lz, and Q.
[[nodiscard]] KerrConstants kerrConstantsFromImagePlane(
    const KerrGeodesicConfig& config, double alpha, double beta);

// Integrates a backward null ray with the Carter-separated first-order Kerr
// equations. The implementation adaptively compares one full RK4 step with
// two half steps and explicitly reflects radial/polar signs at turning points.
[[nodiscard]] KerrRayResult integrateKerrImageRay(
    const KerrGeodesicConfig& config, double alpha, double beta);

[[nodiscard]] KerrRayBundleResult integrateKerrImageRayBundle(
    const KerrGeodesicConfig& config,
    double alpha,
    double beta,
    double imagePlaneDifferential = 1e-3);

}  // namespace vulkax::relativity
