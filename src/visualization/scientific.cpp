#include "vulkax/visualization/scientific.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace vulkax::visualization {
namespace {
using field::Vec3;
Vec3 position(const field::GridShape&g,std::uint32_t x,std::uint32_t y,std::uint32_t z){return {g.origin.x+g.spacing.x*x,g.origin.y+g.spacing.y*y,g.origin.z+g.spacing.z*z};}
Vec3 interpolate(Vec3 a,Vec3 b,double va,double vb,double iso){double den=vb-va;double t=std::abs(den)>1e-15?(iso-va)/den:0.5;t=std::clamp(t,0.0,1.0);return a+(b-a)*t;}
void emitTriangle(field::TriangleMesh&mesh,Vec3 a,Vec3 b,Vec3 c){auto base=static_cast<std::uint32_t>(mesh.vertices.size());mesh.vertices.push_back(a);mesh.vertices.push_back(b);mesh.vertices.push_back(c);mesh.triangles.push_back({base,base+1,base+2});}
void polygoniseTet(field::TriangleMesh&mesh,const std::array<Vec3,4>&p,const std::array<double,4>&v,double iso){static constexpr std::array<std::array<int,2>,6>edges{{{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}}};std::vector<Vec3>hits;for(auto e:edges){bool a=v[static_cast<std::size_t>(e[0])]>=iso,b=v[static_cast<std::size_t>(e[1])]>=iso;if(a==b)continue;hits.push_back(interpolate(p[static_cast<std::size_t>(e[0])],p[static_cast<std::size_t>(e[1])],v[static_cast<std::size_t>(e[0])],v[static_cast<std::size_t>(e[1])],iso));}if(hits.size()==3)emitTriangle(mesh,hits[0],hits[1],hits[2]);else if(hits.size()==4){emitTriangle(mesh,hits[0],hits[1],hits[2]);emitTriangle(mesh,hits[0],hits[2],hits[3]);}}
}
IsoSurfaceResult extractIsoSurface(const field::ScalarField&f,double iso){if(f.values.size()!=f.grid.cellCount())throw std::invalid_argument("iso field size mismatch");IsoSurfaceResult out;if(f.grid.nx<2||f.grid.ny<2||f.grid.nz<2)return out;static constexpr std::array<std::array<int,4>,6>tets{{{0,5,1,6},{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6}}};static constexpr std::array<std::array<int,3>,8>corner{{{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}}};
    for(std::uint32_t z=0;z+1<f.grid.nz;++z)for(std::uint32_t y=0;y+1<f.grid.ny;++y)for(std::uint32_t x=0;x+1<f.grid.nx;++x){std::array<Vec3,8>p{};std::array<double,8>v{};bool below=false,above=false;for(std::size_t c=0;c<8;++c){auto xx=x+static_cast<std::uint32_t>(corner[c][0]),yy=y+static_cast<std::uint32_t>(corner[c][1]),zz=z+static_cast<std::uint32_t>(corner[c][2]);p[c]=position(f.grid,xx,yy,zz);v[c]=f.values[f.grid.index(xx,yy,zz)];below|=v[c]<iso;above|=v[c]>=iso;}if(!(below&&above))continue;++out.activeCells;for(auto t:tets){std::array<Vec3,4>tp{p[static_cast<std::size_t>(t[0])],p[static_cast<std::size_t>(t[1])],p[static_cast<std::size_t>(t[2])],p[static_cast<std::size_t>(t[3])]};std::array<double,4>tv{v[static_cast<std::size_t>(t[0])],v[static_cast<std::size_t>(t[1])],v[static_cast<std::size_t>(t[2])],v[static_cast<std::size_t>(t[3])]};polygoniseTet(out.mesh,tp,tv,iso);}}return out;}
} // namespace vulkax::visualization
