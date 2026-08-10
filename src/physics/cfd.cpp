#include "vulkax/physics/cfd.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::physics::cfd {
MacGrid2D::MacGrid2D(std::uint32_t w,std::uint32_t h,double sx,double sy):nx(w),ny(h),dx(sx),dy(sy),u(static_cast<std::size_t>(w+1)*h),v(static_cast<std::size_t>(w)*(h+1)),pressure(static_cast<std::size_t>(w)*h),scalar(static_cast<std::size_t>(w)*h),solid(static_cast<std::size_t>(w)*h){if(w<2||h<2||sx<=0||sy<=0)throw std::invalid_argument("MAC grid requires at least 2x2 positive cells");}
std::size_t MacGrid2D::cell(std::uint32_t x,std::uint32_t y)const{if(x>=nx||y>=ny)throw std::out_of_range("cell");return static_cast<std::size_t>(y)*nx+x;}
std::size_t MacGrid2D::uFace(std::uint32_t x,std::uint32_t y)const{if(x>nx||y>=ny)throw std::out_of_range("u face");return static_cast<std::size_t>(y)*(nx+1)+x;}
std::size_t MacGrid2D::vFace(std::uint32_t x,std::uint32_t y)const{if(x>=nx||y>ny)throw std::out_of_range("v face");return static_cast<std::size_t>(y)*nx+x;}
namespace {
double divAt(const MacGrid2D&g,std::uint32_t x,std::uint32_t y){if(g.solid[g.cell(x,y)])return 0.0;return (g.u[g.uFace(x+1,y)]-g.u[g.uFace(x,y)])/g.dx+(g.v[g.vFace(x,y+1)]-g.v[g.vFace(x,y)])/g.dy;}
double cellVelocityU(const MacGrid2D&g,std::uint32_t x,std::uint32_t y){return 0.5*(g.u[g.uFace(x,y)]+g.u[g.uFace(x+1,y)]);}double cellVelocityV(const MacGrid2D&g,std::uint32_t x,std::uint32_t y){return 0.5*(g.v[g.vFace(x,y)]+g.v[g.vFace(x,y+1)]);}
double bilerpScalar(const MacGrid2D&g,double gx,double gy,const std::vector<double>&s){gx=std::clamp(gx,0.0,static_cast<double>(g.nx-1));gy=std::clamp(gy,0.0,static_cast<double>(g.ny-1));auto x0=static_cast<std::uint32_t>(std::floor(gx)),y0=static_cast<std::uint32_t>(std::floor(gy));auto x1=std::min(x0+1,g.nx-1),y1=std::min(y0+1,g.ny-1);double tx=gx-x0,ty=gy-y0;double a=s[g.cell(x0,y0)]*(1-tx)+s[g.cell(x1,y0)]*tx;double b=s[g.cell(x0,y1)]*(1-tx)+s[g.cell(x1,y1)]*tx;return a*(1-ty)+b*ty;}
}
double divergenceL2(const MacGrid2D&g){double sum=0;std::size_t count=0;for(std::uint32_t y=0;y<g.ny;++y)for(std::uint32_t x=0;x<g.nx;++x)if(!g.solid[g.cell(x,y)]){double d=divAt(g,x,y);sum+=d*d;++count;}return count?std::sqrt(sum/static_cast<double>(count)):0.0;}
ProjectionStats projectIncompressible(MacGrid2D&g,double dt,std::size_t iterations){if(dt<=0||iterations==0)throw std::invalid_argument("invalid projection settings");ProjectionStats stats;stats.divergenceBefore=divergenceL2(g);std::vector<double>rhs(g.pressure.size()),next(g.pressure.size());for(std::uint32_t y=0;y<g.ny;++y)for(std::uint32_t x=0;x<g.nx;++x)rhs[g.cell(x,y)]=divAt(g,x,y)/dt;std::fill(g.pressure.begin(),g.pressure.end(),0.0);const double idx2=1.0/(g.dx*g.dx),idy2=1.0/(g.dy*g.dy);
    for(std::size_t it=0;it<iterations;++it){for(std::uint32_t y=0;y<g.ny;++y)for(std::uint32_t x=0;x<g.nx;++x){auto c=g.cell(x,y);if(g.solid[c]){next[c]=0;continue;}double sum=0,coef=0;if(x>0&&!g.solid[g.cell(x-1,y)]){sum+=g.pressure[g.cell(x-1,y)]*idx2;coef+=idx2;}if(x+1<g.nx&&!g.solid[g.cell(x+1,y)]){sum+=g.pressure[g.cell(x+1,y)]*idx2;coef+=idx2;}if(y>0&&!g.solid[g.cell(x,y-1)]){sum+=g.pressure[g.cell(x,y-1)]*idy2;coef+=idy2;}if(y+1<g.ny&&!g.solid[g.cell(x,y+1)]){sum+=g.pressure[g.cell(x,y+1)]*idy2;coef+=idy2;}next[c]=coef>0?(sum-rhs[c])/coef:0.0;}g.pressure.swap(next);}stats.pressureIterations=iterations;
    for(std::uint32_t y=0;y<g.ny;++y){g.u[g.uFace(0,y)]=0;g.u[g.uFace(g.nx,y)]=0;for(std::uint32_t x=1;x<g.nx;++x){auto l=g.cell(x-1,y),r=g.cell(x,y);if(g.solid[l]||g.solid[r])g.u[g.uFace(x,y)]=0;else g.u[g.uFace(x,y)]-=dt*(g.pressure[r]-g.pressure[l])/g.dx;}}
    for(std::uint32_t x=0;x<g.nx;++x){g.v[g.vFace(x,0)]=0;g.v[g.vFace(x,g.ny)]=0;for(std::uint32_t y=1;y<g.ny;++y){auto b=g.cell(x,y-1),t=g.cell(x,y);if(g.solid[b]||g.solid[t])g.v[g.vFace(x,y)]=0;else g.v[g.vFace(x,y)]-=dt*(g.pressure[t]-g.pressure[b])/g.dy;}}
    stats.divergenceAfter=divergenceL2(g);return stats;}
void advectScalarSemiLagrangian(MacGrid2D&g,double dt){if(dt<0)throw std::invalid_argument("negative advection dt");auto old=g.scalar;for(std::uint32_t y=0;y<g.ny;++y)for(std::uint32_t x=0;x<g.nx;++x){auto c=g.cell(x,y);if(g.solid[c]){g.scalar[c]=0;continue;}double px=static_cast<double>(x)-dt*cellVelocityU(g,x,y)/g.dx;double py=static_cast<double>(y)-dt*cellVelocityV(g,x,y)/g.dy;g.scalar[c]=bilerpScalar(g,px,py,old);}}
} // namespace vulkax::physics::cfd
