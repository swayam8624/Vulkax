#include "vulkax/relativity/kerr_geodesic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vulkax::relativity {
namespace {

constexpr double kPi = 3.1415926535897932384626433832795;

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

}  // namespace

double kerrOuterHorizonRadius(double mass, double spin) {
  if (!(mass > 0.0) || !std::isfinite(mass) || !std::isfinite(spin) || std::abs(spin) >= mass) {
    throw std::invalid_argument("Kerr requires finite mass > 0 and |spin| < mass");
  }
  return mass + std::sqrt(mass * mass - spin * spin);
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

KerrRayResult integrateKerrImageRay(const KerrGeodesicConfig& config, double alpha, double beta) {
  if (!(config.observerRadius > 2.0 * config.mass) || !(config.affineStep > 0.0) ||
      !(config.minimumAffineStep > 0.0) ||
      !(config.maximumAffineStep >= config.minimumAffineStep) ||
      !(config.relativeTolerance > 0.0) || config.maximumSteps == 0) {
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

  for (uint32_t iteration = 0; iteration < config.maximumSteps; ++iteration) {
    result.integrationSteps = iteration + 1;
    result.minimumRadius = std::min(result.minimumRadius, state.radius);
    if (state.radius <= result.horizonRadius * (1.0 + 2e-6)) {
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
    for (uint32_t attempt = 0; attempt < 12; ++attempt) {
      const State coarse = rk4(config, result.constants, state, radialSign, polarSign, step);
      const State half = rk4(config, result.constants, state, radialSign, polarSign, 0.5 * step);
      State refined = rk4(config, result.constants, half, radialSign, polarSign, 0.5 * step);
      normalizePolar(refined, polarSign);
      error = normalizedError(coarse, refined);
      const Potentials nextPotential = potentials(config, result.constants, refined);
      const bool crossedRadialTurningPoint = nextPotential.radial < 0.0;
      const bool crossedPolarTurningPoint = nextPotential.polar < 0.0;
      if (crossedRadialTurningPoint || crossedPolarTurningPoint) {
        if (step > config.minimumAffineStep * 1.01) {
          step = std::max(config.minimumAffineStep, step * 0.5);
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
      if (error <= config.relativeTolerance || step <= config.minimumAffineStep * 1.01) {
        accepted = refined;
        acceptedStep = true;
        break;
      }
      step = std::max(config.minimumAffineStep, step * 0.5);
    }
    if (!acceptedStep || !finite(accepted)) {
      result.status = KerrRayStatus::Invalid;
      break;
    }

    const bool crossedEquator = (state.polar - 0.5 * kPi) * (accepted.polar - 0.5 * kPi) <= 0.0 &&
                                std::abs(accepted.polar - state.polar) > 1e-12;
    if (crossedEquator) {
      const double denominator = accepted.polar - state.polar;
      const double fraction = std::clamp((0.5 * kPi - state.polar) / denominator, 0.0, 1.0);
      const double crossingRadius = state.radius + fraction * (accepted.radius - state.radius);
      if (crossingRadius >= config.diskInnerRadius && crossingRadius <= config.diskOuterRadius) {
        if (result.diskCrossings == 0) result.firstDiskRadius = crossingRadius;
        ++result.diskCrossings;
      }
    }
    state = accepted;

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
  const double b = (bundle.imagePlaneX.finalPolarRadians - bundle.imagePlaneNegativeX.finalPolarRadians) /
                   kPi * inverseDifferential;
  const double c = wrappedAzimuth(
                       bundle.imagePlaneY.finalAzimuthRadians,
                       bundle.imagePlaneNegativeY.finalAzimuthRadians) *
                   inverseDifferential;
  const double d = (bundle.imagePlaneY.finalPolarRadians - bundle.imagePlaneNegativeY.finalPolarRadians) /
                   kPi * inverseDifferential;
  bundle.sourceJacobian = {a, b, c, d};

  const double trace = a * a + b * b + c * c + d * d;
  const double determinant = a * d - b * c;
  const double discriminant = std::sqrt(std::max(0.0, trace * trace - 4.0 * determinant * determinant));
  const double maximumEigenvalue = 0.5 * (trace + discriminant);
  const double minimumEigenvalue = std::max(0.0, 0.5 * (trace - discriminant));
  bundle.maximumSingularValue = std::sqrt(maximumEigenvalue);
  bundle.minimumSingularValue = std::sqrt(minimumEigenvalue);
  bundle.sourceAreaScale = std::abs(determinant);
  bundle.magnification = 1.0 / std::max(bundle.sourceAreaScale, 1e-12);
  bundle.shear = (bundle.maximumSingularValue - bundle.minimumSingularValue) /
                 std::max(bundle.maximumSingularValue + bundle.minimumSingularValue, 1e-12);
  // Principal-axis orientation of J^T J in image-plane coordinates.
  bundle.orientationRadians = 0.5 * std::atan2(
      2.0 * (a * c + b * d), a * a + b * b - c * c - d * d);
  bundle.causticRisk = bundle.sourceAreaScale < 1e-4 || bundle.shear > 0.95;
  bundle.valid = std::isfinite(bundle.minimumSingularValue) &&
                 std::isfinite(bundle.maximumSingularValue) &&
                 std::isfinite(bundle.magnification) && std::isfinite(bundle.shear) &&
                 bundle.maximumSingularValue >= bundle.minimumSingularValue &&
                 bundle.shear >= 0.0 && bundle.shear <= 1.0 + 1e-12;
  return bundle;
}

}  // namespace vulkax::relativity
