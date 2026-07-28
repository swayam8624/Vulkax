#include "vulkax/relativity/schwarzschild_lensing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace vulkax::relativity {
namespace {

struct State { double u; double derivative; };

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
