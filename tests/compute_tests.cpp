#include "vulkax/compute/conformance.hpp"

#include <cassert>
#include <cmath>

int main() {
    const auto backends = vulkax::compute::availableConformanceBackends();
    for (const auto backend : backends) {
        const auto result = vulkax::compute::runConformance(backend, 1024);
        assert(result.elementCount == 1024);
        assert(!result.deviceName.empty());
        assert(std::isfinite(result.maxAbsoluteError));
        assert(std::isfinite(result.maxRelativeError));
        assert(result.passed);
    }
    return 0;
}
