#include "vulkax/relativity/kerr_geodesic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vulkax::relativity {
namespace {

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kPlanck = 6.62607015e-34;
constexpr double kLightSpeed = 299792458.0;
constexpr double kBoltzmann = 1.380649e-23;

struct State {
  double radius = 0.0;
  double polar = 0.0;
  double azimuth = 0.0;
  double coordinateTime = 0.0;
};

struct Potentials {
  double radial = 0.0;
  double polar = 0.0;
};

[[nodiscard]] Potentials potentials(
    const KerrGeodesicConfig& config, const KerrConstants& constants, const State& state) {
  const double mass = config.mass;
  const double spin = config.spin;
  const double radius2 = state.radius * state.radius;
  const double delta = radius2 - 2.0 * mass * state.radius + spin * spin;
  const double sine = std::sin(state.polar);
  const double cosine = std::cos(state.polar);
  const double sine2 = std::max(sine * sine, 1e-12);
  const double p =
      constants.energy * (radius2 + spin * spin) - spin * constants.axialAngularMomentum;
  const double shiftedMomentum = constants.axialAngularMomentum - spin * constants.energy;
  return {
      p * p - delta * (shiftedMomentum * shiftedMomentum + constants.carterConstant),
      constants.carterConstant +
          spin * spin * constants.energy * constants.energy * cosine * cosine -
          constants.axialAngularMomentum * constants.axialAngularMomentum * cosine * cosine /
              sine2};
}

[[nodiscard]] State derivative(
    const KerrGeodesicConfig& config,
    const KerrConstants& constants,
    const State& state,
    double radialSign,
    double polarSign) {
  const double spin = config.spin;
  const double radius2 = state.radius * state.radius;
  const double sine = std::sin(state.polar);
  const double cosine = std::cos(state.polar);
  const double sine2 = std::max(sine * sine, 1e-12);
  const double sigma = std::max(radius2 + spin * spin * cosine * cosine, 1e-12);
  const double delta = std::max(radius2 - 2.0 * config.mass * state.radius + spin * spin, 1e-10);
  const double p =
      constants.energy * (radius2 + spin * spin) - spin * constants.axialAngularMomentum;
  const Potentials potential = potentials(config, constants, state);
  return {
      radialSign * std::sqrt(std::max(0.0, potential.radial)) / sigma,
      polarSign * std::sqrt(std::max(0.0, potential.polar)) / sigma,
      (constants.axialAngularMomentum / sine2 - spin + spin * p / delta) / sigma,
      (-spin * (spin * constants.energy * sine2 - constants.axialAngularMomentum) +
       (radius2 + spin * spin) * p / delta) /
          sigma};
}

[[nodiscard]] State add(const State& state, const State& delta, double scale) {
  return {
      state.radius + scale * delta.radius,
      state.polar + scale * delta.polar,
      state.azimuth + scale * delta.azimuth,
      state.coordinateTime + scale * delta.coordinateTime};
}

[[nodiscard]] State rk4(
    const KerrGeodesicConfig& config,
    const KerrConstants& constants,
    const State& state,
    double radialSign,
    double polarSign,
    double step) {
  const State k1 = derivative(config, constants, state, radialSign, polarSign);
  const State k2 = derivative(config, constants, add(state, k1, 0.5 * step), radialSign, polarSign);
  const State k3 = derivative(config, constants, add(state, k2, 0.5 * step), radialSign, polarSign);
  const State k4 = derivative(config, constants, add(state, k3, step), radialSign, polarSign);
  return {
      state.radius + step * (k1.radius + 2.0 * k2.radius + 2.0 * k3.radius + k4.radius) / 6.0,
      state.polar + step * (k1.polar + 2.0 * k2.polar + 2.0 * k3.polar + k4.polar) / 6.0,
      state.azimuth + step * (k1.azimuth + 2.0 * k2.azimuth + 2.0 * k3.azimuth + k4.azimuth) / 6.0,
      state.coordinateTime + step *
                                 (k1.coordinateTime + 2.0 * k2.coordinateTime +
                                  2.0 * k3.coordinateTime + k4.coordinateTime) /
                                 6.0};
}

[[nodiscard]] bool finite(const State& state) {
  return std::isfinite(state.radius) && std::isfinite(state.polar) &&
         std::isfinite(state.azimuth) && std::isfinite(state.coordinateTime);
}

[[nodiscard]] double normalizedError(const State& coarse, const State& refined) {
  const std::array<double, 4> coarseValues{
      coarse.radius,
      coarse.polar,
      coarse.azimuth,
      coarse.coordinateTime};
  const std::array<double, 4> refinedValues{
      refined.radius,
      refined.polar,
      refined.azimuth,
      refined.coordinateTime};
  double error = 0.0;
  for (size_t index = 0; index < coarseValues.size(); ++index) {
    const double scale = std::max(1.0, std::abs(refinedValues[index]));
    error = std::max(error, std::abs(coarseValues[index] - refinedValues[index]) / scale);
  }
  return error;
}

void normalizePolar(State& state, double& polarSign) {
  if (state.polar < 0.0) {
    state.polar = -state.polar;
    state.azimuth += kPi;
    polarSign = -polarSign;
  } else if (state.polar > kPi) {
    state.polar = 2.0 * kPi - state.polar;
    state.azimuth += kPi;
    polarSign = -polarSign;
  }
}

double gaussian(double wavelength, double mean, double leftWidth, double rightWidth) {
  const double width = wavelength < mean ? leftWidth : rightWidth;
  const double normalized = (wavelength - mean) / width;
  return std::exp(-0.5 * normalized * normalized);
}

std::array<double, 3> cieApproximation(double wavelengthNanometres) {
  const double x = 1.056 * gaussian(wavelengthNanometres, 599.8, 37.9, 31.0) +
                   0.362 * gaussian(wavelengthNanometres, 442.0, 16.0, 26.7) -
                   0.065 * gaussian(wavelengthNanometres, 501.1, 20.4, 26.2);
  const double y = 0.821 * gaussian(wavelengthNanometres, 568.8, 46.9, 40.5) +
                   0.286 * gaussian(wavelengthNanometres, 530.9, 16.3, 31.1);
  const double z = 1.217 * gaussian(wavelengthNanometres, 437.0, 11.8, 36.0) +
                   0.681 * gaussian(wavelengthNanometres, 459.0, 26.0, 13.8);
  return {std::max(0.0, x), std::max(0.0, y), std::max(0.0, z)};
}

// Spectral samples below are integrated on a uniform wavelength grid, so use
// Planck's wavelength-domain radiance B_lambda directly. Mixing B_nu with a
// wavelength-domain quadrature biases the colour because dnu/dlambda is not
// constant across the visible spectrum.
double planckWavelengthRadiance(double wavelengthMetres, double temperatureKelvin) {
  const double exponent =
      kPlanck * kLightSpeed / (wavelengthMetres * kBoltzmann * temperatureKelvin);
  if (exponent > 700.0) return 0.0;
  const double wavelength2 = wavelengthMetres * wavelengthMetres;
  const double wavelength5 = wavelength2 * wavelength2 * wavelengthMetres;
  return 2.0 * kPlanck * kLightSpeed * kLightSpeed /
         (wavelength5 * std::expm1(exponent));
}

State equatorRoot(
    const KerrGeodesicConfig& config,
    const KerrConstants& constants,
    const State& start,
    double radialSign,
    double polarSign,
    double step) {
  double low = 0.0;
  double high = 1.0;
  const double startValue = start.polar - 0.5 * kPi;
  State best = start;
  for (uint32_t iteration = 0; iteration < 40; ++iteration) {
    const double fraction = 0.5 * (low + high);
    best = rk4(config, constants, start, radialSign, polarSign, step * fraction);
    double ignoredSign = polarSign;
    normalizePolar(best, ignoredSign);
    const double value = best.polar - 0.5 * kPi;
    if ((startValue <= 0.0 && value <= 0.0) || (startValue >= 0.0 && value >= 0.0)) {
      low = fraction;
    } else {
      high = fraction;
    }
  }
  return best;
}

State radiusRoot(
    const KerrGeodesicConfig& config,
    const KerrConstants& constants,
    const State& start,
    double radialSign,
    double polarSign,
    double step,
    double targetRadius) {
  double low = 0.0;
  double high = 1.0;
  State best = start;
  for (uint32_t iteration = 0; iteration < 40; ++iteration) {
    const double fraction = 0.5 * (low + high);
    best = rk4(config, constants, start, radialSign, polarSign, step * fraction);
    if (best.radius > targetRadius)
      low = fraction;
    else
      high = fraction;
  }
  return best;
}

void appendIntersection(KerrRayResult& result, const KerrIntersection& intersection) {
  if (result.intersectionCount < result.intersections.size()) {
    result.intersections[result.intersectionCount++] = intersection;
  } else {
    ++result.droppedIntersections;
  }
}

}  // namespace

