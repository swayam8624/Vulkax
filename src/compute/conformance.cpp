#include "vulkax/compute/conformance.hpp"

#include <stdexcept>

#ifndef VULKAX_HAS_VULKAN_COMPUTE
#define VULKAX_HAS_VULKAN_COMPUTE 0
#endif
#ifndef VULKAX_HAS_METAL_COMPUTE
#define VULKAX_HAS_METAL_COMPUTE 0
#endif

namespace vulkax::compute {

#if VULKAX_HAS_VULKAN_COMPUTE
ConformanceResult runVulkanConformance(std::size_t elementCount);
#endif
#if VULKAX_HAS_METAL_COMPUTE
ConformanceResult runMetalConformance(std::size_t elementCount);
#endif

std::vector<backend::BackendKind> availableConformanceBackends() {
    std::vector<backend::BackendKind> result;
#if VULKAX_HAS_VULKAN_COMPUTE
    result.push_back(backend::BackendKind::Vulkan);
#endif
#if VULKAX_HAS_METAL_COMPUTE
    result.push_back(backend::BackendKind::Metal);
#endif
    return result;
}

ConformanceResult runConformance(backend::BackendKind backend, std::size_t elementCount) {
    if (elementCount == 0) {
        throw std::invalid_argument("compute conformance requires at least one element");
    }
    switch (backend) {
    case backend::BackendKind::Vulkan:
#if VULKAX_HAS_VULKAN_COMPUTE
        return runVulkanConformance(elementCount);
#else
        throw std::runtime_error("Vulkan compute conformance was not built on this machine");
#endif
    case backend::BackendKind::Metal:
#if VULKAX_HAS_METAL_COMPUTE
        return runMetalConformance(elementCount);
#else
        throw std::runtime_error("Metal compute conformance was not built on this machine");
#endif
    case backend::BackendKind::OpenGL:
        throw std::runtime_error("OpenGL compute conformance is not implemented yet");
    }
    throw std::logic_error("unknown backend kind");
}

} // namespace vulkax::compute
