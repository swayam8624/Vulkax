#include "vulkax/relativity/kerr_jacobi.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace vulkax::relativity {
namespace {

using Vector4 = std::array<double, 4>;
using Matrix4 = std::array<std::array<double, 4>, 4>;
using Connection = std::array<std::array<std::array<double, 4>, 4>, 4>;
using Curvature = std::array<std::array<std::array<std::array<double, 4>, 4>, 4>, 4>;

struct BundleState {
  Vector4 position{};
  Vector4 wave{};
  Vector4 horizontal{};
  Vector4 horizontalDerivative{};
  Vector4 vertical{};
  Vector4 verticalDerivative{};
};

Matrix4 metric(double mass, double spin, const Vector4& x) {
  const double radius = x[1];
  const double polar = std::clamp(x[2], 1e-5, 3.14159265358979323846 - 1e-5);
  const double sine = std::sin(polar);
  const double cosine = std::cos(polar);
  const double sine2 = sine * sine;
  const double sigma = radius * radius + spin * spin * cosine * cosine;
  const double delta = radius * radius - 2.0 * mass * radius + spin * spin;
  Matrix4 result{};
  result[0][0] = -(1.0 - 2.0 * mass * radius / sigma);
  result[0][3] = result[3][0] = -2.0 * mass * spin * radius * sine2 / sigma;
  result[1][1] = sigma / delta;
  result[2][2] = sigma;
  result[3][3] =
      sine2 * (radius * radius + spin * spin + 2.0 * mass * spin * spin * radius * sine2 / sigma);
  return result;
}

Matrix4 inverse(Matrix4 value) {
  Matrix4 result{};
  for (size_t row = 0; row < 4; ++row) result[row][row] = 1.0;
  for (size_t column = 0; column < 4; ++column) {
    size_t pivot = column;
    for (size_t row = column + 1; row < 4; ++row) {
      if (std::abs(value[row][column]) > std::abs(value[pivot][column])) pivot = row;
    }
    if (std::abs(value[pivot][column]) < 1e-18) {
      throw std::runtime_error("singular Kerr metric during Jacobi integration");
    }
    std::swap(value[pivot], value[column]);
    std::swap(result[pivot], result[column]);
    const double divisor = value[column][column];
    for (size_t entry = 0; entry < 4; ++entry) {
      value[column][entry] /= divisor;
      result[column][entry] /= divisor;
    }
    for (size_t row = 0; row < 4; ++row) {
      if (row == column) continue;
      const double scale = value[row][column];
      for (size_t entry = 0; entry < 4; ++entry) {
        value[row][entry] -= scale * value[column][entry];
        result[row][entry] -= scale * result[column][entry];
      }
    }
  }
  return result;
}

double derivativeStep(const Vector4& x, size_t coordinate) {
  if (coordinate == 0 || coordinate == 3) return 1e-5;
  return 2e-5 * std::max(1.0, std::abs(x[coordinate]));
}

Matrix4 metricDerivative(double mass, double spin, const Vector4& x, size_t coordinate) {
  if (coordinate == 0 || coordinate == 3) return {};
  const double step = derivativeStep(x, coordinate);
  Vector4 positive = x;
  Vector4 negative = x;
  positive[coordinate] += step;
  negative[coordinate] -= step;
  const Matrix4 upper = metric(mass, spin, positive);
  const Matrix4 lower = metric(mass, spin, negative);
  Matrix4 result{};
  for (size_t row = 0; row < 4; ++row)
    for (size_t column = 0; column < 4; ++column)
      result[row][column] = (upper[row][column] - lower[row][column]) / (2.0 * step);
  return result;
}

Connection connection(double mass, double spin, const Vector4& x) {
  const Matrix4 inverseMetric = inverse(metric(mass, spin, x));
  std::array<Matrix4, 4> derivatives{};
  for (size_t coordinate = 0; coordinate < 4; ++coordinate)
    derivatives[coordinate] = metricDerivative(mass, spin, x, coordinate);
  Connection result{};
  for (size_t mu = 0; mu < 4; ++mu)
    for (size_t alpha = 0; alpha < 4; ++alpha)
      for (size_t beta = 0; beta < 4; ++beta)
        for (size_t sigma = 0; sigma < 4; ++sigma)
          result[mu][alpha][beta] +=
              0.5 * inverseMetric[mu][sigma] *
              (derivatives[alpha][sigma][beta] + derivatives[beta][sigma][alpha] -
               derivatives[sigma][alpha][beta]);
  return result;
}

Curvature curvature(double mass, double spin, const Vector4& x) {
  const Connection center = connection(mass, spin, x);
  std::array<Connection, 4> derivatives{};
  for (size_t coordinate = 0; coordinate < 4; ++coordinate) {
    if (coordinate == 0 || coordinate == 3) continue;
    const double step = derivativeStep(x, coordinate) * 2.0;
    Vector4 positive = x;
    Vector4 negative = x;
    positive[coordinate] += step;
    negative[coordinate] -= step;
    const Connection upper = connection(mass, spin, positive);
    const Connection lower = connection(mass, spin, negative);
    for (size_t mu = 0; mu < 4; ++mu)
      for (size_t nu = 0; nu < 4; ++nu)
        for (size_t beta = 0; beta < 4; ++beta)
          derivatives[coordinate][mu][nu][beta] =
              (upper[mu][nu][beta] - lower[mu][nu][beta]) / (2.0 * step);
  }
  Curvature result{};
  for (size_t mu = 0; mu < 4; ++mu)
    for (size_t nu = 0; nu < 4; ++nu)
      for (size_t alpha = 0; alpha < 4; ++alpha)
        for (size_t beta = 0; beta < 4; ++beta) {
          result[mu][nu][alpha][beta] =
              derivatives[alpha][mu][nu][beta] - derivatives[beta][mu][nu][alpha];
          for (size_t sigma = 0; sigma < 4; ++sigma)
            result[mu][nu][alpha][beta] += center[mu][sigma][alpha] * center[sigma][nu][beta] -
                                           center[mu][sigma][beta] * center[sigma][nu][alpha];
        }
  return result;
}

Vector4 launchWave(const KerrGeodesicConfig& config, double alpha, double beta) {
  const KerrConstants constants = kerrConstantsFromImagePlane(config, alpha, beta);
  const double radius = config.observerRadius;
  const double polar = config.observerInclinationRadians;
  const double radius2 = radius * radius;
  const double sine = std::sin(polar);
  const double cosine = std::cos(polar);
  const double sine2 = std::max(sine * sine, 1e-12);
  const double sigma = radius2 + config.spin * config.spin * cosine * cosine;
  const double delta = radius2 - 2.0 * config.mass * radius + config.spin * config.spin;
  const double p = constants.energy * (radius2 + config.spin * config.spin) -
                   config.spin * constants.axialAngularMomentum;
  const double shifted = constants.axialAngularMomentum - config.spin * constants.energy;
  const double radialPotential = p * p - delta * (shifted * shifted + constants.carterConstant);
  const double polarPotential =
      constants.carterConstant + config.spin * config.spin * cosine * cosine -
      constants.axialAngularMomentum * constants.axialAngularMomentum * cosine * cosine / sine2;
  return {
      (-config.spin * (config.spin * sine2 - constants.axialAngularMomentum) +
       (radius2 + config.spin * config.spin) * p / delta) /
          sigma,
      -std::sqrt(std::max(0.0, radialPotential)) / sigma,
      (beta >= 0.0 ? 1.0 : -1.0) * std::sqrt(std::max(0.0, polarPotential)) / sigma,
      (constants.axialAngularMomentum / sine2 - config.spin + config.spin * p / delta) / sigma};
}

Vector4 launchDerivative(
    const KerrGeodesicConfig& config,
    double alpha,
    double beta,
    double differential,
    bool horizontal) {
  const Vector4 positive = launchWave(
      config,
      alpha + (horizontal ? differential : 0.0),
      beta + (horizontal ? 0.0 : differential));
  const Vector4 negative = launchWave(
      config,
      alpha - (horizontal ? differential : 0.0),
      beta - (horizontal ? 0.0 : differential));
  Vector4 result{};
  for (size_t index = 0; index < 4; ++index)
    result[index] = (positive[index] - negative[index]) / (2.0 * differential);
  return result;
}

BundleState stateDerivative(
    double mass, double spin, const BundleState& state, double* tidalMagnitude) {
  const Connection gamma = connection(mass, spin, state.position);
  const Curvature riemann = curvature(mass, spin, state.position);
  BundleState result{};
  result.position = state.wave;
  for (size_t mu = 0; mu < 4; ++mu)
    for (size_t alpha = 0; alpha < 4; ++alpha)
      for (size_t beta = 0; beta < 4; ++beta)
        result.wave[mu] -= gamma[mu][alpha][beta] * state.wave[alpha] * state.wave[beta];

  const auto transport = [&](const Vector4& deviation,
                             const Vector4& covariant,
                             Vector4& dDeviation,
                             Vector4& dCovariant) {
    for (size_t mu = 0; mu < 4; ++mu) {
      dDeviation[mu] = covariant[mu];
      for (size_t alpha = 0; alpha < 4; ++alpha)
        for (size_t beta = 0; beta < 4; ++beta)
          dDeviation[mu] -= gamma[mu][alpha][beta] * state.wave[alpha] * deviation[beta];
      double tidal = 0.0;
      for (size_t nu = 0; nu < 4; ++nu)
        for (size_t alpha = 0; alpha < 4; ++alpha)
          for (size_t beta = 0; beta < 4; ++beta)
            tidal -=
                riemann[mu][nu][alpha][beta] * state.wave[nu] * deviation[alpha] * state.wave[beta];
      dCovariant[mu] = tidal;
      for (size_t alpha = 0; alpha < 4; ++alpha)
        for (size_t beta = 0; beta < 4; ++beta)
          dCovariant[mu] -= gamma[mu][alpha][beta] * state.wave[alpha] * covariant[beta];
      *tidalMagnitude = std::max(*tidalMagnitude, std::abs(tidal));
    }
  };
  transport(
      state.horizontal,
      state.horizontalDerivative,
      result.horizontal,
      result.horizontalDerivative);
  transport(state.vertical, state.verticalDerivative, result.vertical, result.verticalDerivative);
  return result;
}

BundleState add(const BundleState& left, const BundleState& right, double scale) {
  BundleState result = left;
  const auto addVector = [&](Vector4& target, const Vector4& delta) {
    for (size_t index = 0; index < 4; ++index) target[index] += scale * delta[index];
  };
  addVector(result.position, right.position);
  addVector(result.wave, right.wave);
  addVector(result.horizontal, right.horizontal);
  addVector(result.horizontalDerivative, right.horizontalDerivative);
  addVector(result.vertical, right.vertical);
  addVector(result.verticalDerivative, right.verticalDerivative);
  return result;
}

BundleState rk4(double mass, double spin, const BundleState& state, double step, double* tidal) {
  const BundleState k1 = stateDerivative(mass, spin, state, tidal);
  const BundleState k2 = stateDerivative(mass, spin, add(state, k1, 0.5 * step), tidal);
  const BundleState k3 = stateDerivative(mass, spin, add(state, k2, 0.5 * step), tidal);
  const BundleState k4 = stateDerivative(mass, spin, add(state, k3, step), tidal);
  BundleState result = state;
  const auto integrate =
      [&](Vector4& target, const Vector4& a, const Vector4& b, const Vector4& c, const Vector4& d) {
        for (size_t index = 0; index < 4; ++index)
          target[index] += step * (a[index] + 2.0 * b[index] + 2.0 * c[index] + d[index]) / 6.0;
      };
  integrate(result.position, k1.position, k2.position, k3.position, k4.position);
  integrate(result.wave, k1.wave, k2.wave, k3.wave, k4.wave);
  integrate(result.horizontal, k1.horizontal, k2.horizontal, k3.horizontal, k4.horizontal);
  integrate(
      result.horizontalDerivative,
      k1.horizontalDerivative,
      k2.horizontalDerivative,
      k3.horizontalDerivative,
      k4.horizontalDerivative);
  integrate(result.vertical, k1.vertical, k2.vertical, k3.vertical, k4.vertical);
  integrate(
      result.verticalDerivative,
      k1.verticalDerivative,
      k2.verticalDerivative,
      k3.verticalDerivative,
      k4.verticalDerivative);
  return result;
}

double nullConstraint(double mass, double spin, const BundleState& state) {
  const Matrix4 g = metric(mass, spin, state.position);
  double sum = 0.0;
  double scale = 0.0;
  for (size_t mu = 0; mu < 4; ++mu)
    for (size_t nu = 0; nu < 4; ++nu) {
      const double term = g[mu][nu] * state.wave[mu] * state.wave[nu];
      sum += term;
      scale += std::abs(term);
    }
  return std::abs(sum) / std::max(scale, 1e-30);
}

bool finite(const BundleState& state) {
  const auto finiteVector = [](const Vector4& value) {
    return std::all_of(value.begin(), value.end(), [](double component) {
      return std::isfinite(component);
    });
  };
  return finiteVector(state.position) && finiteVector(state.wave) &&
         finiteVector(state.horizontal) && finiteVector(state.horizontalDerivative) &&
         finiteVector(state.vertical) && finiteVector(state.verticalDerivative);
}

}  // namespace

