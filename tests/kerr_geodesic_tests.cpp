#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>

#include "vulkax/relativity/kerr_geodesic.hpp"

int main() {
  using namespace vulkax::relativity;

  assert(std::abs(kerrOuterHorizonRadius(1.0, 0.0) - 2.0) < 1e-12);
  assert(std::abs(kerrOuterHorizonRadius(1.0, 0.8) - 1.6) < 1e-12);
  assert(std::abs(kerrProgradeIscoRadius(1.0, 0.0) - 6.0) < 1e-12);
  assert(kerrProgradeIscoRadius(1.0, 0.8) < 6.0);
  assert(kerrProgradeIscoRadius(1.0, -0.8) > 6.0);
  bool rejectedExtremal = false;
  try {
    static_cast<void>(kerrOuterHorizonRadius(1.0, 1.0));
  } catch (const std::invalid_argument&) {
    rejectedExtremal = true;
  }
  assert(rejectedExtremal);

  KerrGeodesicConfig schwarzschild{};
  schwarzschild.spin = 0.0;
  schwarzschild.affineStep = 0.04;
  schwarzschild.maximumAffineStep = 0.08;
  const auto left = integrateKerrImageRay(schwarzschild, -7.0, 1.2);
  const auto right = integrateKerrImageRay(schwarzschild, 7.0, 1.2);
  assert(left.status == KerrRayStatus::Escaped);
  assert(right.status == KerrRayStatus::Escaped);
  assert(std::abs(left.minimumRadius - right.minimumRadius) < 1e-7);
  assert(std::abs(left.finalPolarRadians - right.finalPolarRadians) < 1e-7);
  assert(left.maximumNullConstraintDrift < schwarzschild.nullConstraintTolerance);
  assert(right.maximumNullConstraintDrift < schwarzschild.nullConstraintTolerance);
  assert(left.acceptedSteps > 0 && right.acceptedSteps > 0);
  assert(left.minimumAcceptedStep >= schwarzschild.minimumAffineStep);
  assert(left.maximumAcceptedStep <= schwarzschild.maximumAffineStep);

  const auto central = integrateKerrImageRay(schwarzschild, 0.0, 0.0);
  assert(central.status == KerrRayStatus::Captured);
  assert(central.minimumRadius <=
         central.horizonRadius * (1.0 + schwarzschild.horizonRelativeEpsilon));
  assert(central.maximumNullConstraintDrift < schwarzschild.nullConstraintTolerance);

  KerrGeodesicConfig spinning = schwarzschild;
  spinning.spin = 0.8;
  const auto prograde = integrateKerrImageRay(spinning, -5.0, 0.4);
  const auto retrograde = integrateKerrImageRay(spinning, 5.0, 0.4);
  assert(prograde.status != KerrRayStatus::Invalid);
  assert(retrograde.status != KerrRayStatus::Invalid);
  assert(
      std::abs(prograde.minimumRadius - retrograde.minimumRadius) > 0.1 ||
      prograde.status != retrograde.status);

  KerrGeodesicConfig refined = spinning;
  refined.affineStep = 0.02;
  refined.maximumAffineStep = 0.04;
  refined.relativeTolerance = 5e-6;
  const auto coarseRay = integrateKerrImageRay(spinning, 8.0, 2.0);
  const auto refinedRay = integrateKerrImageRay(refined, 8.0, 2.0);
  assert(coarseRay.status == KerrRayStatus::Escaped);
  assert(refinedRay.status == KerrRayStatus::Escaped);
  assert(std::abs(coarseRay.finalPolarRadians - refinedRay.finalPolarRadians) < 2e-3);
  assert(std::abs(coarseRay.finalAzimuthRadians - refinedRay.finalAzimuthRadians) < 2e-3);
  assert(refinedRay.maximumPotentialViolation < 1e-6);
  assert(refinedRay.maximumNullConstraintDrift < refined.nullConstraintTolerance);
  assert(refinedRay.maximumLocalError <= refined.relativeTolerance);

  const auto constants = kerrConstantsFromImagePlane(spinning, 12.0, 4.0);
  const double initialConstraint = kerrNormalizedNullConstraint(
      spinning, constants, spinning.observerRadius,
      spinning.observerInclinationRadians, -1.0, 1.0);
  assert(std::isfinite(initialConstraint));
  assert(initialConstraint < 1e-12);

  const auto blueShiftedSpectrum = evaluateKerrThinDiskSpectrum(
      spinning, kerrConstantsFromImagePlane(spinning, -4.0, 1.0), 8.0);
  const auto redShiftedSpectrum = evaluateKerrThinDiskSpectrum(
      spinning, kerrConstantsFromImagePlane(spinning, 4.0, 1.0), 8.0);
  assert(blueShiftedSpectrum.valid && redShiftedSpectrum.valid);
  assert(blueShiftedSpectrum.frequencyShift > redShiftedSpectrum.frequencyShift);
  assert(std::abs(blueShiftedSpectrum.invariantIntensityScale -
                  std::pow(blueShiftedSpectrum.frequencyShift, 3.0)) < 1e-12);
  assert(blueShiftedSpectrum.emitterTemperatureKelvin > 0.0);
  assert(blueShiftedSpectrum.observedWavelengthNanometres.front() == 390.0);
  assert(blueShiftedSpectrum.observedWavelengthNanometres.back() == 720.0);
  assert(std::accumulate(blueShiftedSpectrum.spectralRadiance.begin(),
                         blueShiftedSpectrum.spectralRadiance.end(), 0.0) > 0.0);
  assert(blueShiftedSpectrum.relativeLuminance != redShiftedSpectrum.relativeLuminance);
  assert(!evaluateKerrThinDiskSpectrum(spinning, constants, 2.0).valid);

  const auto bundle = integrateKerrImageRayBundle(spinning, 12.0, 4.0, 2e-2);
  assert(bundle.valid);
  assert(bundle.central.status == KerrRayStatus::Escaped);
  assert(bundle.minimumSingularValue >= 0.0);
  assert(bundle.maximumSingularValue >= bundle.minimumSingularValue);
  assert(bundle.sourceAreaScale > 0.0);
  assert(bundle.magnification > 0.0);
  assert(bundle.shear >= 0.0 && bundle.shear <= 1.0);

  const auto refinedBundle = integrateKerrImageRayBundle(refined, 12.0, 4.0, 1e-2);
  assert(refinedBundle.valid);
  assert(std::abs(bundle.sourceAreaScale - refinedBundle.sourceAreaScale) < 0.05);
  assert(std::abs(bundle.shear - refinedBundle.shear) < 0.08);
  const auto halfDifferentialBundle = integrateKerrImageRayBundle(spinning, 12.0, 4.0, 1e-2);
  assert(halfDifferentialBundle.valid);
  assert(std::abs(bundle.shear - halfDifferentialBundle.shear) < 0.02);

  const auto schwarzschildLeftBundle = integrateKerrImageRayBundle(schwarzschild, -8.0, 2.0, 2e-3);
  const auto schwarzschildRightBundle = integrateKerrImageRayBundle(schwarzschild, 8.0, 2.0, 2e-3);
  assert(schwarzschildLeftBundle.valid && schwarzschildRightBundle.valid);
  assert(std::abs(schwarzschildLeftBundle.sourceAreaScale -
                  schwarzschildRightBundle.sourceAreaScale) < 2e-3);
  assert(std::abs(schwarzschildLeftBundle.shear - schwarzschildRightBundle.shear) < 2e-3);

  bool rejectedBundleDifferential = false;
  try {
    static_cast<void>(integrateKerrImageRayBundle(spinning, 8.0, 2.0, 0.0));
  } catch (const std::invalid_argument&) {
    rejectedBundleDifferential = true;
  }
  assert(rejectedBundleDifferential);

  std::cout << "Vulkax Kerr geodesic and differential bundle tests passed\n";
  return 0;
}
