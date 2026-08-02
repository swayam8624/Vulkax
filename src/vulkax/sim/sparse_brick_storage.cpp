#include "vulkax/sim/sparse_brick_storage.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::sim {
namespace {

uint32_t ceilDivide(uint32_t value, uint32_t divisor) {
  return (value + divisor - 1u) / divisor;
}

}  // namespace

SparseBrickStorage::SparseBrickStorage(SparseBrickConfig config) : config_(config) {
  if (config_.width == 0 || config_.height == 0 || config_.depth == 0 ||
      config_.brickSize == 0 || config_.channels == 0) {
    throw std::invalid_argument("sparse brick storage requires positive extents and channels");
  }
  if (config_.brickSize > 32) {
    throw std::invalid_argument("sparse brick size must not exceed 32 cells");
  }
  brickCountX_ = ceilDivide(config_.width, config_.brickSize);
  brickCountY_ = ceilDivide(config_.height, config_.brickSize);
  brickCountZ_ = ceilDivide(config_.depth, config_.brickSize);
  cellsPerBrick_ = config_.brickSize * config_.brickSize * config_.brickSize;
  pageTable_.assign(
      static_cast<size_t>(brickCountX_) * brickCountY_ * brickCountZ_, kInactiveSlot);
}

size_t SparseBrickStorage::pageIndex(SparseBrickCoord brick) const {
  if (brick.x >= brickCountX_ || brick.y >= brickCountY_ || brick.z >= brickCountZ_) {
    throw std::out_of_range("sparse brick coordinate is outside the logical domain");
  }
  return (static_cast<size_t>(brick.z) * brickCountY_ + brick.y) * brickCountX_ + brick.x;
}

SparseBrickCoord SparseBrickStorage::brickForCell(
    uint32_t x, uint32_t y, uint32_t z) const {
  return {x / config_.brickSize, y / config_.brickSize, z / config_.brickSize};
}

void SparseBrickStorage::validateCell(
    uint32_t x, uint32_t y, uint32_t z, uint32_t channel) const {
  if (x >= config_.width || y >= config_.height || z >= config_.depth ||
      channel >= config_.channels) {
    throw std::out_of_range("sparse fluid cell is outside the configured field");
  }
}

size_t SparseBrickStorage::payloadIndex(
    uint32_t slot,
    uint32_t x,
    uint32_t y,
    uint32_t z,
    uint32_t channel) const {
  const uint32_t localX = x % config_.brickSize;
  const uint32_t localY = y % config_.brickSize;
  const uint32_t localZ = z % config_.brickSize;
  const uint32_t localCell =
      (localZ * config_.brickSize + localY) * config_.brickSize + localX;
  return (static_cast<size_t>(slot) * cellsPerBrick_ + localCell) * config_.channels +
      channel;
}

bool SparseBrickStorage::resident(SparseBrickCoord brick) const {
  return pageTable_[pageIndex(brick)] != kInactiveSlot;
}

uint32_t SparseBrickStorage::activate(SparseBrickCoord brick) {
  const size_t page = pageIndex(brick);
  if (pageTable_[page] != kInactiveSlot) return pageTable_[page];
  const uint32_t slot = static_cast<uint32_t>(slotBricks_.size());
  pageTable_[page] = slot;
  slotBricks_.push_back(brick);
  payload_.resize(
      payload_.size() + static_cast<size_t>(cellsPerBrick_) * config_.channels, 0.0f);
  ++activations_;
  return slot;
}

bool SparseBrickStorage::deactivate(
    SparseBrickCoord brick, float maximumAbsoluteValue) {
  if (maximumAbsoluteValue < 0.0f) {
    throw std::invalid_argument("sparse coarsening threshold must be non-negative");
  }
  const size_t page = pageIndex(brick);
  const uint32_t slot = pageTable_[page];
  if (slot == kInactiveSlot) return false;
  const size_t valuesPerBrick = static_cast<size_t>(cellsPerBrick_) * config_.channels;
  const auto begin = payload_.begin() + static_cast<std::ptrdiff_t>(slot * valuesPerBrick);
  const auto end = begin + static_cast<std::ptrdiff_t>(valuesPerBrick);
  if (std::any_of(begin, end, [&](float value) {
        return std::abs(value) > maximumAbsoluteValue;
      })) {
    return false;
  }

  const uint32_t lastSlot = static_cast<uint32_t>(slotBricks_.size() - 1u);
  if (slot != lastSlot) {
    const auto lastBegin = payload_.begin() +
        static_cast<std::ptrdiff_t>(static_cast<size_t>(lastSlot) * valuesPerBrick);
    std::copy(lastBegin, lastBegin + static_cast<std::ptrdiff_t>(valuesPerBrick), begin);
    slotBricks_[slot] = slotBricks_[lastSlot];
    pageTable_[pageIndex(slotBricks_[slot])] = slot;
    ++slotMigrations_;
  }
  payload_.resize(payload_.size() - valuesPerBrick);
  slotBricks_.pop_back();
  pageTable_[page] = kInactiveSlot;
  ++deactivations_;
  return true;
}