KerrJacobiResult integrateKerrJacobiBundle(
    const KerrGeodesicConfig& geodesic, double alpha, double beta, const KerrJacobiConfig& jacobi) {
  if (!(jacobi.affineStep > 0.0) || !(jacobi.affineDistance > 0.0) ||
      !(jacobi.launchDifferential > 0.0) || jacobi.maximumSteps == 0)
    throw std::invalid_argument("invalid Kerr Jacobi integration configuration");
  const double horizon = kerrOuterHorizonRadius(geodesic.mass, geodesic.spin) *
                         (1.0 + geodesic.horizonRelativeEpsilon);
  BundleState state{};
  state.position = {0.0, geodesic.observerRadius, geodesic.observerInclinationRadians, 0.0};
  state.wave = launchWave(geodesic, alpha, beta);
  state.horizontalDerivative =
      launchDerivative(geodesic, alpha, beta, jacobi.launchDifferential, true);
  state.verticalDerivative =
      launchDerivative(geodesic, alpha, beta, jacobi.launchDifferential, false);

  KerrJacobiResult result{};
  for (uint32_t index = 0;
       index < jacobi.maximumSteps && result.integratedAffineDistance < jacobi.affineDistance;
       ++index) {
    if (state.position[1] <= horizon) {
      result.status = KerrRayStatus::Captured;
      break;
    }
    const double step =
        std::min(jacobi.affineStep, jacobi.affineDistance - result.integratedAffineDistance);
    state = rk4(geodesic.mass, geodesic.spin, state, step, &result.maximumTidalAcceleration);
    if (!finite(state)) {
      result.status = KerrRayStatus::Invalid;
      break;
    }
    result.maximumNullConstraint =
        std::max(result.maximumNullConstraint, nullConstraint(geodesic.mass, geodesic.spin, state));
    result.integratedAffineDistance += step;
    ++result.acceptedSteps;
  }
  if (result.status == KerrRayStatus::Unfinished &&
      result.integratedAffineDistance >= jacobi.affineDistance)
    result.status = KerrRayStatus::Escaped;
  result.finalPosition = state.position;
  result.finalWaveVector = state.wave;
  result.horizontalDeviation = state.horizontal;
  result.verticalDeviation = state.vertical;
  result.horizontalCovariantDerivative = state.horizontalDerivative;
  result.verticalCovariantDerivative = state.verticalDerivative;
  const double sine = std::max(std::sin(state.position[2]), 1e-8);
  result.sourceJacobian = {
      state.horizontal[3] * sine,
      state.horizontal[2],
      state.vertical[3] * sine,
      state.vertical[2]};
  result.sourceAreaScale = std::abs(
      result.sourceJacobian[0] * result.sourceJacobian[3] -
      result.sourceJacobian[1] * result.sourceJacobian[2]);
  result.valid = result.status != KerrRayStatus::Invalid && result.acceptedSteps > 0 &&
                 std::isfinite(result.sourceAreaScale) &&
                 std::isfinite(result.maximumTidalAcceleration) &&
                 result.maximumNullConstraint < 1e-4;
  return result;
}

double kerrKretschmannScalar(double mass, double spin, double radius, double polarRadians) {
  static_cast<void>(kerrOuterHorizonRadius(mass, spin));
  const double cosine = std::cos(polarRadians);
  const double radius2 = radius * radius;
  const double a2c2 = spin * spin * cosine * cosine;
  const double numerator = 48.0 * mass * mass *
                           (std::pow(radius, 6.0) - 15.0 * std::pow(radius, 4.0) * a2c2 +
                            15.0 * radius2 * a2c2 * a2c2 - a2c2 * a2c2 * a2c2);
  return numerator / std::pow(radius2 + a2c2, 6.0);
}

}  // namespace vulkax::relativity
