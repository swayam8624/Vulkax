#include "vulkax/render/camera.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace vulkax::render {
namespace {

double smooth(double t){t=std::clamp(t,0.0,1.0);return t*t*(3.0-2.0*t);}
math::Vec3 lerp(math::Vec3 a,math::Vec3 b,double t){return a+(b-a)*t;}
double lerp(double a,double b,double t){return a+(b-a)*t;}
Camera interpolate(const Camera&a,const Camera&b,double t){t=smooth(t);Camera c;c.position=lerp(a.position,b.position,t);c.target=lerp(a.target,b.target,t);c.up=math::normalized(lerp(a.up,b.up,t));c.verticalFovDegrees=lerp(a.verticalFovDegrees,b.verticalFovDegrees,t);c.exposure=lerp(a.exposure,b.exposure,t);return c;}

} // namespace

void CameraTrack::setKeyframes(std::vector<CameraKeyframe> keyframes){if(keyframes.empty()){keyframes_.clear();return;}std::sort(keyframes.begin(),keyframes.end(),[](const auto&a,const auto&b){return a.timeSeconds<b.timeSeconds;});for(std::size_t i=1;i<keyframes.size();++i)if(keyframes[i].timeSeconds<=keyframes[i-1].timeSeconds)throw std::invalid_argument("camera keyframe times must be unique");keyframes_=std::move(keyframes);}

Camera CameraTrack::sample(double timeSeconds)const{if(keyframes_.empty())return {};if(timeSeconds<=keyframes_.front().timeSeconds)return keyframes_.front().camera;if(timeSeconds>=keyframes_.back().timeSeconds)return keyframes_.back().camera;const auto upper=std::upper_bound(keyframes_.begin(),keyframes_.end(),timeSeconds,[](double time,const CameraKeyframe&key){return time<key.timeSeconds;});const auto&b=*upper;const auto&a=*(upper-1);const double t=(timeSeconds-a.timeSeconds)/(b.timeSeconds-a.timeSeconds);return interpolate(a.camera,b.camera,t);}

std::vector<visualization::ParticleInstance> projectParticles(const std::vector<visualization::ParticleInstance>& worldParticles,const Camera&camera,double aspectRatio,double nearPlane){if(aspectRatio<=0.0||nearPlane<=0.0||camera.verticalFovDegrees<=1.0||camera.verticalFovDegrees>=179.0)throw std::invalid_argument("invalid perspective camera");const math::Vec3 forward=math::normalized(camera.target-camera.position);if(math::length(forward)<1e-12)throw std::invalid_argument("camera position and target coincide");const math::Vec3 right=math::normalized(math::cross(forward,camera.up));if(math::length(right)<1e-12)throw std::invalid_argument("camera up is parallel to view direction");const math::Vec3 up=math::cross(right,forward);const double tanHalf=std::tan(camera.verticalFovDegrees*std::numbers::pi/360.0);std::vector<visualization::ParticleInstance> projected;projected.reserve(worldParticles.size());for(const auto&p:worldParticles){const math::Vec3 delta=p.position-camera.position;const double z=math::dot(delta,forward);if(z<=nearPlane)continue;const double x=math::dot(delta,right)/(z*tanHalf*aspectRatio);const double y=math::dot(delta,up)/(z*tanHalf);const double radius=p.radius/(z*tanHalf);if(std::abs(x)>1.0+radius||std::abs(y)>1.0+radius)continue;auto copy=p;copy.position={x,y,std::clamp(z/1000.0,-1.0,1.0)};copy.radius=radius;projected.push_back(copy);}return projected;}

} // namespace vulkax::render
