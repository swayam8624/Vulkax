#pragma once

#include "vulkax/backend/backend.hpp"

#include <vector>

namespace vulkax::backend {

// Runtime discovery is backend-specific. A backend is returned only when its probe can identify a
// usable device. Policy and discovery are intentionally separate so no OS check silently invents
// capabilities.
[[nodiscard]] std::vector<BackendCapabilities> probeAvailableBackends();

} // namespace vulkax::backend
