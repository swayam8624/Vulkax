#include "vulkax/backend/probe.hpp"

#include <iterator>
#include <vector>

#ifndef VULKAX_HAS_VULKAN
#define VULKAX_HAS_VULKAN 0
#endif
#ifndef VULKAX_HAS_METAL
#define VULKAX_HAS_METAL 0
#endif

namespace vulkax::backend {

#if VULKAX_HAS_VULKAN
std::vector<BackendCapabilities> probeVulkanBackends();
#endif
#if VULKAX_HAS_METAL
std::vector<BackendCapabilities> probeMetalBackends();
#endif

std::vector<BackendCapabilities> probeAvailableBackends() {
    std::vector<BackendCapabilities> result;
#if VULKAX_HAS_VULKAN
    auto vulkan = probeVulkanBackends();
    result.insert(result.end(), std::make_move_iterator(vulkan.begin()),
                  std::make_move_iterator(vulkan.end()));
#endif
#if VULKAX_HAS_METAL
    auto metal = probeMetalBackends();
    result.insert(result.end(), std::make_move_iterator(metal.begin()),
                  std::make_move_iterator(metal.end()));
#endif
    return result;
}

} // namespace vulkax::backend
