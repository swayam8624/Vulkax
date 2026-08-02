#include "vulkax/sim/sparse_brick_storage.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
  using namespace vulkax::sim;
  SparseBrickStorage storage({16, 12, 8, 4, 2});
  assert(storage.validate());
  assert(storage.payload().empty());

  storage.setCell(3, 5, 2, 0, 1.0f);
  storage.setCell(3, 5, 2, 1, 0.4f);
  assert(storage.stats().residentBricks == 1);
  storage.setCell(12, 1, 1, 0, 0.0f);
  const double initialMass = storage.sum(0);
  storage.transferCell(3, 5, 2, 4, 5, 2, 0, 0.35f);
  assert(storage.stats().residentBricks == 3);
  assert(std::abs(storage.sum(0) - initialMass) < 1e-7);
  assert(std::abs(storage.cell(3, 5, 2, 0) - 0.65f) < 1e-7f);
  assert(std::abs(storage.cell(4, 5, 2, 0) - 0.35f) < 1e-7f);

  const size_t targetPage = (static_cast<size_t>(0) * storage.brickCountY() + 1u) *
      storage.brickCountX() + 1u;
  assert(storage.pageTable()[targetPage] == 2u);
  assert(storage.deactivate({3, 0, 0}));
  assert(storage.stats().slotMigrations == 1);
  assert(storage.pageTable()[targetPage] == 1u);
  assert(storage.resident({1, 1, 0}));
  assert(storage.validate());

  const uint32_t refined = storage.refineHalo(1);
  assert(refined > 0);
  const auto refinedStats = storage.stats();
  assert(refinedStats.residentBricks < refinedStats.logicalBricks);
  const uint32_t removed = storage.coarsen(0.0f);
  assert(removed > 0);
  assert(storage.resident({0, 1, 0}));
  assert(storage.resident({1, 1, 0}));
  assert(!storage.resident({3, 0, 0}));
  assert(std::abs(storage.sum(0) - initialMass) < 1e-7);
  assert(std::abs(storage.sum(1) - 0.4) < 1e-7);
  assert(storage.validate());

  const SparseBrickStats stats = storage.stats();
  const uint64_t densePayloadBytes = stats.denseCells * storage.config().channels * sizeof(float);
  assert(stats.payloadBytes < densePayloadBytes / 2u);
  assert(stats.pageTableBytes < densePayloadBytes / 16u);

  bool rejected = false;
  try {
    storage.transferCell(3, 5, 2, 4, 5, 2, 0, 2.0f);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);

  std::cout << "Vulkax sparse brick storage tests passed: resident="
            << stats.residentBricks << '/' << stats.logicalBricks
            << " payload_bytes=" << stats.payloadBytes
            << " dense_bytes=" << densePayloadBytes
            << " migrations=" << stats.slotMigrations << '\n';
  return 0;
}
