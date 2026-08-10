#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/render/camera.hpp"
#include "vulkax/render/headless.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace vulkax::render {

struct CaptureSettings {
    std::uint32_t width{1920};
    std::uint32_t height{1080};
    double fps{30.0};
    std::size_t frameCount{1};
    std::string outputDirectory{"vulkax-capture"};
    visualization::Color clearColor{0.01F,0.012F,0.018F,1.0F};
};

using ParticleFrameProvider=std::function<std::vector<visualization::ParticleInstance>(double timeSeconds)>;

struct CaptureResult {
    std::vector<std::string> framePaths;
    double durationSeconds{};
};

[[nodiscard]] CaptureResult captureParticleSequence(backend::BackendKind backend,
                                                     const ParticleFrameProvider& frameProvider,
                                                     const CameraTrack& cameraTrack,
                                                     const CaptureSettings& settings);

} // namespace vulkax::render