double kerrOuterHorizonRadius(double mass, double spin) {
  if (!(mass > 0.0) || !std::isfinite(mass) || !std::isfinite(spin) || std::abs(spin) >= mass) {
    throw std::invalid_argument("Kerr requires finite mass > 0 and |spin| < mass");
  }
  return mass + std::sqrt(mass * mass - spin * spin);
}

double kerrProgradeIscoRadius(double mass, double spin) {
  static_cast<void>(kerrOuterHorizonRadius(mass, spin));
  const double normalizedSpin = std::clamp(spin / mass, -0.998, 0.998);
  const double z1 = 1.0 + std::cbrt(1.0 - normalizedSpin * normalizedSpin) *
                              (std::cbrt(1.0 + normalizedSpin) + std::cbrt(1.0 - normalizedSpin));
  const double z2 = std::sqrt(3.0 * normalizedSpin * normalizedSpin + z1 * z1);
  const double direction = normalizedSpin >= 0.0 ? -1.0 : 1.0;
  return mass *
         (3.0 + z2 + direction * std::sqrt(std::max(0.0, (3.0 - z1) * (3.0 + z1 + 2.0 * z2))));
}

KerrConstants kerrConstantsFromImagePlane(
    const KerrGeodesicConfig& config, double alpha, double beta) {
  static_cast<void>(kerrOuterHorizonRadius(config.mass, config.spin));
  const double sine = std::sin(config.observerInclinationRadians);
  const double cosine = std::cos(config.observerInclinationRadians);
  KerrConstants constants{};
  constants.axialAngularMomentum = -alpha * sine;
  constants.carterConstant =
      beta * beta + cosine * cosine * (alpha * alpha - config.spin * config.spin);
  return constants;
}

