#include "vulkax/render/gaussian_scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VULKAX_HAS_VULKAN_GAUSSIAN_SCHEDULER
#define VULKAX_HAS_VULKAN_GAUSSIAN_SCHEDULER 0
#endif
#ifndef VULKAX_HAS_METAL_GAUSSIAN_SCHEDULER
#define VULKAX_HAS_METAL_GAUSSIAN_SCHEDULER 0
#endif

namespace vulkax::render {

#if VULKAX_HAS_VULKAN_GAUSSIAN_SCHEDULER
GaussianNativeScheduleResult scheduleGaussianProjectionVulkan(
    const GaussianNativeProjectionResult& projection);
#endif
#if VULKAX_HAS_METAL_GAUSSIAN_SCHEDULER
GaussianNativeScheduleResult scheduleGaussianProjectionMetal(
    const GaussianNativeProjectionResult& projection);
#endif

namespace {

struct TileReferenceEntry {
    std::uint32_t tileId{};
    std::uint32_t projectedSplatIndex{};
    float depth{};
};

[[nodiscard]] std::uint32_t decodeTile(
    float encoded,
    std::uint32_t limit,
    const char* label) {
    if (!std::isfinite(encoded))
        throw std::invalid_argument(std::string("Gaussian schedule received non-finite ") + label);
    const long rounded = std::lround(encoded);
    if (rounded < 0L || static_cast<unsigned long>(rounded) >= static_cast<unsigned long>(limit) ||
        std::abs(encoded - static_cast<float>(rounded)) > 1.0e-4F)
        throw std::invalid_argument(std::string("Gaussian schedule received invalid ") + label);
    return static_cast<std::uint32_t>(rounded);
}

[[nodiscard]] std::size_t tileCount(const GaussianNativeProjectionResult& projection) {
    if (projection.tileSize == 0U || projection.tileColumns == 0U || projection.tileRows == 0U)
        throw std::invalid_argument("Gaussian schedule requires a positive tile grid");
    if (projection.projected.size() != projection.stats.visibleSplats)
        throw std::invalid_argument("Gaussian schedule projection visible count is inconsistent");
    if (projection.projected.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Gaussian schedule exceeds uint32 projected-splat indexing");
    return static_cast<std::size_t>(projection.tileColumns) * projection.tileRows;
}

[[nodiscard]] bool referenceLess(const TileReferenceEntry& lhs, const TileReferenceEntry& rhs) {
    if (lhs.tileId != rhs.tileId) return lhs.tileId < rhs.tileId;
    if (lhs.depth != rhs.depth) return lhs.depth > rhs.depth;
    return lhs.projectedSplatIndex < rhs.projectedSplatIndex;
}

void validateDepthOrder(
    const GaussianNativeScheduleResult& result,
    const GaussianNativeProjectionResult& projection) {
    if (result.depthOrder.size() != projection.projected.size())
        throw std::invalid_argument("Gaussian native scheduler depth-order count is inconsistent");
    if (result.paddedOrderCount == 0U ||
        (result.paddedOrderCount & (result.paddedOrderCount - 1U)) != 0U ||
        result.paddedOrderCount < std::max<std::size_t>(projection.projected.size(), 1U))
        throw std::invalid_argument("Gaussian native scheduler padded order count is invalid");

    std::vector<bool> seen(projection.projected.size(), false);
    float previousDepth = std::numeric_limits<float>::infinity();
    std::uint32_t previousIndex = 0U;
    bool havePrevious = false;
    for (const std::uint32_t index : result.depthOrder) {
        if (index >= projection.projected.size())
            throw std::invalid_argument("Gaussian native scheduler depth order references an invalid splat");
        if (seen[index])
            throw std::invalid_argument("Gaussian native scheduler depth order is not a permutation");
        seen[index] = true;
        const float depth = projection.projected[index].minorDepth[2];
        if (!std::isfinite(depth) || !(depth > 0.0F))
            throw std::invalid_argument("Gaussian native scheduler received a non-finite visible depth");
        if (havePrevious) {
            if (depth > previousDepth)
                throw std::invalid_argument("Gaussian native scheduler depth order is not back-to-front");
            if (depth == previousDepth && index < previousIndex)
                throw std::invalid_argument("Gaussian native scheduler violates deterministic equal-depth order");
        }
        previousDepth = depth;
        previousIndex = index;
        havePrevious = true;
    }
}

} // namespace

GaussianTileSchedule buildGaussianTileScheduleOracle(
    const GaussianNativeProjectionResult& projection) {
    const std::size_t tiles = tileCount(projection);

    GaussianTileSchedule schedule;
    schedule.tileSize = projection.tileSize;
    schedule.columns = projection.tileColumns;
    schedule.rows = projection.tileRows;
    schedule.tileOffsets.assign(tiles + 1U, 0U);

    std::vector<TileReferenceEntry> entries;
    if (projection.splatReferences > 0U) entries.reserve(projection.splatReferences);

    for (std::size_t splat = 0U; splat < projection.projected.size(); ++splat) {
        const auto& projected = projection.projected[splat];
        const float depth = projected.minorDepth[2];
        if (!std::isfinite(depth) || !(depth > 0.0F))
            throw std::invalid_argument("Gaussian schedule received a visible splat with invalid depth");

        const std::uint32_t firstColumn = decodeTile(
            projected.tileBounds[0], projection.tileColumns, "first tile column");
        const std::uint32_t lastColumn = decodeTile(
            projected.tileBounds[1], projection.tileColumns, "last tile column");
        const std::uint32_t firstRow = decodeTile(
            projected.tileBounds[2], projection.tileRows, "first tile row");
        const std::uint32_t lastRow = decodeTile(
            projected.tileBounds[3], projection.tileRows, "last tile row");
        if (firstColumn > lastColumn || firstRow > lastRow)
            throw std::invalid_argument("Gaussian schedule received an inverted tile range");

        for (std::uint32_t row = firstRow; row <= lastRow; ++row) {
            for (std::uint32_t column = firstColumn; column <= lastColumn; ++column) {
                const std::uint32_t tileId = row * projection.tileColumns + column;
                entries.push_back({
                    tileId,
                    static_cast<std::uint32_t>(splat),
                    depth,
                });
            }
        }
    }

    if (entries.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Gaussian tile schedule exceeds uint32 CSR offset capacity");

    std::sort(entries.begin(), entries.end(), referenceLess);
    schedule.splatReferences = entries.size();
    schedule.projectedSplatIndices.reserve(entries.size());

    for (const auto& entry : entries) {
        ++schedule.tileOffsets[static_cast<std::size_t>(entry.tileId) + 1U];
        schedule.projectedSplatIndices.push_back(entry.projectedSplatIndex);
    }
    for (std::size_t tile = 0U; tile < tiles; ++tile) {
        const std::uint32_t count = schedule.tileOffsets[tile + 1U];
        schedule.maximumSplatsPerTile = std::max(
            schedule.maximumSplatsPerTile,
            static_cast<std::size_t>(count));
        schedule.tileOffsets[tile + 1U] += schedule.tileOffsets[tile];
    }

    validateGaussianTileSchedule(schedule, projection);
    return schedule;
}

void validateGaussianTileSchedule(
    const GaussianTileSchedule& schedule,
    const GaussianNativeProjectionResult& projection) {
    const std::size_t tiles = tileCount(projection);
    if (schedule.tileSize != projection.tileSize ||
        schedule.columns != projection.tileColumns ||
        schedule.rows != projection.tileRows)
        throw std::invalid_argument("Gaussian tile schedule grid does not match projection");
    if (schedule.tileOffsets.size() != tiles + 1U)
        throw std::invalid_argument("Gaussian tile schedule CSR offset count is invalid");
    if (schedule.tileOffsets.front() != 0U)
        throw std::invalid_argument("Gaussian tile schedule CSR must start at zero");
    if (schedule.splatReferences != schedule.projectedSplatIndices.size())
        throw std::invalid_argument("Gaussian tile schedule reference count is inconsistent");
    if (schedule.tileOffsets.back() != schedule.projectedSplatIndices.size())
        throw std::invalid_argument("Gaussian tile schedule CSR terminal offset is inconsistent");

    std::size_t maximumOccupancy = 0U;
    for (std::size_t tile = 0U; tile < tiles; ++tile) {
        const std::uint32_t begin = schedule.tileOffsets[tile];
        const std::uint32_t end = schedule.tileOffsets[tile + 1U];
        if (begin > end || end > schedule.projectedSplatIndices.size())
            throw std::invalid_argument("Gaussian tile schedule CSR offsets are not monotonic");
        maximumOccupancy = std::max(
            maximumOccupancy,
            static_cast<std::size_t>(end - begin));

        const std::uint32_t row = static_cast<std::uint32_t>(
            tile / static_cast<std::size_t>(schedule.columns));
        const std::uint32_t column = static_cast<std::uint32_t>(
            tile % static_cast<std::size_t>(schedule.columns));

        float previousDepth = std::numeric_limits<float>::infinity();
        std::uint32_t previousIndex = 0U;
        bool havePrevious = false;
        for (std::uint32_t reference = begin; reference < end; ++reference) {
            const std::uint32_t splatIndex = schedule.projectedSplatIndices[reference];
            if (splatIndex >= projection.projected.size())
                throw std::invalid_argument("Gaussian tile schedule references an invalid projected splat");
            const auto& projected = projection.projected[splatIndex];
            const std::uint32_t firstColumn = decodeTile(
                projected.tileBounds[0], projection.tileColumns, "first tile column");
            const std::uint32_t lastColumn = decodeTile(
                projected.tileBounds[1], projection.tileColumns, "last tile column");
            const std::uint32_t firstRow = decodeTile(
                projected.tileBounds[2], projection.tileRows, "first tile row");
            const std::uint32_t lastRow = decodeTile(
                projected.tileBounds[3], projection.tileRows, "last tile row");
            if (column < firstColumn || column > lastColumn || row < firstRow || row > lastRow)
                throw std::invalid_argument("Gaussian tile schedule reference is outside its projected tile range");

            const float depth = projected.minorDepth[2];
            if (havePrevious) {
                if (depth > previousDepth)
                    throw std::invalid_argument("Gaussian tile schedule is not back-to-front ordered");
                if (depth == previousDepth && splatIndex < previousIndex)
                    throw std::invalid_argument("Gaussian tile schedule violates deterministic equal-depth order");
            }
            previousDepth = depth;
            previousIndex = splatIndex;
            havePrevious = true;
        }
    }

    if (maximumOccupancy != schedule.maximumSplatsPerTile)
        throw std::invalid_argument("Gaussian tile schedule maximum occupancy is inconsistent");
    if (projection.splatReferences != 0U && schedule.splatReferences != projection.splatReferences)
        throw std::invalid_argument("Gaussian tile schedule reference count disagrees with projection statistics");
    if (projection.maximumSplatsPerTile != 0U &&
        schedule.maximumSplatsPerTile != projection.maximumSplatsPerTile)
        throw std::invalid_argument("Gaussian tile schedule maximum occupancy disagrees with projection statistics");
}

GaussianNativeScheduleResult scheduleGaussianProjectionNative(
    backend::BackendKind backend,
    const GaussianNativeProjectionResult& projection) {
    (void)tileCount(projection);
    if (projection.splatReferences > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Gaussian native scheduler reference capacity exceeds uint32");

    GaussianNativeScheduleResult result;
    switch (backend) {
        case backend::BackendKind::Vulkan:
#if VULKAX_HAS_VULKAN_GAUSSIAN_SCHEDULER
            result = scheduleGaussianProjectionVulkan(projection);
            break;
#else
            throw std::runtime_error("Vulkan Gaussian scheduler was not compiled into this build");
#endif
        case backend::BackendKind::Metal:
#if VULKAX_HAS_METAL_GAUSSIAN_SCHEDULER
            result = scheduleGaussianProjectionMetal(projection);
            break;
#else
            throw std::runtime_error("Metal Gaussian scheduler was not compiled into this build");
#endif
        case backend::BackendKind::OpenGL:
            throw std::runtime_error("OpenGL Gaussian scheduler is not implemented");
    }

    validateDepthOrder(result, projection);
    validateGaussianTileSchedule(result.schedule, projection);
    return result;
}

} // namespace vulkax::render
