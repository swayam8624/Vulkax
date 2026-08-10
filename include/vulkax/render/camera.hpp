#pragma once

#include "vulkax/core/math.hpp"
#include "vulkax/visualization/scientific.hpp"

#include <vector>

namespace vulkax::render {

struct Camera {
    math::Vec3 position{0.0,0.0,3.0};
    math::Vec3 target{0.0,0.0,0.0};
    math::Vec3 up{0.0,1.0,0.0};
    double verticalFovDegrees{45.0};
    double exposure{1.0};
};

struct CameraKeyframe {
    double timeSeconds{};
    Camera camera;
};

class CameraTrack {
public:
    void setKeyframes(std::vector<CameraKeyframe> keyframes);
    [[nodiscard]] const std::vector<CameraKeyframe>& keyframes()const noexcept{return keyframes_;}
    [[nodiscard]] Camera sample(double timeSeconds)const;
private:
    std::vector<CameraKeyframe> keyframes_;
};

[[nodiscard]] std::vector<visualization::ParticleInstance> projectParticles(
    const std::vector<visualization::ParticleInstance>& worldParticles,const Camera& camera,
    double aspectRatio,double nearPlane=1.0e-3);

} // namespace vulkax::render
