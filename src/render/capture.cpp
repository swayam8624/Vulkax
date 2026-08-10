#include "vulkax/render/capture.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace vulkax::render {

CaptureResult captureParticleSequence(backend::BackendKind backend,const ParticleFrameProvider&frameProvider,const CameraTrack&cameraTrack,const CaptureSettings&settings){if(!frameProvider||settings.width==0||settings.height==0||settings.fps<=0.0||settings.frameCount==0)throw std::invalid_argument("invalid capture request");std::filesystem::create_directories(settings.outputDirectory);CaptureResult result;result.framePaths.reserve(settings.frameCount);const double aspect=static_cast<double>(settings.width)/settings.height;for(std::size_t frame=0;frame<settings.frameCount;++frame){const double time=static_cast<double>(frame)/settings.fps;const auto world=frameProvider(time);const auto projected=projectParticles(world,cameraTrack.sample(time),aspect);const RenderSettings renderSettings{settings.width,settings.height,1.0,settings.clearColor};const auto image=renderParticlesHeadless(backend,projected,renderSettings);std::ostringstream name;name<<"frame_"<<std::setw(6)<<std::setfill('0')<<frame<<".ppm";const auto path=(std::filesystem::path(settings.outputDirectory)/name.str()).string();writePpm(image,path);result.framePaths.push_back(path);}result.durationSeconds=static_cast<double>(settings.frameCount)/settings.fps;return result;}

} // namespace vulkax::render
