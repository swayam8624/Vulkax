#include "vulkax/relativity/schwarzschild_lensing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace vulkax::relativity {
namespace {

struct State { double u; double derivative; };

SchwarzschildVec3 add(SchwarzschildVec3 left, SchwarzschildVec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

SchwarzschildVec3 scale(SchwarzschildVec3 value, double scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

double dot(SchwarzschildVec3 left, SchwarzschildVec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

SchwarzschildVec3 cross(SchwarzschildVec3 left, SchwarzschildVec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

double length(SchwarzschildVec3 value) { return std::sqrt(dot(value, value)); }

SchwarzschildVec3 normalized(SchwarzschildVec3 value) {
  const double valueLength = length(value);
  if (!(valueLength > std::numeric_limits<double>::epsilon())) {
    throw std::invalid_argument("Schwarzschild geodesic vector must be non-zero");
  }
  return scale(value, 1.0 / valueLength);
}

struct GeodesicState {
  double radius = 0.0;
  double radialVelocity = 0.0;
  double azimuth = 0.0;
};

GeodesicState geodesicDerivative(GeodesicState state, double mass, double angularMomentum) {
  const double radius2 = state.radius * state.radius;
  const double radius3 = radius2 * state.radius;
  const double radius4 = radius3 * state.radius;
  return {state.radialVelocity,
          angularMomentum * angularMomentum / radius3 -
              3.0 * mass * angularMomentum * angularMomentum / radius4,
          angularMomentum / radius2};
}

GeodesicState geodesicAdvanceRk4(
    GeodesicState state, double mass, double angularMomentum, double step) {
  const GeodesicState k1 = geodesicDerivative(state, mass, angularMomentum);
  const GeodesicState k2 = geodesicDerivative(
      {state.radius + 0.5 * step * k1.radius,
       state.radialVelocity + 0.5 * step * k1.radialVelocity,
       state.azimuth + 0.5 * step * k1.azimuth}, mass, angularMomentum);
  const GeodesicState k3 = geodesicDerivative(
      {state.radius + 0.5 * step * k2.radius,
       state.radialVelocity + 0.5 * step * k2.radialVelocity,
       state.azimuth + 0.5 * step * k2.azimuth}, mass, angularMomentum);
  const GeodesicState k4 = geodesicDerivative(
      {state.radius + step * k3.radius,
       state.radialVelocity + step * k3.radialVelocity,
       state.azimuth + step * k3.azimuth}, mass, angularMomentum);
  return {
      state.radius + step * (k1.radius + 2.0 * k2.radius + 2.0 * k3.radius + k4.radius) / 6.0,
      state.radialVelocity + step * (k1.radialVelocity + 2.0 * k2.radialVelocity +
                                     2.0 * k3.radialVelocity + k4.radialVelocity) / 6.0,
      state.azimuth + step * (k1.azimuth + 2.0 * k2.azimuth + 2.0 * k3.azimuth + k4.azimuth) / 6.0,
  };
}

State derivative(State state, double mass) {
  return {state.derivative, -state.u + 3.0 * mass * state.u * state.u};
}

State advanceRk4(State state, double mass, double step) {
  const State k1 = derivative(state, mass);
  const State k2 = derivative(
      {state.u + 0.5 * step * k1.u, state.derivative + 0.5 * step * k1.derivative}, mass);
  const State k3 = derivative(
      {state.u + 0.5 * step * k2.u, state.derivative + 0.5 * step * k2.derivative}, mass);
  const State k4 = derivative(
      {state.u + step * k3.u, state.derivative + step * k3.derivative}, mass);
  return {
      state.u + step * (k1.u + 2.0 * k2.u + 2.0 * k3.u + k4.u) / 6.0,
      state.derivative + step * (k1.derivative + 2.0 * k2.derivative +
                                 2.0 * k3.derivative + k4.derivative) / 6.0,
  };
}

double closestU(double mass, double impactParameter) {
  double u = 1.0 / impactParameter;
  for (uint32_t iteration = 0; iteration < 32; ++iteration) {
    const double residual = u * u - 2.0 * mass * u * u * u -
                            1.0 / (impactParameter * impactParameter);
    const double slope = 2.0 * u - 6.0 * mass * u * u;
    const double next = u - residual / slope;
    if (!std::isfinite(next) || next <= 0.0) break;
    if (std::abs(next - u) <= 1e-14 * std::max(1.0, u)) return next;
    u = next;
  }
  return u;
}

}  // namespace

double weakFieldDeflectionRadians(double mass, double impactParameter) {
  if (mass <= 0.0 || impactParameter <= 0.0) {
    throw std::invalid_argument("mass and impact parameter must be positive");
  }
  return 4.0 * mass / impactParameter;
}

SchwarzschildGeodesicResult integrateSchwarzschildGeodesic(
    const SchwarzschildGeodesicConfig& config,
    SchwarzschildVec3 observerPosition,
    SchwarzschildVec3 initialDirection) {
  if (!(config.mass > 0.0) || !(config.affineStep > 0.0) ||
      !(config.minimumAffineStep > 0.0) || config.maximumAffineStep < config.minimumAffineStep ||
      !(config.relativeTolerance > 0.0) || !(config.maximumAffineDistance > 0.0) || config.maximumSteps == 0 ||
      config.recordedPathStride == 0) {
    throw std::invalid_argument("Schwarzschild geodesic configuration is invalid");
  }
  const SchwarzschildVec3 radial = normalized(observerPosition);
  const SchwarzschildVec3 direction = normalized(initialDirection);
  const double initialRadius = length(observerPosition);
  const double horizon = 2.0 * config.mass;
  if (initialRadius <= horizon) {
    throw std::invalid_argument("Schwarzschild observer must be outside the horizon");
  }
  const double radialVelocity = dot(direction, radial);
  SchwarzschildVec3 tangential = add(direction, scale(radial, -radialVelocity));
  const double tangentialLength = length(tangential);
  if (tangentialLength <= 1e-10) {
    throw std::invalid_argument("Schwarzschild geodesic requires a non-radial camera ray");
  }
  tangential = scale(tangential, 1.0 / tangentialLength);
  // e0/e1 span the ray's orbital plane. The plane normal is retained by this
  // basis even though the reduced Schwarzschild equations only require r/phi.
  const SchwarzschildVec3 planeNormal = normalized(cross(radial, tangential));
  const SchwarzschildVec3 e1 = normalized(cross(planeNormal, radial));
  const double angularMomentum = initialRadius * tangentialLength;
  const double energySquared = radialVelocity * radialVelocity +
      (1.0 - horizon / initialRadius) * angularMomentum * angularMomentum /
          (initialRadius * initialRadius);
  SchwarzschildGeodesicResult result{};
  result.initialEnergy = std::sqrt(energySquared);
  result.minimumRadius = initialRadius;
  GeodesicState state{initialRadius, radialVelocity, 0.0};
  double affineDistance = 0.0;
  double nextStep = std::clamp(config.affineStep, config.minimumAffineStep, config.maximumAffineStep);
  uint32_t completedSteps = 0;
  for (uint32_t step = 0; step < config.maximumSteps && affineDistance < config.maximumAffineDistance; ++step) {
    if (step % config.recordedPathStride == 0) {
      const SchwarzschildVec3 directionAtState = add(
          scale(radial, std::cos(state.azimuth)), scale(e1, std::sin(state.azimuth)));
      result.path.push_back(scale(directionAtState, state.radius));
    }
    result.minimumRadius = std::min(result.minimumRadius, state.radius);
    const double invariant = state.radialVelocity * state.radialVelocity +
        (1.0 - horizon / state.radius) * angularMomentum * angularMomentum /
            (state.radius * state.radius);
    result.maximumRelativeEnergyDrift = std::max(
        result.maximumRelativeEnergyDrift,
        std::abs(invariant - energySquared) / std::max(energySquared, 1e-12));
    if (state.radius <= horizon * (1.0 + 1e-5)) {
      result.captured = true;
      result.integrationSteps = step;
      result.azimuthRadians = state.azimuth;
      return result;
    }
    if (step > 0 && state.radius >= initialRadius && state.radialVelocity > 0.0) {
      result.escaped = true;
      result.integrationSteps = step;
      result.azimuthRadians = state.azimuth;
      return result;
    }
    GeodesicState accepted{};
    double acceptedStep = nextStep;
    for (uint32_t retry = 0; retry < 24; ++retry) {
      const GeodesicState full = geodesicAdvanceRk4(state, config.mass, angularMomentum, acceptedStep);
      const GeodesicState half = geodesicAdvanceRk4(
          geodesicAdvanceRk4(state, config.mass, angularMomentum, acceptedStep * 0.5),
          config.mass, angularMomentum, acceptedStep * 0.5);
      const double radiusScale = std::max({1.0, std::abs(half.radius), std::abs(full.radius)});
      const double velocityScale = std::max({1.0, std::abs(half.radialVelocity), std::abs(full.radialVelocity)});
      const double azimuthScale = std::max({1.0, std::abs(half.azimuth), std::abs(full.azimuth)});
      const double normalizedError = std::max({
          std::abs(half.radius - full.radius) / radiusScale,
          std::abs(half.radialVelocity - full.radialVelocity) / velocityScale,
          std::abs(half.azimuth - full.azimuth) / azimuthScale,
      });
      if (normalizedError <= config.relativeTolerance || acceptedStep <= config.minimumAffineStep) {
        accepted = half;
        const double safeError = std::max(normalizedError, 1e-16);
        const double factor = std::clamp(0.9 * std::pow(config.relativeTolerance / safeError, 0.2), 0.5, 2.0);
        nextStep = std::clamp(acceptedStep * factor, config.minimumAffineStep, config.maximumAffineStep);
        break;
      }
      acceptedStep = std::max(config.minimumAffineStep, acceptedStep * 0.5);
    }
    state = accepted;
    affineDistance += acceptedStep;
    completedSteps = step + 1;
    if (!std::isfinite(state.radius) || !std::isfinite(state.radialVelocity) || state.radius <= 0.0) {
      result.integrationSteps = step + 1;
      result.azimuthRadians = state.azimuth;
      return result;
    }
  }
  result.integrationSteps = completedSteps;
  result.azimuthRadians = state.azimuth;
  return result;
}

SchwarzschildDeflectionLut::SchwarzschildDeflectionLut(
    double mass, double maximumImpactParameter, uint32_t samples, double angularStep)
    : mass_(mass), maximumImpactParameter_(maximumImpactParameter) {
  if (mass_ <= 0.0 || maximumImpactParameter_ <= std::sqrt(27.0) * mass_ || samples < 2 || angularStep <= 0.0) {
    throw std::invalid_argument("Schwarzschild deflection LUT requires a positive range above the photon sphere");
  }
  samples_.reserve(samples);
  const double critical = criticalImpactParameter();
  // Quadratic spacing allocates more reference samples near the photon sphere,
  // where deflection changes fastest.
  for (uint32_t index = 0; index < samples; ++index) {
    const double normalized = static_cast<double>(index) / static_cast<double>(samples - 1);
    const double impact = critical * (1.0 + 1e-4) + (maximumImpactParameter_ - critical * (1.0 + 1e-4)) * normalized * normalized;
    const auto result = integrateSchwarzschildRay(mass_, impact, angularStep);
    samples_.push_back({impact, !result.escaped, result.deflectionRadians});
  }
}

double SchwarzschildDeflectionLut::criticalImpactParameter() const {
  return std::sqrt(27.0) * mass_;
}

bool SchwarzschildDeflectionLut::captured(double impactParameter) const {
  return impactParameter <= criticalImpactParameter();
}

double SchwarzschildDeflectionLut::deflectionRadians(double impactParameter) const {
  if (captured(impactParameter)) return 0.0;
  if (impactParameter >= maximumImpactParameter_) return weakFieldDeflectionRadians(mass_, impactParameter);
  const auto upper = std::lower_bound(samples_.begin(), samples_.end(), impactParameter,
      [](const SchwarzschildDeflectionSample& sample, double impact) { return sample.impactParameter < impact; });
  if (upper == samples_.begin()) return upper->deflectionRadians;
  if (upper == samples_.end()) return samples_.back().deflectionRadians;
  const auto lower = upper - 1;
  const double fraction = (impactParameter - lower->impactParameter) / (upper->impactParameter - lower->impactParameter);
  return lower->deflectionRadians + fraction * (upper->deflectionRadians - lower->deflectionRadians);
}

SchwarzschildThinDiskRenderer::SchwarzschildThinDiskRenderer(SchwarzschildThinDiskConfig config)
    : config_(config),
      lut_(config.mass, config.maximumImpactParameter, config.deflectionSamples, config.angularStep) {
  if (config_.diskInnerRadius <= 0.0 || config_.diskOuterRadius <= config_.diskInnerRadius ||
      config_.inclinationRadians <= 0.05 || config_.inclinationRadians >= 1.52 ||
      config_.dopplerStrength < 0.0) {
    throw std::invalid_argument("Schwarzschild thin disk requires a positive inclined annulus");
  }
}

SchwarzschildDiskSample SchwarzschildThinDiskRenderer::sample(
    double cameraX, double cameraY, double cameraOrbitRadians) const {
  SchwarzschildDiskSample result{};
  const double radius = std::sqrt(cameraX * cameraX + cameraY * cameraY);
  // The image-plane scale places the critical impact parameter near the
  // center while retaining enough outer disk and starfield in the viewport.
  result.impactParameter = radius * 18.0;
  if (lut_.captured(result.impactParameter)) {
    result.captured = true;
    return result;
  }
  result.deflectionRadians = lut_.deflectionRadians(result.impactParameter);
  const double imageAngle = std::atan2(cameraY, cameraX);
  const double sourceAngle = imageAngle + result.deflectionRadians + cameraOrbitRadians;
  // Source-plane reconstruction is an explicit thin-disk approximation over
  // the RK4 bend angle, not a claim of complete 3D Kerr transport.
  const double lensedRadius = std::max(0.0, radius + 0.045 * std::sin(result.deflectionRadians));
  const double sourceX = lensedRadius * std::cos(sourceAngle) * 20.0;
  const double sourceY = lensedRadius * std::sin(sourceAngle) * 20.0;
  const double diskRadius = std::sqrt(
      sourceX * sourceX + std::pow(sourceY / std::cos(config_.inclinationRadians), 2.0));
  if (diskRadius < config_.diskInnerRadius || diskRadius > config_.diskOuterRadius) {
    // Near-critical escaped rays form a photon-ring proxy from the actual
    // rapidly varying Schwarzschild deflection, even when the disk is missed.
    const double critical = lut_.criticalImpactParameter();
    const double ring = std::exp(-std::pow((result.impactParameter - critical) / 0.20, 2.0));
    result.red = 0.9 * ring;
    result.green = 0.48 * ring;
    result.blue = 0.13 * ring;
    return result;
  }
  result.diskHit = true;
  const double normalizedRadius = (diskRadius - config_.diskInnerRadius) /
      (config_.diskOuterRadius - config_.diskInnerRadius);
  const double temperature = std::pow(config_.diskInnerRadius / diskRadius, 0.75);
  // The sign is a viewing convention. It is a compact emission heuristic,
  // not a relativistic spectrum calculation.
  const double lineOfSightVelocity = std::sin(sourceAngle) * std::sin(config_.inclinationRadians);
  const double doppler = std::pow(
      std::max(0.12, 1.0 + config_.dopplerStrength * lineOfSightVelocity), 3.0);
  const double edgeFade = std::sin(std::numbers::pi * std::clamp(normalizedRadius, 0.0, 1.0));
  const double emissivity = temperature * doppler * (0.25 + 0.75 * edgeFade);
  result.red = std::clamp(1.45 * emissivity, 0.0, 1.0);
  result.green = std::clamp(0.64 * emissivity * (1.0 - 0.18 * normalizedRadius), 0.0, 1.0);
  result.blue = std::clamp(0.18 * emissivity * (1.0 - 0.45 * normalizedRadius), 0.0, 1.0);
  return result;
}

SchwarzschildRayResult integrateSchwarzschildRay(
    double mass,
    double impactParameter,
    double angularStep) {
  if (mass <= 0.0 || impactParameter <= 0.0 || angularStep <= 0.0) {
    throw std::invalid_argument("mass, impact parameter, and angular step must be positive");
  }
  SchwarzschildRayResult result{};
  result.impactParameter = impactParameter;
  if (impactParameter <= std::sqrt(27.0) * mass) return result;

  const double u0 = closestU(mass, impactParameter);
  result.closestApproach = 1.0 / u0;
  State state{u0, 0.0};
  double phi = 0.0;
  constexpr uint32_t kMaximumSteps = 200000;
  for (uint32_t step = 0; step < kMaximumSteps; ++step) {
    const State next = advanceRk4(state, mass, angularStep);
    if (next.u <= 0.0) {
      const double portion = state.u / (state.u - next.u);
      const double crossing = phi + angularStep * portion;
      result.escaped = true;
      result.deflectionRadians = 2.0 * crossing - std::numbers::pi;
      result.integrationSteps = step + 1;
      return result;
    }
    state = next;
    phi += angularStep;
  }
  return result;
}

}  // namespace vulkax::relativity
