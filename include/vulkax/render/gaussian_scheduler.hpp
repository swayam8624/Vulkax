#pragma once

#include "vulkax/render/gaussian_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vulkax::render {

// Deterministic compressed sparse-row tile schedule for projected Gaussians.
// tileOffsets has tileCount + 1 elements. References for tile t occupy
// [tileOffsets[t], tileOffsets[t + 1]) in projectedSplatIndices.
//
// Within every tile, references are ordered back-to-front by projected depth.
// Equal-depth ties preserve the input projected-splat order. That is the exact
// deterministic contract native GPU schedulers must reproduce even when their
// reference emission uses atomics.
struct GaussianTileSchedule {
    std::uint32_t tileSize{};
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::size_t splatReferences{};
    std::size_t maximumSplatsPerTile{};
    std::vector<std::uint32_t> tileOffsets;
    std::vector<std::uint32_t> projectedSplatIndices;
};

// Correctness oracle for the 1.1 GPU-resident scheduler. This intentionally
// uses host-side sorting so Vulkan/Metal implementations have a simple,
// inspectable reference independent of their scan/bin/sort implementation.
[[nodiscard]] GaussianTileSchedule buildGaussianTileScheduleOracle(
    const GaussianNativeProjectionResult& projection);

// Performs structural and semantic checks: CSR shape, reference bounds,
// tile membership, back-to-front depth order, deterministic equal-depth ties,
// and agreement with projection-level occupancy statistics.
void validateGaussianTileSchedule(
    const GaussianTileSchedule& schedule,
    const GaussianNativeProjectionResult& projection);

} // namespace vulkax::render
