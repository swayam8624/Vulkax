#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace vulkax::viewer {

struct GpuDepthKey {
    float depth{};
    std::uint32_t sourceIndex{};
};

struct GpuSortValidation {
    bool sizeMatches{};
    bool finiteDepths{};
    bool sourceIndicesMatch{};
    bool backToFront{};
    double maximumDepthError{};

    [[nodiscard]] bool valid() const noexcept {
        return sizeMatches && finiteDepths && sourceIndicesMatch && backToFront;
    }
};

// Compare GPU-generated depth keys against a CPU reference order. This stays in
// the reusable viewer layer so the Metal compute path can be validated without
// becoming part of the scientific state or renderer contracts.
[[nodiscard]] inline GpuSortValidation validateGpuDepthKeys(
    const std::vector<GpuDepthKey>& gpu,
    const std::vector<GpuDepthKey>& reference,
    double depthTolerance = 1.0e-5) {
    GpuSortValidation result;
    result.sizeMatches = gpu.size() == reference.size();
    result.finiteDepths = true;
    result.sourceIndicesMatch = result.sizeMatches;
    result.backToFront = true;
    result.maximumDepthError = 0.0;

    if (!result.sizeMatches) {
        result.sourceIndicesMatch = false;
        result.backToFront = false;
        return result;
    }

    std::unordered_map<std::uint32_t, double> expected;
    expected.reserve(reference.size());
    for (const auto& key : reference) {
        if (!std::isfinite(key.depth)) result.finiteDepths = false;
        expected[key.sourceIndex] = static_cast<double>(key.depth);
    }

    double previousDepth = std::numeric_limits<double>::infinity();
    for (const auto& key : gpu) {
        if (!std::isfinite(key.depth)) {
            result.finiteDepths = false;
            result.backToFront = false;
            continue;
        }
        const auto it = expected.find(key.sourceIndex);
        if (it == expected.end()) {
            result.sourceIndicesMatch = false;
            continue;
        }
        const double error = std::abs(static_cast<double>(key.depth) - it->second);
        result.maximumDepthError = std::max(result.maximumDepthError, error);
        if (error > depthTolerance) result.sourceIndicesMatch = false;
        if (static_cast<double>(key.depth) > previousDepth + depthTolerance) result.backToFront = false;
        previousDepth = static_cast<double>(key.depth);
    }

    return result;
}

} // namespace vulkax::viewer
