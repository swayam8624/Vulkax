#ifndef VULKAX_RUNTIME_CONTRACT_H
#define VULKAX_RUNTIME_CONTRACT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VULKAX_RUNTIME_ABI_VERSION 1u
#define VULKAX_RUNTIME_MAX_PARAMETERS 16u

typedef enum VulkaxRuntimeBackendKind {
  VULKAX_RUNTIME_BACKEND_VULKAN = 0,
  VULKAX_RUNTIME_BACKEND_METAL = 1
} VulkaxRuntimeBackendKind;

typedef enum VulkaxVisualizationKind {
  VULKAX_VISUALIZATION_SCALAR_FIELD = 0,
  VULKAX_VISUALIZATION_RELATIVITY = 1,
  VULKAX_VISUALIZATION_VOLUME = 2
} VulkaxVisualizationKind;

// Language-neutral frame ABI shared by the Vulkan compatibility runtime and
// the native Metal editor. Parameter values are supplied in stable compiler
// order by the host; parameterHash identifies that ordered block.
typedef struct VulkaxFrameRequest {
  uint32_t abiVersion;
  uint32_t visualization;
  uint32_t drawableWidth;
  uint32_t drawableHeight;
  uint64_t frameIndex;
  float timelineSeconds;
  float deltaSeconds;
  float renderScale;
  uint32_t resetHistory;
  uint32_t parameterCount;
  uint32_t reserved;
  uint64_t parameterHash;
} VulkaxFrameRequest;

typedef struct VulkaxFrameTelemetry {
  uint32_t abiVersion;
  uint32_t backend;
  uint64_t frameIndex;
  double simulationMilliseconds;
  double renderingMilliseconds;
  uint32_t framesInFlight;
  uint32_t historySamples;
  uint32_t frameSubmitted;
  uint32_t framePresented;
} VulkaxFrameTelemetry;

typedef struct VulkaxRuntimeCapabilities {
  uint32_t abiVersion;
  uint32_t backend;
  uint32_t maximumFramesInFlight;
  uint32_t gpuResidentHdr;
  uint32_t asynchronousSubmission;
} VulkaxRuntimeCapabilities;

#ifdef __cplusplus
}
#endif

#endif
