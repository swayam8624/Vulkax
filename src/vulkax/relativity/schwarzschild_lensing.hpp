#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace vulkax::relativity {

struct SchwarzschildRayResult {
  bool escaped = false;
  double impactParameter = 0.0;
  double closestApproach = 0.0;
  double deflectionRadians = 0.0;
  uint32_t integrationSteps = 0;
};

// Integrates the equatorial null-ray orbit u'' + u = 3 M u^2 with RK4.
// Units are geometric (G=c=1), so mass and impact parameter share a length
// unit. Rays at/below sqrt(27) M are classified as captured by the photon
// sphere rather than producing an invented deflection value.
[[nodiscard]] SchwarzschildRayResult integrateSchwarzschildRay(
    double mass,
    double impactParameter,
    double angularStep = 0.0005);

[[nodiscard]] double weakFieldDeflectionRadians(double mass, double impactParameter);

struct SchwarzschildVec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct SchwarzschildGeodesicConfig {
  double mass = 1.0;
  double affineStep = 0.02;
  double minimumAffineStep = 1e-5;
  double maximumAffineStep = 0.10;
  double relativeTolerance = 1e-7;
  double maximumAffineDistance = 4096.0;
  uint32_t maximumSteps = 250000;
  uint32_t recordedPathStride = 8;
};

struct SchwarzschildGeodesicResult {
  bool captured = false;
  bool escaped = false;
  double initialEnergy = 0.0;
  double minimumRadius = 0.0;
  double azimuthRadians = 0.0;
  double maximumRelativeEnergyDrift = 0.0;
  uint32_t integrationSteps = 0;
  std::vector<SchwarzschildVec3> path;
};

// Integrates a null geodesic in an arbitrary orbital plane around a
// Schwarzschild black hole. Spherical symmetry keeps every geodesic planar,
// but the returned positions are reconstructed in the caller's 3D frame. The
// integrator evolves r, dr/dlambda, and phi using the exact radial effective
// potential and reports conserved-energy drift for validation/step control.
[[nodiscard]] SchwarzschildGeodesicResult integrateSchwarzschildGeodesic(
    const SchwarzschildGeodesicConfig& config,
    SchwarzschildVec3 observerPosition,
    SchwarzschildVec3 initialDirection);

struct SchwarzschildDeflectionSample {
  double impactParameter = 0.0;
  bool captured = false;
  double deflectionRadians = 0.0;
};

// A monotone lookup made from the RK4 reference integrator. It is intended for
// preview mapping only: captured rays remain explicit and values outside the
// sampled range fall back to the weak-field approximation.
class SchwarzschildDeflectionLut {
 public:
  SchwarzschildDeflectionLut(double mass, double maximumImpactParameter, uint32_t samples = 32,
                              double angularStep = 0.002);

  [[nodiscard]] double mass() const { return mass_; }
  [[nodiscard]] double criticalImpactParameter() const;
  [[nodiscard]] bool captured(double impactParameter) const;
  [[nodiscard]] double deflectionRadians(double impactParameter) const;
  [[nodiscard]] const std::vector<SchwarzschildDeflectionSample>& samples() const { return samples_; }

 private:
  double mass_ = 1.0;
  double maximumImpactParameter_ = 1.0;
  std::vector<SchwarzschildDeflectionSample> samples_;
};

struct SchwarzschildThinDiskConfig {
  double mass = 1.0;
  double maximumImpactParameter = 32.0;
  double diskInnerRadius = 4.0;
  double diskOuterRadius = 15.0;
  double inclinationRadians = 1.10;
  double dopplerStrength = 0.75;
  uint32_t deflectionSamples = 96;
  double angularStep = 0.001;
};

struct SchwarzschildDiskSample {
  bool captured = false;
  bool diskHit = false;
  double impactParameter = 0.0;
  double deflectionRadians = 0.0;
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
};

// A ray-per-pixel thin-disk visualization using the equatorial Schwarzschild
// RK4 deflection reference. It is deliberately not a Kerr/DNGR substitute:
// frame dragging, full 3D geodesics, ray bundles, and spectral transport are
// outside this documented model. The disk source is an inclined emissive
// annulus with a heuristic Doppler term after Schwarzschild lens mapping.
class SchwarzschildThinDiskRenderer {
 public:
  explicit SchwarzschildThinDiskRenderer(SchwarzschildThinDiskConfig config = {});

  [[nodiscard]] const SchwarzschildThinDiskConfig& config() const { return config_; }
  [[nodiscard]] const SchwarzschildDeflectionLut& deflectionLut() const { return lut_; }
  [[nodiscard]] SchwarzschildDiskSample sample(
      double cameraX, double cameraY, double cameraOrbitRadians = 0.0) const;

 private:
  SchwarzschildThinDiskConfig config_;
  SchwarzschildDeflectionLut lut_;
};

}  // namespace vulkax::relativity
