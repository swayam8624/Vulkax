#pragma once

#include "vulkax/runtime_contract.h"

#include <cstddef>
#include <cmath>
#include <span>
#include <stdexcept>

namespace vulkax::runtime {

static_assert(sizeof(VulkaxFrameRequest) == 56);
static_assert(offsetof(VulkaxFrameRequest, frameIndex) == 16);
static_assert(offsetof(VulkaxFrameRequest, parameterHash) == 48);

inline void validateFrameRequest(
    const VulkaxFrameRequest& request,
    std::span<const float> parameters) {
  if (request.abiVersion != VULKAX_RUNTIME_ABI_VERSION) {
    throw std::invalid_argument("unsupported Vulkax runtime ABI");
  }
  if (request.drawableWidth == 0 || request.drawableHeight == 0) {
    throw std::invalid_argument("runtime frame extent must be non-zero");
  }
  if (request.parameterCount != parameters.size() ||
      request.parameterCount > VULKAX_RUNTIME_MAX_PARAMETERS) {
    throw std::invalid_argument("runtime parameter block does not match its frame request");
  }
  if (request.visualization > VULKAX_VISUALIZATION_VOLUME) {
    throw std::invalid_argument("runtime visualization kind is invalid");
  }
  if (!std::isfinite(request.timelineSeconds) || !std::isfinite(request.deltaSeconds) ||
      !std::isfinite(request.renderScale) || request.deltaSeconds < 0.0f ||
      request.renderScale <= 0.0f) {
    throw std::invalid_argument("runtime frame timing and scale must be finite and valid");
  }
}

}  // namespace vulkax::runtime
