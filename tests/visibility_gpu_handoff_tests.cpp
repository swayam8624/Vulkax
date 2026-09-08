#include "vulkax/viewer/visibility.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using vulkax::viewer::ViewerCamera;
    using vulkax::viewer::ViewerGaussian;
    using vulkax::viewer::VisibilitySettings;

    std::vector<ViewerGaussian> cloud(4);
    for (std::uint32_t i = 0; i < cloud.size(); ++i) {
        cloud[i].id = {5U, i + 1U};
        cloud[i].scale = {0.05F, 0.05F, 0.05F};
        cloud[i].opacity = 0.9F;
    }
    // Source order is deliberately not depth order.
    cloud[0].position = {0.0, 0.0, -2.0};
    cloud[1].position = {0.0, 0.0, -8.0};
    cloud[2].position = {0.0, 0.0, -5.0};
    cloud[3].position = {100.0, 0.0, -4.0}; // culled in either path

    ViewerCamera camera;
    camera.position = {0.0, 0.0, 0.0};
    camera.target = {0.0, 0.0, -1.0};
    camera.aspect = 1.0;
    camera.nearPlane = 0.1;
    camera.farPlane = 20.0;

    const auto sorted = vulkax::viewer::selectVisibleGaussians(cloud, camera);
    assert((sorted.order == std::vector<std::uint32_t>{1U, 2U, 0U}));
    assert(sorted.frustumRejected == 1U);

    VisibilitySettings handoff;
    handoff.sortBackToFront = false;
    const auto retained = vulkax::viewer::selectVisibleGaussians(cloud, camera, handoff);
    assert((retained.order == std::vector<std::uint32_t>{0U, 1U, 2U}));
    assert(retained.frustumRejected == sorted.frustumRejected);
    assert(retained.opacityRejected == sorted.opacityRejected);
    assert(retained.budgetRejected == sorted.budgetRejected);

    // LOD must retain the same source set regardless of who owns the final sort.
    cloud[0].scale = {0.30F, 0.30F, 0.30F};
    cloud[1].scale = {0.02F, 0.02F, 0.02F};
    cloud[2].scale = {0.22F, 0.22F, 0.22F};
    VisibilitySettings cpuBudget;
    cpuBudget.maxSplats = 2U;
    VisibilitySettings gpuBudget = cpuBudget;
    gpuBudget.sortBackToFront = false;

    const auto cpuLod = vulkax::viewer::selectVisibleGaussians(cloud, camera, cpuBudget);
    const auto gpuLod = vulkax::viewer::selectVisibleGaussians(cloud, camera, gpuBudget);
    assert(cpuLod.order.size() == 2U);
    assert(gpuLod.order.size() == 2U);
    auto cpuSet = cpuLod.order;
    auto gpuSet = gpuLod.order;
    std::sort(cpuSet.begin(), cpuSet.end());
    std::sort(gpuSet.begin(), gpuSet.end());
    assert(cpuSet == gpuSet);
    assert(cpuLod.budgetRejected == gpuLod.budgetRejected);

    return 0;
}
