#include "vulkax/field/field.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::field {
Vec3 operator+(Vec3 a,Vec3 b) noexcept{return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 operator-(Vec3 a,Vec3 b) noexcept{return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 operator*(Vec3 a,double s) noexcept{return {a.x*s,a.y*s,a.z*s};}
Vec3 operator/(Vec3 a,double s){if(s==0.0)throw std::invalid_argument("division by zero");return a*(1.0/s);}
double dot(Vec3 a,Vec3 b) noexcept{return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 cross(Vec3 a,Vec3 b) noexcept{return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double length(Vec3 a) noexcept{return std::sqrt(dot(a,a));}
Vec3 normalized(Vec3 a) noexcept{double l=length(a);return l>1e-15?a*(1.0/l):Vec3{};}
std::size_t GridShape::cellCount() const noexcept{return static_cast<std::size_t>(nx)*ny*nz;}
std::size_t GridShape::index(std::uint32_t x,std::uint32_t y,std::uint32_t z) const{if(x>=nx||y>=ny||z>=nz)throw std::out_of_range("grid index");return (static_cast<std::size_t>(z)*ny+y)*nx+x;}
namespace {
std::uint32_t lo(std::uint32_t i){return i==0?0:i-1;} std::uint32_t hi(std::uint32_t i,std::uint32_t n){return std::min(i+1,n-1);}
double sample(const ScalarField& f,std::uint32_t x,std::uint32_t y,std::uint32_t z){return f.values[f.grid.index(x,y,z)];}
Vec3 sample(const VectorField& f,std::uint32_t x,std::uint32_t y,std::uint32_t z){return f.values[f.grid.index(x,y,z)];}
}
VectorField gradient(const ScalarField& f){if(f.values.size()!=f.grid.cellCount())throw std::invalid_argument("scalar field size mismatch");VectorField out{f.grid,std::vector<Vec3>(f.grid.cellCount())};for(std::uint32_t z=0;z<f.grid.nz;++z)for(std::uint32_t y=0;y<f.grid.ny;++y)for(std::uint32_t x=0;x<f.grid.nx;++x){auto xm=lo(x),xp=hi(x,f.grid.nx),ym=lo(y),yp=hi(y,f.grid.ny),zm=lo(z),zp=hi(z,f.grid.nz);double dx=(xp==xm?1.0:static_cast<double>(xp-xm)*f.grid.spacing.x);double dy=(yp==ym?1.0:static_cast<double>(yp-ym)*f.grid.spacing.y);double dz=(zp==zm?1.0:static_cast<double>(zp-zm)*f.grid.spacing.z);out.values[f.grid.index(x,y,z)]={ (sample(f,xp,y,z)-sample(f,xm,y,z))/dx,(sample(f,x,yp,z)-sample(f,x,ym,z))/dy,(sample(f,x,y,zp)-sample(f,x,y,zm))/dz};}return out;}
ScalarField divergence(const VectorField& f){if(f.values.size()!=f.grid.cellCount())throw std::invalid_argument("vector field size mismatch");ScalarField out{f.grid,std::vector<double>(f.grid.cellCount())};for(std::uint32_t z=0;z<f.grid.nz;++z)for(std::uint32_t y=0;y<f.grid.ny;++y)for(std::uint32_t x=0;x<f.grid.nx;++x){auto xm=lo(x),xp=hi(x,f.grid.nx),ym=lo(y),yp=hi(y,f.grid.ny),zm=lo(z),zp=hi(z,f.grid.nz);double dx=(xp==xm?1.0:static_cast<double>(xp-xm)*f.grid.spacing.x);double dy=(yp==ym?1.0:static_cast<double>(yp-ym)*f.grid.spacing.y);double dz=(zp==zm?1.0:static_cast<double>(zp-zm)*f.grid.spacing.z);out.values[f.grid.index(x,y,z)]=(sample(f,xp,y,z).x-sample(f,xm,y,z).x)/dx+(sample(f,x,yp,z).y-sample(f,x,ym,z).y)/dy+(sample(f,x,y,zp).z-sample(f,x,y,zm).z)/dz;}return out;}
ScalarField laplacian(const ScalarField& f){if(f.values.size()!=f.grid.cellCount())throw std::invalid_argument("scalar field size mismatch");ScalarField out{f.grid,std::vector<double>(f.grid.cellCount())};for(std::uint32_t z=0;z<f.grid.nz;++z)for(std::uint32_t y=0;y<f.grid.ny;++y)for(std::uint32_t x=0;x<f.grid.nx;++x){double c=sample(f,x,y,z),v=0.0;if(f.grid.nx>1)v+=(sample(f,hi(x,f.grid.nx),y,z)-2*c+sample(f,lo(x),y,z))/(f.grid.spacing.x*f.grid.spacing.x);if(f.grid.ny>1)v+=(sample(f,x,hi(y,f.grid.ny),z)-2*c+sample(f,x,lo(y),z))/(f.grid.spacing.y*f.grid.spacing.y);if(f.grid.nz>1)v+=(sample(f,x,y,hi(z,f.grid.nz))-2*c+sample(f,x,y,lo(z)))/(f.grid.spacing.z*f.grid.spacing.z);out.values[f.grid.index(x,y,z)]=v;}return out;}
} // namespace vulkax::field
