#include "vulkax/sim/buoyant_smoke.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::sim {

BuoyantSmokeSimulation::BuoyantSmokeSimulation(BuoyantSmokeConfig config) : config_(config) {
  if (config_.width < 8 || config_.height < 8 || config_.timestepSeconds <= 0.0f ||
      config_.pressureIterations == 0 || config_.velocityDissipation <= 0.0f ||
      config_.densityDissipation <= 0.0f || config_.temperatureDissipation <= 0.0f) {
    throw std::invalid_argument("buoyant smoke requires an 8x8 positive deterministic grid");
  }
  const size_t count = static_cast<size_t>(config_.width) * config_.height;
  density_.resize(count);
  temperature_.resize(count);
  velocityX_.resize(count);
  velocityY_.resize(count);
  scratchDensity_.resize(count);
  scratchTemperature_.resize(count);
  scratchVelocityX_.resize(count);
  scratchVelocityY_.resize(count);
  pressure_.resize(count);
  pressureScratch_.resize(count);
  divergence_.resize(count);
  curl_.resize(count);
  reset();
}

size_t BuoyantSmokeSimulation::index(uint32_t x, uint32_t y) const {
  return static_cast<size_t>(y) * config_.width + x;
}

float BuoyantSmokeSimulation::sample(const std::vector<float>& field, float x, float y) const {
  x = std::clamp(x, 0.0f, static_cast<float>(config_.width - 1));
  y = std::clamp(y, 0.0f, static_cast<float>(config_.height - 1));
  const uint32_t x0 = static_cast<uint32_t>(x);
  const uint32_t y0 = static_cast<uint32_t>(y);
  const uint32_t x1 = std::min(config_.width - 1, x0 + 1);
  const uint32_t y1 = std::min(config_.height - 1, y0 + 1);
  const float tx = x - static_cast<float>(x0);
  const float ty = y - static_cast<float>(y0);
  const float lower = field[index(x0, y0)] * (1.0f - tx) + field[index(x1, y0)] * tx;
  const float upper = field[index(x0, y1)] * (1.0f - tx) + field[index(x1, y1)] * tx;
  return lower * (1.0f - ty) + upper * ty;
}

void BuoyantSmokeSimulation::reset() {
  for (auto* field : {&density_, &temperature_, &velocityX_, &velocityY_, &scratchDensity_,
                      &scratchTemperature_, &scratchVelocityX_, &scratchVelocityY_, &pressure_,
                      &pressureScratch_, &divergence_, &curl_}) {
    std::fill(field->begin(), field->end(), 0.0f);
  }
  timeSeconds_ = 0.0;
}

void BuoyantSmokeSimulation::enforceBoundary(
    std::vector<float>& field, bool horizontalVelocity, bool verticalVelocity) {
  for (uint32_t x = 0; x < config_.width; ++x) {
    field[index(x, 0)] = verticalVelocity ? 0.0f : field[index(x, 1)];
    field[index(x, config_.height - 1)] = verticalVelocity ? 0.0f : field[index(x, config_.height - 2)];
  }
  for (uint32_t y = 0; y < config_.height; ++y) {
    field[index(0, y)] = horizontalVelocity ? 0.0f : field[index(1, y)];
    field[index(config_.width - 1, y)] =
        horizontalVelocity ? 0.0f : field[index(config_.width - 2, y)];
  }
}

void BuoyantSmokeSimulation::injectEmitter() {
  const float centerX = static_cast<float>(config_.width - 1) * 0.5f;
  const float centerY = static_cast<float>(config_.height - 1) * 0.82f;
  const float radius = std::max(2.0f, static_cast<float>(config_.width) * 0.07f);
  for (uint32_t y = 1; y + 1 < config_.height; ++y) {
    for (uint32_t x = 1; x + 1 < config_.width; ++x) {
      const float dx = (static_cast<float>(x) - centerX) / radius;
      const float dy = (static_cast<float>(y) - centerY) / radius;
      const float source = std::exp(-0.5f * (dx * dx + dy * dy));
      const size_t cell = index(x, y);
      // A low, continuous source avoids clamping the plume into a uniformly
      // bright ribbon before the flow can develop visible vortices.
      density_[cell] = std::min(1.0f, density_[cell] + 0.018f * source);
      temperature_[cell] = std::min(1.0f, temperature_[cell] + 0.050f * source);
      velocityX_[cell] += 0.050f * std::sin(static_cast<float>(timeSeconds_) * 3.7f) * source;
      velocityY_[cell] -= 0.022f * source;
    }
  }
}

void BuoyantSmokeSimulation::addBuoyancy() {
  for (uint32_t y = 1; y + 1 < config_.height; ++y) {
    for (uint32_t x = 1; x + 1 < config_.width; ++x) {
      const size_t cell = index(x, y);
      const float force = config_.buoyancy * temperature_[cell] - config_.smokeWeight * density_[cell];
      // Screen/grid y grows downwards; negative velocity rises.
      velocityY_[cell] -= config_.timestepSeconds * force;
    }
  }
}

