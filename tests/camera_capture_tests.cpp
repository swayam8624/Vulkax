#include "vulkax/render/camera.hpp"
#include "vulkax/render/capture.hpp"
#include "vulkax/render/headless.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>

int main(){using namespace vulkax;render::CameraTrack track;track.setKeyframes({{0.0,{{0,0,3},{0,0,0},{0,1,0},45,1}},{1.0,{{2,1,3},{0,0,0},{0,1,0},55,1.2}}});const auto mid=track.sample(0.5);assert(mid.position.x>0.9&&mid.position.x<1.1);const std::vector<visualization::ParticleInstance> world={{{0,0,0},0.2,{1,0.3F,0.1F,1}},{{0.5,0.2,0},0.12,{0.1F,0.8F,1,1}}};const auto projected=render::projectParticles(world,track.sample(0.0),16.0/9.0);assert(projected.size()==2);assert(std::abs(projected[0].position.x)<1e-9);for(const auto backend:render::availableHeadlessRenderBackends()){const auto dir=(std::filesystem::temp_directory_path()/std::string("vulkax_capture_")+std::string(backend::toString(backend))).string();const auto capture=render::captureParticleSequence(backend,[&](double time){auto frame=world;frame[1].position.y+=0.1*time;return frame;},track,{96,64,24.0,2,dir,{0,0,0,1}});assert(capture.framePaths.size()==2);for(const auto&path:capture.framePaths)assert(std::filesystem::file_size(path)>100);std::filesystem::remove_all(dir);}return 0;}
