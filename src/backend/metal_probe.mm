#include "vulkax/backend/backend.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <vector>

namespace vulkax::backend {

std::vector<BackendCapabilities> probeMetalBackends() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            return {};
        }

        BackendCapabilities capability;
        capability.kind = BackendKind::Metal;
        capability.available = true;
        capability.nativePlatformBackend = true;
        capability.dedicatedGpu = !device.isLowPower && !device.hasUnifiedMemory;
        capability.driverQuality = 1.0;
        capability.deviceMemoryBytes = static_cast<std::uint64_t>(device.recommendedMaxWorkingSetSize);
        capability.deviceName = device.name.UTF8String != nullptr ? device.name.UTF8String : "Metal device";
        capability.features = {Feature::Compute, Feature::StorageBuffers, Feature::StorageImages,
                               Feature::Atomics, Feature::Float16, Feature::Headless};
        return {std::move(capability)};
    }
}

} // namespace vulkax::backend
