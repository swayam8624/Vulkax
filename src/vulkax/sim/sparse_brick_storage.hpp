#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace vulkax::sim {

struct SparseBrickConfig {
  uint32_t width = 64;
  uint32_t height = 64;
  uint32_t depth = 64;
  uint32_t brickSize = 4;
  uint32_t channels = 2;
};

struct SparseBrickCoord {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t z = 0;

  bool operator==(const SparseBrickCoord&) const = default;
};

struct SparseBrickStats {
  uint32_t residentBricks = 0;
  uint32_t logicalBricks = 0;
  uint64_t residentCells = 0;
  uint64_t denseCells = 0;
  uint64_t payloadBytes = 0;
  uint64_t pageTableBytes = 0;
  uint64_t activations = 0;
  uint64_t deactivations = 0;
  uint64_t slotMigrations = 0;
};

// Compact sparse storage for cell-centred fluid fields. The page table maps a
// logical brick to a tightly packed payload slot; inactive bricks own no field
// payload. Removing a brick migrates the last slot into the hole and rewrites
// its page-table entry, keeping the buffers directly uploadable to a GPU.
class SparseBrickStorage {
 public:
  static constexpr uint32_t kInactiveSlot = std::numeric_limits<uint32_t>::max();

  explicit SparseBrickStorage(SparseBrickConfig config = {});

  [[nodiscard]] const SparseBrickConfig& config() const { return config_; }
  [[nodiscard]] uint32_t brickCountX() const { return brickCountX_; }
  [[nodiscard]] uint32_t brickCountY() const { return brickCountY_; }
  [[nodiscard]] uint32_t brickCountZ() const { return brickCountZ_; }
  [[nodiscard]] uint32_t cellsPerBrick() const { return cellsPerBrick_; }
  [[nodiscard]] bool resident(SparseBrickCoord brick) const;
  [[nodiscard]] float cell(
      uint32_t x, uint32_t y, uint32_t z, uint32_t channel) const;
  [[nodiscard]] double sum(uint32_t channel) const;
  [[nodiscard]] bool validate() const;
  [[nodiscard]] SparseBrickStats stats() const;

  uint32_t activate(SparseBrickCoord brick);
  bool deactivate(SparseBrickCoord brick, float maximumAbsoluteValue = 0.0f);
  uint32_t refineHalo(uint32_t brickRadius = 1);
  uint32_t coarsen(float maximumAbsoluteValue);

  void setCell(
      uint32_t x, uint32_t y, uint32_t z, uint32_t channel, float value);
  void addCell(
      uint32_t x, uint32_t y, uint32_t z, uint32_t channel, float value);
  void transferCell(
      uint32_t sourceX,
      uint32_t sourceY,
      uint32_t sourceZ,
      uint32_t targetX,
      uint32_t targetY,
      uint32_t targetZ,
      uint32_t channel,
      float amount);

  [[nodiscard]] const std::vector<uint32_t>& pageTable() const { return pageTable_; }
  [[nodiscard]] const std::vector<SparseBrickCoord>& slotBricks() const {
    return slotBricks_;
  }
  [[nodiscard]] const std::vector<float>& payload() const { return payload_; }

 private:
  [[nodiscard]] size_t pageIndex(SparseBrickCoord brick) const;
  [[nodiscard]] SparseBrickCoord brickForCell(uint32_t x, uint32_t y, uint32_t z) const;
  [[nodiscard]] size_t payloadIndex(
      uint32_t slot,
      uint32_t x,
      uint32_t y,
      uint32_t z,
      uint32_t channel) const;
  void validateCell(uint32_t x, uint32_t y, uint32_t z, uint32_t channel) const;

  SparseBrickConfig config_;
  uint32_t brickCountX_ = 0;
  uint32_t brickCountY_ = 0;
  uint32_t brickCountZ_ = 0;
  uint32_t cellsPerBrick_ = 0;
  std::vector<uint32_t> pageTable_;
  std::vector<SparseBrickCoord> slotBricks_;
  std::vector<float> payload_;
  uint64_t activations_ = 0;
  uint64_t deactivations_ = 0;
  uint64_t slotMigrations_ = 0;
};

}  // namespace vulkax::sim