void BuoyantSmokeSimulation::applyVorticityConfinement() {
  for (uint32_t y = 1; y + 1 < config_.height; ++y) {
    for (uint32_t x = 1; x + 1 < config_.width; ++x) {
      curl_[index(x, y)] = 0.5f * ((velocityY_[index(x + 1, y)] - velocityY_[index(x - 1, y)]) -
                                   (velocityX_[index(x, y + 1)] - velocityX_[index(x, y - 1)]));
    }
  }
  for (uint32_t y = 2; y + 2 < config_.height; ++y) {
    for (uint32_t x = 2; x + 2 < config_.width; ++x) {
      const float gradientX = 0.5f * (std::abs(curl_[index(x + 1, y)]) - std::abs(curl_[index(x - 1, y)]));
      const float gradientY = 0.5f * (std::abs(curl_[index(x, y + 1)]) - std::abs(curl_[index(x, y - 1)]));
      const float magnitude = std::sqrt(gradientX * gradientX + gradientY * gradientY);
      if (magnitude <= 1e-7f) continue;
      const float normalX = gradientX / magnitude;
      const float normalY = gradientY / magnitude;
      const float force = config_.vorticityConfinement * curl_[index(x, y)] * config_.timestepSeconds;
      velocityX_[index(x, y)] += normalY * force;
      velocityY_[index(x, y)] -= normalX * force;
    }
  }
}

void BuoyantSmokeSimulation::advect(
    const std::vector<float>& source, std::vector<float>& destination, float dissipation) {
  const float dt = config_.timestepSeconds * std::min(config_.width, config_.height);
  for (uint32_t y = 0; y < config_.height; ++y) {
    for (uint32_t x = 0; x < config_.width; ++x) {
      const size_t cell = index(x, y);
      destination[cell] = dissipation * sample(
          source, static_cast<float>(x) - dt * velocityX_[cell], static_cast<float>(y) - dt * velocityY_[cell]);
    }
  }
}

void BuoyantSmokeSimulation::project() {
  constexpr float kScale = 0.5f;
  for (uint32_t y = 1; y + 1 < config_.height; ++y) {
    for (uint32_t x = 1; x + 1 < config_.width; ++x) {
      divergence_[index(x, y)] = -kScale * (
          velocityX_[index(x + 1, y)] - velocityX_[index(x - 1, y)] +
          velocityY_[index(x, y + 1)] - velocityY_[index(x, y - 1)]);
      pressure_[index(x, y)] = 0.0f;
    }
  }
  for (uint32_t iteration = 0; iteration < config_.pressureIterations; ++iteration) {
    for (uint32_t y = 1; y + 1 < config_.height; ++y) {
      for (uint32_t x = 1; x + 1 < config_.width; ++x) {
        pressureScratch_[index(x, y)] = 0.25f * (
            divergence_[index(x, y)] + pressure_[index(x - 1, y)] + pressure_[index(x + 1, y)] +
            pressure_[index(x, y - 1)] + pressure_[index(x, y + 1)]);
      }
    }
    pressure_.swap(pressureScratch_);
    enforceBoundary(pressure_, false, false);
  }
  for (uint32_t y = 1; y + 1 < config_.height; ++y) {
    for (uint32_t x = 1; x + 1 < config_.width; ++x) {
      velocityX_[index(x, y)] -= kScale * (pressure_[index(x + 1, y)] - pressure_[index(x - 1, y)]);
      velocityY_[index(x, y)] -= kScale * (pressure_[index(x, y + 1)] - pressure_[index(x, y - 1)]);
    }
  }
  enforceBoundary(velocityX_, true, false);
  enforceBoundary(velocityY_, false, true);
}

void BuoyantSmokeSimulation::step(uint32_t count) {
  for (uint32_t iteration = 0; iteration < count; ++iteration) {
    injectEmitter();
    addBuoyancy();
    applyVorticityConfinement();
    project();

    advect(velocityX_, scratchVelocityX_, config_.velocityDissipation);
    advect(velocityY_, scratchVelocityY_, config_.velocityDissipation);
    velocityX_.swap(scratchVelocityX_);
    velocityY_.swap(scratchVelocityY_);
    enforceBoundary(velocityX_, true, false);
    enforceBoundary(velocityY_, false, true);
    project();

    advect(density_, scratchDensity_, config_.densityDissipation);
    density_.swap(scratchDensity_);
    advect(temperature_, scratchTemperature_, config_.temperatureDissipation);
    temperature_.swap(scratchTemperature_);
    enforceBoundary(density_, false, false);
    enforceBoundary(temperature_, false, false);
    timeSeconds_ += config_.timestepSeconds;
  }
}

double BuoyantSmokeSimulation::divergenceL2() const {
  double sum = 0.0;
  size_t count = 0;
  for (uint32_t y = 1; y + 1 < config_.height; ++y) {
    for (uint32_t x = 1; x + 1 < config_.width; ++x) {
      const double divergence = 0.5 * (
          velocityX_[index(x + 1, y)] - velocityX_[index(x - 1, y)] +
          velocityY_[index(x, y + 1)] - velocityY_[index(x, y - 1)]);
      sum += divergence * divergence;
      ++count;
    }
  }
  return count == 0 ? 0.0 : std::sqrt(sum / static_cast<double>(count));
}

double BuoyantSmokeSimulation::densityMass() const {
  double result = 0.0;
  for (const float value : density_) result += value;
  return result;
}

double BuoyantSmokeSimulation::densityCenterY() const {
  double weightedY = 0.0;
  double mass = 0.0;
  for (uint32_t y = 0; y < config_.height; ++y) {
    for (uint32_t x = 0; x < config_.width; ++x) {
      const double weight = density_[index(x, y)];
      weightedY += static_cast<double>(y) * weight;
      mass += weight;
    }
  }
  return mass <= 1e-12 ? 0.0 : weightedY / mass;
}

}  // namespace vulkax::sim
