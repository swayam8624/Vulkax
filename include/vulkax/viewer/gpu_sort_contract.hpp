#pragma once

#include <cstddef>
#include <cstdint>
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
[[nodiscard]] GpuSortValidation validateGpuDepthKeys(
    const std::vector<GpuDepthKey>& gpu,
    const std::vector<GpuDepthKey>& reference,
    double depthTolerance = 1.0e-5);

} // namespace vulkax::viewer
