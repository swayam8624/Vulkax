#include "vulkax/geometry/voxelize.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::geometry {
namespace {

bool rayTriangleX(math::Vec3 origin,math::Vec3 a,math::Vec3 b,math::Vec3 c){
    const math::Vec3 dir{1,0,0};const math::Vec3 e1=b-a,e2=c-a;const math::Vec3 h=math::cross(dir,e2);const double det=math::dot(e1,h);if(std::abs(det)<1e-12)return false;const double inv=1.0/det;const math::Vec3 s=origin-a;const double u=inv*math::dot(s,h);if(u<-1e-10||u>1.0+1e-10)return false;const math::Vec3 q=math::cross(s,e1);const double v=inv*math::dot(dir,q);if(v<-1e-10||u+v>1.0+1e-10)return false;const double t=inv*math::dot(e2,q);return t>1e-10;
}

} // namespace

bool VoxelMask::at(std::size_t x,std::size_t y,std::size_t z)const{if(x>=nx||y>=ny||z>=nz)throw std::out_of_range("voxel index out of range");return solid[(z*ny+y)*nx+x]!=0;}
std::size_t VoxelMask::solidCount()const noexcept{return static_cast<std::size_t>(std::count(solid.begin(),solid.end(),static_cast<std::uint8_t>(1)));}

VoxelMask voxelizeClosedMesh(const TriangleMesh& mesh,std::size_t nx,std::size_t ny,std::size_t nz,Bounds3 domainBounds){if(nx==0||ny==0||nz==0)throw std::invalid_argument("voxel dimensions must be positive");const auto topology=analyzeTopology(mesh);if(!topology.watertight)throw std::invalid_argument("physics voxelization requires a watertight manifold mesh");const math::Vec3 extent=domainBounds.maximum-domainBounds.minimum;if(extent.x<=0||extent.y<=0||extent.z<=0)throw std::invalid_argument("invalid voxel domain bounds");VoxelMask result{nx,ny,nz,domainBounds,std::vector<std::uint8_t>(nx*ny*nz,0)};for(std::size_t z=0;z<nz;++z)for(std::size_t y=0;y<ny;++y)for(std::size_t x=0;x<nx;++x){math::Vec3 p{domainBounds.minimum.x+(static_cast<double>(x)+0.5)*extent.x/static_cast<double>(nx),domainBounds.minimum.y+(static_cast<double>(y)+0.5)*extent.y/static_cast<double>(ny),domainBounds.minimum.z+(static_cast<double>(z)+0.5)*extent.z/static_cast<double>(nz)};std::size_t hits=0;for(const auto&t:mesh.triangles)if(rayTriangleX(p,mesh.positions[t[0]],mesh.positions[t[1]],mesh.positions[t[2]]))++hits;if(hits%2==1)result.solid[(z*ny+y)*nx+x]=1;}return result;}

} // namespace vulkax::geometry
