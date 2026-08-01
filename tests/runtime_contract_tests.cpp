#include "vulkax/runtime/runtime_contract.hpp"

#include <array>
#include <cassert>
#include <stdexcept>

namespace {

bool rejected(const VulkaxFrameRequest& request, std::span<const float> parameters) {
  try {
    vulkax::runtime::validateFrameRequest(request, parameters);
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  std::array<float, 4> parameters{1.0f, 2.0f, 3.0f, 4.0f};
  VulkaxFrameRequest request{};
  request.abiVersion = VULKAX_RUNTIME_ABI_VERSION;
  request.visualization = VULKAX_VISUALIZATION_SCALAR_FIELD;
  request.drawableWidth = 1920;
  request.drawableHeight = 1080;
  request.frameIndex = 7;
  request.timelineSeconds = 1.25f;
  request.deltaSeconds = 1.0f / 60.0f;
  request.renderScale = 1.0f;
  request.parameterCount = static_cast<uint32_t>(parameters.size());
  request.parameterHash = 0x1234u;
  vulkax::runtime::validateFrameRequest(request, parameters);

  auto invalid = request;
  invalid.abiVersion += 1;
  assert(rejected(invalid, parameters));
  invalid = request;
  invalid.drawableWidth = 0;
  assert(rejected(invalid, parameters));
  invalid = request;
  invalid.parameterCount -= 1;
  assert(rejected(invalid, parameters));
  invalid = request;
  invalid.visualization = 99;
  assert(rejected(invalid, parameters));
  invalid = request;
  invalid.renderScale = 0.0f;
  assert(rejected(invalid, parameters));
}
