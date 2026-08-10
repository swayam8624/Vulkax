#include "vulkax/geometry/mesh.hpp"
#include "vulkax/geometry/voxelize.hpp"
#include "vulkax/solvers/mac3d.hpp"

#include <cassert>
#include <cmath>
#include <string>

int main(){using namespace vulkax;solvers::MacGrid3D grid(20,16,12,0.05,0.05,0.05);for(std::size_t z=0;z<grid.nz();++z)for(std::size_t y=0;y<grid.ny();++y)for(std::size_t x=0;x<=grid.nx();++x)grid.u(x,y,z)=0.2*std::sin(0.3*static_cast<double>(x))+0.01*y;for(std::size_t z=0;z<grid.nz();++z)for(std::size_t y=0;y<=grid.ny();++y)for(std::size_t x=0;x<grid.nx();++x)grid.v(x,y,z)=0.15*std::cos(0.4*static_cast<double>(y))-0.01*x;for(std::size_t z=0;z<=grid.nz();++z)for(std::size_t y=0;y<grid.ny();++y)for(std::size_t x=0;x<grid.nx();++x)grid.w(x,y,z)=0.12*std::sin(0.5*static_cast<double>(z));for(std::size_t z=4;z<8;++z)for(std::size_t y=6;y<10;++y)for(std::size_t x=8;x<12;++x)grid.solid(x,y,z)=1;const auto d=solvers::projectMacGrid(grid,{0.01,1.225,300});assert(d.divergenceL2Before>1e-3);assert(d.divergenceL2After<d.divergenceL2Before*0.35);assert(std::isfinite(d.maxSpeed));for(std::size_t z=4;z<8;++z)for(std::size_t y=6;y<10;++y){assert(grid.u(8,y,z)==0);assert(grid.u(12,y,z)==0);}return 0;}
