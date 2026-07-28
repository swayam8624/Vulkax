#include "vulkax/sim/simulation_graph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vulkax::sim {

SimulationGraph::SimulationGraph(SimulationConfig config) : config_(config) {
  if (config_.width < 3 || config_.height < 3 || config_.timestepSeconds <= 0.0f ||
      config_.spatialStep <= 0.0f || config_.diffusionA < 0.0f || config_.diffusionB < 0.0f ||
      config_.feed < 0.0f || config_.kill < 0.0f) {
    throw std::invalid_argument("simulation graph requires a 3x3-or-larger positive grid");
  }
  const size_t count = static_cast<size_t>(config_.width) * config_.height;
  previous_.resize(count);
  current_.resize(count);
  next_.resize(count);
  secondary_.resize(count);
  nextSecondary_.resize(count);
  reset();
}

size_t SimulationGraph::index(uint32_t x, uint32_t y) const {
  return static_cast<size_t>(y) * config_.width + x;
}

float SimulationGraph::laplacian(const std::vector<float>& field, uint32_t x, uint32_t y) const {
  const float center = field[index(x, y)];
  return field[index(x - 1, y)] + field[index(x + 1, y)] +
         field[index(x, y - 1)] + field[index(x, y + 1)] - 4.0f * center;
}

void SimulationGraph::reset() {
  std::fill(previous_.begin(), previous_.end(), 0.0f);
  std::fill(current_.begin(), current_.end(), 0.0f);
  std::fill(next_.begin(), next_.end(), 0.0f);
  std::fill(secondary_.begin(), secondary_.end(), 0.0f);
  std::fill(nextSecondary_.begin(), nextSecondary_.end(), 0.0f);
  const float centerX = static_cast<float>(config_.width - 1) * 0.5f;
  const float centerY = static_cast<float>(config_.height - 1) * 0.5f;
  const float radius = std::min(config_.width, config_.height) * 0.11f;
  for (uint32_t y = 0; y < config_.height; ++y) {
    for (uint32_t x = 0; x < config_.width; ++x) {
      const float dx = (static_cast<float>(x) - centerX) / radius;
      const float dy = (static_cast<float>(y) - centerY) / radius;
      const float gaussian = std::exp(-0.5f * (dx * dx + dy * dy));
      current_[index(x, y)] = config_.kind == SimulationKind::Wave ? gaussian : 1.0f;
      previous_[index(x, y)] = current_[index(x, y)];
      secondary_[index(x, y)] = config_.kind == SimulationKind::ReactionDiffusion
          ? 0.25f * gaussian
          : 0.0f;
    }
  }
  timeSeconds_ = 0.0f;
}

void SimulationGraph::step(uint32_t count) {
  for (uint32_t iteration = 0; iteration < count; ++iteration) {
    if (config_.kind == SimulationKind::Wave) stepWave();
    else stepReactionDiffusion();
    timeSeconds_ += config_.timestepSeconds;
  }
}

void SimulationGraph::stepWave() {
  // c*dt/dx remains below the 2D explicit stability limit for defaults.
  constexpr float waveSpeed = 1.0f;
  const float coefficient = std::pow(waveSpeed * config_.timestepSeconds / config_.spatialStep, 2.0f);
  for (uint32_t y = 0; y < config_.height; ++y) {
    for (uint32_t x = 0; x < config_.width; ++x) {
      const size_t cell = index(x, y);
      if (x == 0 || y == 0 || x + 1 == config_.width || y + 1 == config_.height) {
        next_[cell] = 0.0f;
      } else {
        next_[cell] = 2.0f * current_[cell] - previous_[cell] + coefficient * laplacian(current_, x, y);
      }
    }
  }
  previous_.swap(current_);
  current_.swap(next_);
}

void SimulationGraph::stepReactionDiffusion() {
  for (uint32_t y = 0; y < config_.height; ++y) {
    for (uint32_t x = 0; x < config_.width; ++x) {
      const size_t cell = index(x, y);
      if (x == 0 || y == 0 || x + 1 == config_.width || y + 1 == config_.height) {
        next_[cell] = current_[cell];
        nextSecondary_[cell] = secondary_[cell];
        continue;
      }
      const float a = current_[cell];
      const float b = secondary_[cell];
      const float reaction = a * b * b;
      next_[cell] = std::clamp(
          a + (config_.diffusionA * laplacian(current_, x, y) - reaction +
               config_.feed * (1.0f - a)) * config_.timestepSeconds,
          0.0f, 1.0f);
      nextSecondary_[cell] = std::clamp(
          b + (config_.diffusionB * laplacian(secondary_, x, y) + reaction -
               (config_.kill + config_.feed) * b) * config_.timestepSeconds,
          0.0f, 1.0f);
    }
  }
  current_.swap(next_);
  secondary_.swap(nextSecondary_);
}

const std::vector<float>& SimulationGraph::primaryField() const { return current_; }
const std::vector<float>& SimulationGraph::secondaryField() const { return secondary_; }

FieldMetrics SimulationGraph::metrics() const {
  FieldMetrics result{};
  result.minimum = std::numeric_limits<float>::infinity();
  result.maximum = -std::numeric_limits<float>::infinity();
  for (const float value : current_) {
    result.minimum = std::min(result.minimum, value);
    result.maximum = std::max(result.maximum, value);
    result.mean += value;
    result.meanSquare += value * value;
  }
  result.mean /= static_cast<float>(current_.size());
  result.meanSquare /= static_cast<float>(current_.size());
  return result;
}

std::string SimulationGraph::glslComputeKernel() const {
  const std::string body = config_.kind == SimulationKind::Wave
      ? "nextField.values[index] = 2.0 * currentField.values[index] - previousField.values[index] + coefficient * laplace;"
      : "nextField.values[index] = clamp(a + (diffusionA * laplaceA - a*b*b + feed*(1.0-a))*dt, 0.0, 1.0);";
  return "#version 450\nlayout(local_size_x=16, local_size_y=16) in;\n"
         "// Generated from the deterministic SimulationGraph update contract.\n"
         "void main() { uint index = gl_GlobalInvocationID.y * width + gl_GlobalInvocationID.x; " + body + " }\n";
}

}  // namespace vulkax::sim
