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
// Equal-depth ties preserve the original projected-splat index. This is the
// exact deterministic contract native GPU schedulers must reproduce.
struct GaussianTileSchedule {
    std::uint32_t tileSize{};
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::size_t splatReferences{};
    std::size_t maximumSplatsPerTile{};
    std::vector<std::uint32_t> tileOffsets;
    std::vector<std::uint32_t> projectedSplatIndices;
};

struct GaussianNativeScheduleResult {
    GaussianTileSchedule schedule;
    // Global deterministic permutation of projected splats, ordered by depth
    // descending and original projected index ascending for equal depths.
    std::vector<std::uint32_t> depthOrder;
    std::size_t paddedOrderCount{};
    double schedulingMilliseconds{};
    std::size_t inputBytes{};
    std::size_t outputBytes{};
    std::size_t workspaceBytes{};
};

// Correctness oracle for the 1.1 GPU-resident scheduler. This intentionally
// uses host-side sorting so Vulkan/Metal implementations have a simple,
// inspectable reference independent of their parallel ordering/binning code.
[[nodiscard]] GaussianTileSchedule buildGaussianTileScheduleOracle(
    const GaussianNativeProjectionResult& projection);

// Performs structural and semantic checks: CSR shape, reference bounds,
// tile membership, back-to-front depth order, deterministic equal-depth ties,
// and agreement with projection-level occupancy statistics.
void validateGaussianTileSchedule(
    const GaussianTileSchedule& schedule,
    const GaussianNativeProjectionResult& projection);

// Native 1.1A ordering + CSR stage. The input projection may be in arbitrary
// visible-splat order. Vulkan/Metal first construct a deterministic depth-order
// permutation on-device, then build tile counts, prefix offsets, and references
// from that permutation. Projected records themselves are not shuffled.
[[nodiscard]] GaussianNativeScheduleResult scheduleGaussianProjectionNative(
    backend::BackendKind backend,
    const GaussianNativeProjectionResult& projection);

} // namespace vulkax::render
