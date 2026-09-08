#include "vulkax/viewer/gpu_sort_contract.hpp"

#include <cassert>
#include <limits>
#include <vector>

int main() {
    using vulkax::viewer::GpuDepthKey;
    using vulkax::viewer::validateGpuDepthKeys;

    const std::vector<GpuDepthKey> reference{{8.0F, 2U}, {5.0F, 1U}, {2.0F, 0U}};

    const auto exact = validateGpuDepthKeys(reference, reference);
    assert(exact.valid());
    assert(exact.maximumDepthError == 0.0);

    const std::vector<GpuDepthKey> smallError{{8.000001F, 2U}, {4.999999F, 1U}, {2.000001F, 0U}};
    assert(validateGpuDepthKeys(smallError, reference, 1.0e-4).valid());

    const std::vector<GpuDepthKey> wrongOrder{{2.0F, 0U}, {5.0F, 1U}, {8.0F, 2U}};
    const auto order = validateGpuDepthKeys(wrongOrder, reference);
    assert(!order.valid());
    assert(!order.backToFront);

    const std::vector<GpuDepthKey> wrongIndex{{8.0F, 77U}, {5.0F, 1U}, {2.0F, 0U}};
    const auto index = validateGpuDepthKeys(wrongIndex, reference);
    assert(!index.valid());
    assert(!index.sourceIndicesMatch);

    const std::vector<GpuDepthKey> wrongDepth{{8.2F, 2U}, {5.0F, 1U}, {2.0F, 0U}};
    const auto depth = validateGpuDepthKeys(wrongDepth, reference, 1.0e-4);
    assert(!depth.valid());
    assert(!depth.sourceIndicesMatch);
    assert(depth.maximumDepthError > 0.19);

    const std::vector<GpuDepthKey> nonFinite{{std::numeric_limits<float>::quiet_NaN(), 2U},
                                              {5.0F, 1U}, {2.0F, 0U}};
    const auto finite = validateGpuDepthKeys(nonFinite, reference);
    assert(!finite.valid());
    assert(!finite.finiteDepths);

    const std::vector<GpuDepthKey> wrongSize{{8.0F, 2U}, {5.0F, 1U}};
    const auto size = validateGpuDepthKeys(wrongSize, reference);
    assert(!size.valid());
    assert(!size.sizeMatches);

    return 0;
}
