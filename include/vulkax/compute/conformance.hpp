#pragma once

#include "vulkax/backend/backend.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace vulkax::compute {

struct ConformanceResult {
    backend::BackendKind backend{backend::BackendKind::Vulkan};
    std::string deviceName;
    std::size_t elementCount{};
    double maxAbsoluteError{};
    double maxRelativeError{};
    bool passed{false};
};

[[nodiscard]] std::vector<backend::BackendKind> availableConformanceBackends();
[[nodiscard]] ConformanceResult runConformance(backend::BackendKind backend,
                                               std::size_t elementCount = 4096);

} // namespace vulkax::compute