double kerrNormalizedNullConstraint(
    const KerrGeodesicConfig& config,
    const KerrConstants& constants,
    double radius,
    double polarRadians,
    double radialSign,
    double polarSign) {
  static_cast<void>(kerrOuterHorizonRadius(config.mass, config.spin));
  if (!(radius > 0.0) || !std::isfinite(radius) || !std::isfinite(polarRadians) ||
      !std::isfinite(radialSign) || !std::isfinite(polarSign)) {
    return std::numeric_limits<double>::infinity();
  }
  const State state{radius, polarRadians, 0.0, 0.0};
  const Potentials potential = potentials(config, constants, state);
  if (potential.radial < 0.0 || potential.polar < 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  const double radius2 = radius * radius;
  const double spin2 = config.spin * config.spin;
  const double sine = std::sin(polarRadians);
  const double cosine = std::cos(polarRadians);
  const double sine2 = std::max(sine * sine, 1e-14);
  const double sigma = radius2 + spin2 * cosine * cosine;
  const double delta = radius2 - 2.0 * config.mass * radius + spin2;
  if (!(delta > 0.0) || !(sigma > 0.0)) return std::numeric_limits<double>::infinity();

  const double gtt =
      -((radius2 + spin2) * (radius2 + spin2) - spin2 * delta * sine2) / (sigma * delta);
  const double gtPhi = -2.0 * config.mass * config.spin * radius / (sigma * delta);
  const double gPhiPhi = (delta - spin2 * sine2) / (sigma * delta * sine2);
  const double grr = delta / sigma;
  const double gThetaTheta = 1.0 / sigma;
  const double pt = -constants.energy;
  const double pPhi = constants.axialAngularMomentum;
  const double pr = radialSign * std::sqrt(potential.radial) / delta;
  const double pTheta = polarSign * std::sqrt(potential.polar);
  const std::array<double, 5> terms{
      gtt * pt * pt,
      2.0 * gtPhi * pt * pPhi,
      gPhiPhi * pPhi * pPhi,
      grr * pr * pr,
      gThetaTheta * pTheta * pTheta};
  double sum = 0.0;
  double scale = 0.0;
  for (const double term : terms) {
    sum += term;
    scale += std::abs(term);
  }
  return std::abs(sum) / std::max(scale, 1e-30);
}

KerrRayResult integrateKerrImageRay(const KerrGeodesicConfig& config, double alpha, double beta) {
  if (!(config.observerRadius > 2.0 * config.mass) || !(config.affineStep > 0.0) ||
      !(config.minimumAffineStep > 0.0) ||
      !(config.maximumAffineStep >= config.minimumAffineStep) ||
      !(config.relativeTolerance > 0.0) || !(config.nullConstraintTolerance > 0.0) ||
      !(config.horizonRelativeEpsilon > 0.0) || config.maximumSteps == 0) {
    throw std::invalid_argument("invalid Kerr geodesic integration configuration");
  }
  KerrRayResult result{};
  result.constants = kerrConstantsFromImagePlane(config, alpha, beta);
  result.horizonRadius = kerrOuterHorizonRadius(config.mass, config.spin);
  result.minimumRadius = config.observerRadius;
  State state{config.observerRadius, config.observerInclinationRadians, 0.0, 0.0};
  double radialSign = -1.0;
  double polarSign = beta >= 0.0 ? 1.0 : -1.0;
  double step = std::clamp(config.affineStep, config.minimumAffineStep, config.maximumAffineStep);
  double affineDistance = 0.0;

  for (uint32_t iteration = 0; iteration < config.maximumSteps; ++iteration) {
    result.integrationSteps = iteration + 1;
    result.minimumRadius = std::min(result.minimumRadius, state.radius);
    if (state.radius <= result.horizonRadius * (1.0 + config.horizonRelativeEpsilon)) {
      appendIntersection(
          result,
          {KerrIntersectionKind::Horizon,
           affineDistance,
           state.radius,
           state.polar,
           state.azimuth,
           state.coordinateTime,
           0});
      result.status = KerrRayStatus::Captured;
      break;
    }
    if (iteration > 2 && radialSign > 0.0 && state.radius >= config.escapeRadius) {
      result.status = KerrRayStatus::Escaped;
      break;
    }

    const Potentials currentPotential = potentials(config, result.constants, state);
    const double potentialScale = std::max(1.0, std::pow(state.radius, 4.0));
    if (currentPotential.radial < -1e-8 * potentialScale ||
        currentPotential.polar < -1e-8 * potentialScale) {
      result.maximumPotentialViolation = std::max(
          result.maximumPotentialViolation,
          std::max(-currentPotential.radial, -currentPotential.polar) / potentialScale);
      result.status = KerrRayStatus::Invalid;
      break;
    }

    State accepted{};
    double error = 0.0;
    bool acceptedStep = false;
    double acceptedPolarSign = polarSign;
    for (uint32_t attempt = 0; attempt < 12; ++attempt) {
      const State coarse = rk4(config, result.constants, state, radialSign, polarSign, step);
      const State half = rk4(config, result.constants, state, radialSign, polarSign, 0.5 * step);
      State refined = rk4(config, result.constants, half, radialSign, polarSign, 0.5 * step);
      double candidatePolarSign = polarSign;
      normalizePolar(refined, candidatePolarSign);
      error = normalizedError(coarse, refined);
      if (finite(refined) &&
          refined.radius <= result.horizonRadius * (1.0 + config.horizonRelativeEpsilon)) {
        accepted = refined;
        acceptedPolarSign = candidatePolarSign;
        acceptedStep = true;
        result.maximumLocalError = std::max(result.maximumLocalError, error);
        break;
      }
      const double nullConstraint = kerrNormalizedNullConstraint(
          config,
          result.constants,
          refined.radius,
          refined.polar,
          radialSign,
          candidatePolarSign);
      const Potentials nextPotential = potentials(config, result.constants, refined);
      const bool crossedRadialTurningPoint = nextPotential.radial < 0.0;
      const bool crossedPolarTurningPoint = nextPotential.polar < 0.0;
      if (crossedRadialTurningPoint || crossedPolarTurningPoint) {
        if (step > config.minimumAffineStep * 1.01) {
          step = std::max(config.minimumAffineStep, step * 0.5);
          ++result.rejectedSteps;
          continue;
        }
        if (crossedRadialTurningPoint) {
          radialSign = -radialSign;
          ++result.radialTurningPoints;
        }
        if (crossedPolarTurningPoint) {
          polarSign = -polarSign;
          ++result.polarTurningPoints;
        }
        continue;
      }
      const bool localErrorAccepted = error <= config.relativeTolerance;
      const bool constraintAccepted = nullConstraint <= config.nullConstraintTolerance;
      if (localErrorAccepted && constraintAccepted) {
        accepted = refined;
        acceptedPolarSign = candidatePolarSign;
        acceptedStep = true;
        result.maximumLocalError = std::max(result.maximumLocalError, error);
        result.maximumNullConstraintDrift =
            std::max(result.maximumNullConstraintDrift, nullConstraint);
        break;
      }
      if (step <= config.minimumAffineStep * 1.01) {
        result.maximumNullConstraintDrift =
            std::max(result.maximumNullConstraintDrift, nullConstraint);
        break;
      }
      step = std::max(config.minimumAffineStep, step * 0.5);
      ++result.rejectedSteps;
    }
    if (!acceptedStep || !finite(accepted)) {
      result.status = KerrRayStatus::Invalid;
      break;
    }

    const double stretchedHorizon = result.horizonRadius * (1.0 + config.horizonRelativeEpsilon);
    if (state.radius > stretchedHorizon && accepted.radius <= stretchedHorizon) {
      const State crossing = radiusRoot(
          config,
          result.constants,
          state,
          radialSign,
          polarSign,
          step,
          stretchedHorizon);
      appendIntersection(
          result,
          {KerrIntersectionKind::Horizon,
           affineDistance + step,
           crossing.radius,
           crossing.polar,
           crossing.azimuth,
           crossing.coordinateTime,
           0});
      state = crossing;
      result.minimumRadius = std::min(result.minimumRadius, stretchedHorizon);
      affineDistance += step;
      ++result.acceptedSteps;
      result.status = KerrRayStatus::Captured;
      break;
    }

    const bool crossedEquator = (state.polar - 0.5 * kPi) * (accepted.polar - 0.5 * kPi) <= 0.0 &&
                                std::abs(accepted.polar - state.polar) > 1e-12;
    if (crossedEquator) {
      const State crossing =
          equatorRoot(config, result.constants, state, radialSign, polarSign, step);
      const double crossingRadius = crossing.radius;
      if (crossingRadius >= config.diskInnerRadius && crossingRadius <= config.diskOuterRadius) {
        if (result.diskCrossings == 0) result.firstDiskRadius = crossingRadius;
        appendIntersection(
            result,
            {KerrIntersectionKind::DiskMidplane,
             affineDistance + step,
             crossingRadius,
             crossing.polar,
             crossing.azimuth,
             crossing.coordinateTime,
             result.diskCrossings});
        ++result.diskCrossings;
      }
    }
    state = accepted;
    affineDistance += step;
    polarSign = acceptedPolarSign;
    ++result.acceptedSteps;
    result.minimumAcceptedStep =
        result.minimumAcceptedStep == 0.0 ? step : std::min(result.minimumAcceptedStep, step);
    result.maximumAcceptedStep = std::max(result.maximumAcceptedStep, step);

    if (error < config.relativeTolerance * 0.05) {
      step = std::min(config.maximumAffineStep, step * 1.35);
    }
  }

  result.finalRadius = state.radius;
  result.finalPolarRadians = state.polar;
  result.finalAzimuthRadians = state.azimuth;
  return result;
}

KerrRayBundleResult integrateKerrImageRayBundle(
    const KerrGeodesicConfig& config, double alpha, double beta, double imagePlaneDifferential) {
  if (!(imagePlaneDifferential > 0.0) || !std::isfinite(imagePlaneDifferential)) {
    throw std::invalid_argument("Kerr ray-bundle differential must be finite and positive");
  }
  KerrRayBundleResult bundle{};
  bundle.central = integrateKerrImageRay(config, alpha, beta);
  bundle.imagePlaneX = integrateKerrImageRay(config, alpha + imagePlaneDifferential, beta);
  bundle.imagePlaneY = integrateKerrImageRay(config, alpha, beta + imagePlaneDifferential);
  bundle.imagePlaneNegativeX = integrateKerrImageRay(config, alpha - imagePlaneDifferential, beta);
  bundle.imagePlaneNegativeY = integrateKerrImageRay(config, alpha, beta - imagePlaneDifferential);
  if (bundle.central.status != KerrRayStatus::Escaped ||
      bundle.imagePlaneX.status != KerrRayStatus::Escaped ||
      bundle.imagePlaneY.status != KerrRayStatus::Escaped ||
      bundle.imagePlaneNegativeX.status != KerrRayStatus::Escaped ||
      bundle.imagePlaneNegativeY.status != KerrRayStatus::Escaped) {
    return bundle;
  }
  const auto wrappedAzimuth = [](double target, double origin) {
    double difference = (target - origin) / (2.0 * kPi);
    difference -= std::floor(difference + 0.5);
    return difference;
  };
  const double inverseDifferential = 0.5 / imagePlaneDifferential;
  const double a = wrappedAzimuth(
                       bundle.imagePlaneX.finalAzimuthRadians,
                       bundle.imagePlaneNegativeX.finalAzimuthRadians) *
                   inverseDifferential;
  const double b =
      (bundle.imagePlaneX.finalPolarRadians - bundle.imagePlaneNegativeX.finalPolarRadians) / kPi *
      inverseDifferential;
  const double c = wrappedAzimuth(
                       bundle.imagePlaneY.finalAzimuthRadians,
                       bundle.imagePlaneNegativeY.finalAzimuthRadians) *
                   inverseDifferential;
  const double d =
      (bundle.imagePlaneY.finalPolarRadians - bundle.imagePlaneNegativeY.finalPolarRadians) / kPi *
      inverseDifferential;
  bundle.sourceJacobian = {a, b, c, d};

  const double trace = a * a + b * b + c * c + d * d;
  const double determinant = a * d - b * c;
  const double discriminant =
      std::sqrt(std::max(0.0, trace * trace - 4.0 * determinant * determinant));
  const double maximumEigenvalue = 0.5 * (trace + discriminant);
  const double minimumEigenvalue = std::max(0.0, 0.5 * (trace - discriminant));
  bundle.maximumSingularValue = std::sqrt(maximumEigenvalue);
  bundle.minimumSingularValue = std::sqrt(minimumEigenvalue);
  bundle.sourceAreaScale = std::abs(determinant);
  bundle.magnification = 1.0 / std::max(bundle.sourceAreaScale, 1e-12);
  bundle.shear = (bundle.maximumSingularValue - bundle.minimumSingularValue) /
                 std::max(bundle.maximumSingularValue + bundle.minimumSingularValue, 1e-12);
  // Principal-axis orientation of J^T J in image-plane coordinates.
  bundle.orientationRadians =
      0.5 * std::atan2(2.0 * (a * c + b * d), a * a + b * b - c * c - d * d);
  bundle.causticRisk = bundle.sourceAreaScale < 1e-4 || bundle.shear > 0.95;
  bundle.valid = std::isfinite(bundle.minimumSingularValue) &&
                 std::isfinite(bundle.maximumSingularValue) &&
                 std::isfinite(bundle.magnification) && std::isfinite(bundle.shear) &&
                 bundle.maximumSingularValue >= bundle.minimumSingularValue &&
                 bundle.shear >= 0.0 && bundle.shear <= 1.0 + 1e-12;
  return bundle;
}

KerrDiskSpectrum evaluateKerrThinDiskSpectrum(
    const KerrGeodesicConfig& config,
    const KerrConstants& constants,
    double diskRadius,
    double maximumTemperatureKelvin) {
  KerrDiskSpectrum result{};
  if (!(maximumTemperatureKelvin > 0.0) || !std::isfinite(maximumTemperatureKelvin) ||
      !std::isfinite(diskRadius))
    return result;
  const double innerRadius = std::max(
      kerrProgradeIscoRadius(config.mass, config.spin),
      kerrOuterHorizonRadius(config.mass, config.spin) * 1.01);
  if (diskRadius <= innerRadius || diskRadius > config.diskOuterRadius) return result;
  const double normalizedRadius = diskRadius / config.mass;
  const double normalizedSpin = config.spin / config.mass;
  const double radiusPower = std::pow(normalizedRadius, 1.5);
  const double denominatorSquared = radiusPower * radiusPower -
                                    3.0 * normalizedRadius * normalizedRadius +
                                    2.0 * normalizedSpin * radiusPower;
  if (!(denominatorSquared > 0.0)) return result;
  const double angularVelocity = 1.0 / (radiusPower + normalizedSpin);
  const double emitterTimeComponent =
      (radiusPower + normalizedSpin) / std::sqrt(denominatorSquared);
  const double redshiftDenominator =
      emitterTimeComponent * (1.0 - angularVelocity * constants.axialAngularMomentum / config.mass);
  if (!(redshiftDenominator > 0.0) || !std::isfinite(redshiftDenominator)) return result;
  result.frequencyShift = 1.0 / redshiftDenominator;
  if (!(result.frequencyShift > 0.0) || !std::isfinite(result.frequencyShift)) return result;
  // I_nu / nu^3 is Lorentz invariant. Keep this telemetry field in its
  // historical frequency-domain form, while the wavelength-domain spectrum
  // below uses the corresponding g^5 transformation for I_lambda.
  result.invariantIntensityScale = std::pow(result.frequencyShift, 3.0);
  const double wavelengthIntensityScale = std::pow(result.frequencyShift, 5.0);
  const double innerRatio = innerRadius / diskRadius;
  result.emitterTemperatureKelvin = maximumTemperatureKelvin * std::pow(innerRatio, 0.75) *
                                    std::pow(std::max(0.0, 1.0 - std::sqrt(innerRatio)), 0.25);
  if (!(result.emitterTemperatureKelvin > 0.0)) return result;

  constexpr double firstWavelength = 390.0;
  constexpr double wavelengthStep = 30.0;
  constexpr double nanometresToMetres = 1e-9;
  const double wavelengthWeightMetres = wavelengthStep * nanometresToMetres;
  std::array<double, 3> xyz{};
  for (size_t index = 0; index < result.spectralRadiance.size(); ++index) {
    const double observedWavelength = firstWavelength + wavelengthStep * index;
    const double emittedWavelength = result.frequencyShift * observedWavelength;
    const double radiance =
        wavelengthIntensityScale *
        planckWavelengthRadiance(
            emittedWavelength * nanometresToMetres, result.emitterTemperatureKelvin);
    result.observedWavelengthNanometres[index] = observedWavelength;
    result.spectralRadiance[index] = radiance;
    const auto matching = cieApproximation(observedWavelength);
    for (size_t component = 0; component < 3; ++component)
      xyz[component] += radiance * matching[component] * wavelengthWeightMetres;
  }
  result.relativeLuminance = xyz[1];
  const std::array<double, 3> rgb{
      3.2406 * xyz[0] - 1.5372 * xyz[1] - 0.4986 * xyz[2],
      -0.9689 * xyz[0] + 1.8758 * xyz[1] + 0.0415 * xyz[2],
      0.0557 * xyz[0] - 0.2040 * xyz[1] + 1.0570 * xyz[2]};
  const double maximumComponent = std::max({rgb[0], rgb[1], rgb[2], 1e-30});
  for (size_t component = 0; component < 3; ++component) {
    result.linearSrgb[component] = std::max(0.0, rgb[component]) / maximumComponent;
  }
  result.valid = std::all_of(
                     result.spectralRadiance.begin(),
                     result.spectralRadiance.end(),
                     [](double value) { return std::isfinite(value) && value >= 0.0; }) &&
                 std::all_of(result.linearSrgb.begin(), result.linearSrgb.end(), [](double value) {
                   return std::isfinite(value) && value >= 0.0;
                 });
  return result;
}

KerrTransferResult evaluateKerrDiskTransfer(
    const KerrGeodesicConfig& config,
    const KerrRayResult& ray,
    const KerrDiskMedium& medium,
    double maximumTemperatureKelvin) {
  KerrTransferResult result{};
  if (medium.halfThickness < 0.0 || medium.verticalOpticalDepth < 0.0 ||
      medium.singleScatteringAlbedo < 0.0 || medium.singleScatteringAlbedo > 1.0 ||
      medium.coronaTemperatureKelvin <= 0.0 || medium.coronaOpticalDepth < 0.0)
    return result;
  double transmittance = 1.0;
  for (uint32_t index = 0; index < ray.intersectionCount; ++index) {
    const KerrIntersection& intersection = ray.intersections[index];
    if (intersection.kind != KerrIntersectionKind::DiskMidplane) continue;
    const KerrDiskSpectrum spectrum = evaluateKerrThinDiskSpectrum(
        config,
        ray.constants,
        intersection.radius,
        maximumTemperatureKelvin);
    if (!spectrum.valid) continue;
    const double radialScale = std::max(intersection.radius, config.mass);
    const double thicknessScale = std::clamp(2.0 * medium.halfThickness / radialScale, 1e-4, 1.0);
    const double opticalDepth = medium.verticalOpticalDepth * thicknessScale;
    const double absorbed = 1.0 - std::exp(-opticalDepth);
    const double direct = transmittance * absorbed;
    const double scattered =
        direct * medium.singleScatteringAlbedo * (1.0 - std::exp(-medium.coronaOpticalDepth));
    for (size_t component = 0; component < result.linearSrgb.size(); ++component) {
      result.linearSrgb[component] += spectrum.linearSrgb[component] * (direct + scattered);
    }
    result.emittedLuminance += spectrum.relativeLuminance;
    result.transmittedLuminance += spectrum.relativeLuminance * direct;
    result.scatteredLuminance += spectrum.relativeLuminance * scattered;
    result.accumulatedOpticalDepth += opticalDepth;
    transmittance *= std::exp(-opticalDepth);
    ++result.contributingIntersections;
    if (transmittance < 1e-4) {
      result.selfOccluded = index + 1 < ray.intersectionCount;
      break;
    }
  }
  const double maximum =
      std::max({result.linearSrgb[0], result.linearSrgb[1], result.linearSrgb[2], 1e-30});
  for (double& component : result.linearSrgb) component /= maximum;
  result.valid = result.contributingIntersections > 0 &&
                 std::isfinite(result.transmittedLuminance) &&
                 std::isfinite(result.scatteredLuminance);
  return result;
}

}  // namespace vulkax::relativity
