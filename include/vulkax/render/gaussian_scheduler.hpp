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
// reference emission later becomes parallel.
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
    double schedulingMilliseconds{};
    std::size_t inputBytes{};
    std::size_t outputBytes{};
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

// Native 1.1A CSR construction stage. The input projection is already
// depth-ordered by the established projection oracle; tile counting, prefix
// offsets, and reference emission are executed on the requested GPU backend.
// A later 1.1A stage will move the depth ordering itself onto the device.
[[nodiscard]] GaussianNativeScheduleResult scheduleGaussianProjectionNative(
    backend::BackendKind backend,
    const GaussianNativeProjectionResult& projection);

} // namespace vulkax::render
