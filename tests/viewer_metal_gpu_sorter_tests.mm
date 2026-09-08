#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "vulkax/viewer/gpu_sort_contract.hpp"
#include "vulkax/viewer/metal_gpu_sorter.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

#include <simd/simd.h>

namespace {

struct GPUSplat {
    simd_float4 positionScaleX{};
    simd_float4 scaleYZOpacity{};
    simd_float4 rotation{};
    simd_float4 colorMark{};
};

[[nodiscard]] std::vector<vulkax::viewer::GpuDepthKey> cpuReference(
    const std::vector<GPUSplat>& splats,
    vulkax::math::Vec3 eye,
    vulkax::math::Vec3 forward) {
    forward = vulkax::math::normalized(forward);
    std::vector<std::uint32_t> order(splats.size());
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(), [&](std::uint32_t lhs, std::uint32_t rhs) {
        const auto lp = splats[lhs].positionScaleX;
        const auto rp = splats[rhs].positionScaleX;
        const vulkax::math::Vec3 l{lp.x, lp.y, lp.z};
        const vulkax::math::Vec3 r{rp.x, rp.y, rp.z};
        const double ld = vulkax::math::dot(l - eye, forward);
        const double rd = vulkax::math::dot(r - eye, forward);
        if (ld != rd) return ld > rd;
        return lhs < rhs;
    });

    std::vector<vulkax::viewer::GpuDepthKey> keys;
    keys.reserve(order.size());
    for (const auto sourceIndex : order) {
        const auto p = splats[sourceIndex].positionScaleX;
        const vulkax::math::Vec3 position{p.x, p.y, p.z};
        keys.push_back({static_cast<float>(vulkax::math::dot(position - eye, forward)), sourceIndex});
    }
    return keys;
}

} // namespace

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::cerr << "No Metal device available\n";
            return 1;
        }

        vulkax::viewer::MetalGpuSorter sorter(device);
        if (!sorter.available()) {
            std::cerr << sorter.error() << '\n';
            return 1;
        }

        std::vector<GPUSplat> splats(33);
        for (std::uint32_t i = 0; i < splats.size(); ++i) {
            const float x = static_cast<float>((i * 7U) % 11U) - 5.0F;
            const float y = static_cast<float>((i * 13U) % 9U) * 0.25F - 1.0F;
            const float z = -1.0F - static_cast<float>((i * 17U) % 29U) * 0.37F;
            splats[i].positionScaleX = (simd_float4){x, y, z, 0.04F};
            splats[i].scaleYZOpacity = (simd_float4){0.04F, 0.04F, 0.9F, 0.0F};
            splats[i].rotation = (simd_float4){0.0F, 0.0F, 0.0F, 1.0F};
        }
        // Create exact depth ties so the source-index tie-breaker is exercised.
        splats[3].positionScaleX.z = -6.0F;
        splats[21].positionScaleX.z = -6.0F;

        id<MTLBuffer> splatBuffer = [device newBufferWithBytes:splats.data()
                                                       length:splats.size() * sizeof(GPUSplat)
                                                      options:MTLResourceStorageModeShared];
        assert(splatBuffer != nil);

        std::vector<std::uint32_t> retained(splats.size());
        std::iota(retained.begin(), retained.end(), 0U);
        // The GPU handoff is deliberately not pre-sorted.
        std::rotate(retained.begin(), retained.begin() + 9, retained.end());

        const vulkax::math::Vec3 eye{0.0, 0.0, 0.0};
        const vulkax::math::Vec3 forward{0.0, 0.0, -1.0};
        const auto result = sorter.sort(splatBuffer, retained, eye, forward);
        if (!result.success) {
            std::cerr << result.error << '\n';
            return 1;
        }
        assert(result.order.size() == retained.size());
        assert(result.depthKeys.size() == retained.size());

        const auto reference = cpuReference(splats, eye, forward);
        const auto validation = vulkax::viewer::validateGpuDepthKeys(result.depthKeys, reference, 2.0e-5);
        if (!validation.valid()) {
            std::cerr << "Reusable MetalGpuSorter parity failed: max error "
                      << validation.maximumDepthError << '\n';
        }
        assert(validation.valid());

        // A retained subset must preserve exactly that subset, not accidentally
        // pull padded sentinels or omitted source indices into the result.
        std::vector<std::uint32_t> subset{31U, 2U, 17U, 8U, 4U, 25U, 1U};
        const auto subsetResult = sorter.sort(splatBuffer, subset,
                                              {1.0, 1.5, 2.0}, {-1.0, -1.5, -5.0});
        if (!subsetResult.success) {
            std::cerr << subsetResult.error << '\n';
            return 1;
        }
        auto actualSet = subsetResult.order;
        auto expectedSet = subset;
        std::sort(actualSet.begin(), actualSet.end());
        std::sort(expectedSet.begin(), expectedSet.end());
        assert(actualSet == expectedSet);

        return 0;
    }
}
