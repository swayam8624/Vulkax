#pragma once

#include "vulkax/runtime_contract.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vulkax::editor {

struct GpuFieldRequest {
  uint32_t width = 0;
  uint32_t height = 0;
  float time = 0.0f;
  float amplitude = 1.0f;
  float wavenumber = 2.0f;
  float angularFrequency = 3.0f;
};

struct GpuFieldResult {
  std::vector<float> values;
  double dispatchMilliseconds = -1.0;
  // The same dispatch also produces a GPU-resident RGBA16F visualization
  // image. It is intentionally not read back for interactive display.
  bool hdrFrameProduced = false;
};

struct GpuHdrFrame {
  uint32_t width = 0;
  uint32_t height = 0;
  // Linear RGBA radiance decoded from the canonical Vulkan RGBA16F image.
  std::vector<float> rgba;
};

struct GpuReactionConfig {
  uint32_t width = 0;
  uint32_t height = 0;
  float timestep = 1.0f / 60.0f;
  float diffusionA = 1.0f;
  float diffusionB = 0.5f;
  float feed = 0.0367f;
  float kill = 0.0649f;
};

struct GpuReactionResult {
  std::vector<float> primary;
  std::vector<float> secondary;
  double dispatchMilliseconds = -1.0;
};

// A persistent compute-only Vulkan context for editor scalar previews. Wave
// evaluation writes a device-local RGBA16F frame after simulation. The scalar
// readback returned by evaluateWave exists only for numerical validation and
// the temporary Qt image-provider bridge; it is not the renderer's canonical
// frame representation.
class VulkanFieldExecutor final {
 public:
  VulkanFieldExecutor();
  ~VulkanFieldExecutor();

  VulkanFieldExecutor(const VulkanFieldExecutor&) = delete;
  VulkanFieldExecutor& operator=(const VulkanFieldExecutor&) = delete;

  [[nodiscard]] bool available() const;
  [[nodiscard]] const std::string& diagnostic() const;
  [[nodiscard]] const std::string& deviceName() const;
  [[nodiscard]] VulkaxRuntimeCapabilities runtimeCapabilities() const;
  [[nodiscard]] VulkaxFrameTelemetry latestTelemetry() const;

  // Dispatches the checked Wave Field and its GPU visualization pass. Returns
  // a CPU-visible scalar copy only for validation/legacy UI bridging. Throws
  // only for requests that exceed capacity; device failures switch explicitly
  // to fallback.
  [[nodiscard]] GpuFieldResult evaluateWave(const GpuFieldRequest& request);

  // Offline/export-only readback of the device-local HDR frame. Interactive
  // presentation must not call this method.
  [[nodiscard]] GpuHdrFrame readHdrFrame();

  // Keeps the Gray-Scott state resident between calls. The provided seed
  // fields must match the requested extent exactly. resetReaction performs no
  // dispatch; stepReaction is the only operation that advances simulation.
  void resetReaction(
      const GpuReactionConfig& config,
      const std::vector<float>& initialPrimary,
      const std::vector<float>& initialSecondary);
  [[nodiscard]] GpuReactionResult stepReaction(uint32_t steps);
  [[nodiscard]] bool reactionReady() const;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace vulkax::editor