float SparseBrickStorage::cell(
    uint32_t x, uint32_t y, uint32_t z, uint32_t channel) const {
  validateCell(x, y, z, channel);
  const uint32_t slot = pageTable_[pageIndex(brickForCell(x, y, z))];
  if (slot == kInactiveSlot) return 0.0f;
  return payload_[payloadIndex(slot, x, y, z, channel)];
}

void SparseBrickStorage::setCell(
    uint32_t x, uint32_t y, uint32_t z, uint32_t channel, float value) {
  validateCell(x, y, z, channel);
  if (!std::isfinite(value)) throw std::invalid_argument("sparse fluid value must be finite");
  const SparseBrickCoord brick = brickForCell(x, y, z);
  const uint32_t slot = activate(brick);
  payload_[payloadIndex(slot, x, y, z, channel)] = value;
}

void SparseBrickStorage::addCell(
    uint32_t x, uint32_t y, uint32_t z, uint32_t channel, float value) {
  setCell(x, y, z, channel, cell(x, y, z, channel) + value);
}

void SparseBrickStorage::transferCell(
    uint32_t sourceX,
    uint32_t sourceY,
    uint32_t sourceZ,
    uint32_t targetX,
    uint32_t targetY,
    uint32_t targetZ,
    uint32_t channel,
    float amount) {
  if (!(amount >= 0.0f) || !std::isfinite(amount)) {
    throw std::invalid_argument("sparse mass transfer must be finite and non-negative");
  }
  const float source = cell(sourceX, sourceY, sourceZ, channel);
  if (amount > source) throw std::invalid_argument("sparse mass transfer exceeds source value");
  setCell(sourceX, sourceY, sourceZ, channel, source - amount);
  addCell(targetX, targetY, targetZ, channel, amount);
}

uint32_t SparseBrickStorage::refineHalo(uint32_t brickRadius) {
  if (brickRadius == 0 || slotBricks_.empty()) return 0;
  const std::vector<SparseBrickCoord> source = slotBricks_;
  const uint64_t before = activations_;
  for (const SparseBrickCoord brick : source) {
    const int radius = static_cast<int>(brickRadius);
    for (int z = -radius; z <= radius; ++z) {
      for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
          const int bx = static_cast<int>(brick.x) + x;
          const int by = static_cast<int>(brick.y) + y;
          const int bz = static_cast<int>(brick.z) + z;
          if (bx < 0 || by < 0 || bz < 0 || bx >= static_cast<int>(brickCountX_) ||
              by >= static_cast<int>(brickCountY_) || bz >= static_cast<int>(brickCountZ_)) {
            continue;
          }
          activate({static_cast<uint32_t>(bx), static_cast<uint32_t>(by),
                    static_cast<uint32_t>(bz)});
        }
      }
    }
  }
  return static_cast<uint32_t>(activations_ - before);
}

uint32_t SparseBrickStorage::coarsen(float maximumAbsoluteValue) {
  if (maximumAbsoluteValue < 0.0f) {
    throw std::invalid_argument("sparse coarsening threshold must be non-negative");
  }
  uint32_t removed = 0;
  for (size_t slot = slotBricks_.size(); slot > 0; --slot) {
    if (deactivate(slotBricks_[slot - 1u], maximumAbsoluteValue)) ++removed;
  }
  return removed;
}

double SparseBrickStorage::sum(uint32_t channel) const {
  if (channel >= config_.channels) throw std::out_of_range("sparse channel is out of range");
  double result = 0.0;
  for (size_t index = channel; index < payload_.size(); index += config_.channels) {
    result += payload_[index];
  }
  return result;
}

bool SparseBrickStorage::validate() const {
  const size_t valuesPerBrick = static_cast<size_t>(cellsPerBrick_) * config_.channels;
  if (payload_.size() != slotBricks_.size() * valuesPerBrick) return false;
  std::vector<uint8_t> seen(slotBricks_.size(), 0u);
  for (size_t page = 0; page < pageTable_.size(); ++page) {
    const uint32_t slot = pageTable_[page];
    if (slot == kInactiveSlot) continue;
    if (slot >= slotBricks_.size() || seen[slot] != 0u) return false;
    seen[slot] = 1u;
    if (pageIndex(slotBricks_[slot]) != page) return false;
  }
  return std::all_of(seen.begin(), seen.end(), [](uint8_t value) { return value == 1u; }) &&
      std::all_of(payload_.begin(), payload_.end(), [](float value) {
        return std::isfinite(value);
      });
}

SparseBrickStats SparseBrickStorage::stats() const {
  SparseBrickStats result{};
  result.residentBricks = static_cast<uint32_t>(slotBricks_.size());
  result.logicalBricks = static_cast<uint32_t>(pageTable_.size());
  result.residentCells = static_cast<uint64_t>(slotBricks_.size()) * cellsPerBrick_;
  result.denseCells = static_cast<uint64_t>(config_.width) * config_.height * config_.depth;
  result.payloadBytes = payload_.size() * sizeof(float);
  result.pageTableBytes = pageTable_.size() * sizeof(uint32_t);
  result.activations = activations_;
  result.deactivations = deactivations_;
  result.slotMigrations = slotMigrations_;
  return result;
}

}  // namespace vulkax::sim
