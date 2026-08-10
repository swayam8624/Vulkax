#include "vulkax/physics/fem.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::physics::fem {
namespace {
using field::Vec3;
struct M3 { double a[3][3]{}; };
M3 columns(Vec3 a,Vec3 b,Vec3 c){return {{{a.x,b.x,c.x},{a.y,b.y,c.y},{a.z,b.z,c.z}}};}
M3 mul(const M3&A,const M3&B){M3 C;for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)C.a[i][j]+=A.a[i][k]*B.a[k][j];return C;}
M3 transpose(const M3&A){M3 B;for(int i=0;i<3;++i)for(int j=0;j<3;++j)B.a[i][j]=A.a[j][i];return B;}
double det(const M3&A){return A.a[0][0]*(A.a[1][1]*A.a[2][2]-A.a[1][2]*A.a[2][1])-A.a[0][1]*(A.a[1][0]*A.a[2][2]-A.a[1][2]*A.a[2][0])+A.a[0][2]*(A.a[1][0]*A.a[2][1]-A.a[1][1]*A.a[2][0]);}
M3 inverse(const M3&A){double d=det(A);if(std::abs(d)<1e-14)throw std::runtime_error("singular tetrahedron");M3 B;B.a[0][0]=(A.a[1][1]*A.a[2][2]-A.a[1][2]*A.a[2][1])/d;B.a[0][1]=(A.a[0][2]*A.a[2][1]-A.a[0][1]*A.a[2][2])/d;B.a[0][2]=(A.a[0][1]*A.a[1][2]-A.a[0][2]*A.a[1][1])/d;B.a[1][0]=(A.a[1][2]*A.a[2][0]-A.a[1][0]*A.a[2][2])/d;B.a[1][1]=(A.a[0][0]*A.a[2][2]-A.a[0][2]*A.a[2][0])/d;B.a[1][2]=(A.a[0][2]*A.a[1][0]-A.a[0][0]*A.a[1][2])/d;B.a[2][0]=(A.a[1][0]*A.a[2][1]-A.a[1][1]*A.a[2][0])/d;B.a[2][1]=(A.a[0][1]*A.a[2][0]-A.a[0][0]*A.a[2][1])/d;B.a[2][2]=(A.a[0][0]*A.a[1][1]-A.a[0][1]*A.a[1][0])/d;return B;}
Vec3 col(const M3&A,int j){return {A.a[0][j],A.a[1][j],A.a[2][j]};}
double frob2(const M3&A){double s=0;for(auto&r:A.a)for(double x:r)s+=x*x;return s;}

struct TetEval { std::array<Vec3,4> force{}; double volume{}; double energy{}; bool inverted{}; };
TetEval evaluateTet(const TetMesh&mesh,const std::array<std::uint32_t,4>&t,const NeoHookeanMaterial&m){
    for(auto i:t)if(i>=mesh.positions.size())throw std::out_of_range("tet index");
    const Vec3 X0=mesh.restPositions[t[0]],X1=mesh.restPositions[t[1]],X2=mesh.restPositions[t[2]],X3=mesh.restPositions[t[3]];
    const M3 Dm=columns(X1-X0,X2-X0,X3-X0);const double signedDet=det(Dm);const double V=std::abs(signedDet)/6.0;if(V<1e-14)throw std::runtime_error("degenerate rest tetrahedron");
    const M3 invDm=inverse(Dm);
    const Vec3 x0=mesh.positions[t[0]],x1=mesh.positions[t[1]],x2=mesh.positions[t[2]],x3=mesh.positions[t[3]];
    const M3 F=mul(columns(x1-x0,x2-x0,x3-x0),invDm);const double J=det(F);TetEval out;out.volume=V;out.inverted=J<=1e-10;if(out.inverted)return out;
    const M3 FinvT=transpose(inverse(F));const double logJ=std::log(J);const double lambda=m.bulkModulus-2.0*m.shearModulus/3.0;
    M3 P;for(int i=0;i<3;++i)for(int j=0;j<3;++j)P.a[i][j]=m.shearModulus*(F.a[i][j]-FinvT.a[i][j])+lambda*logJ*FinvT.a[i][j];
    M3 H=mul(P,transpose(invDm));for(auto&r:H.a)for(double&v:r)v*=-V;
    out.force[1]=col(H,0);out.force[2]=col(H,1);out.force[3]=col(H,2);out.force[0]=(out.force[1]+out.force[2]+out.force[3])*-1.0;
    out.energy=V*(0.5*m.shearModulus*(frob2(F)-3.0)-m.shearModulus*logJ+0.5*lambda*logJ*logJ);
    return out;
}
}

Solver::Solver(TetMesh mesh,NeoHookeanMaterial material):mesh_(std::move(mesh)),material_(material){
    const auto n=mesh_.restPositions.size();if(mesh_.positions.empty())mesh_.positions=mesh_.restPositions;if(mesh_.velocities.empty())mesh_.velocities.assign(n,{});if(mesh_.fixed.empty())mesh_.fixed.assign(n,false);
    if(mesh_.positions.size()!=n||mesh_.velocities.size()!=n||mesh_.fixed.size()!=n)throw std::invalid_argument("FEM node array size mismatch");if(material_.density<=0||material_.shearModulus<=0||material_.bulkModulus<=0)throw std::invalid_argument("FEM material parameters must be positive");
    masses_.assign(n,0.0);for(const auto&t:mesh_.tetrahedra){auto e=evaluateTet(mesh_,t,material_);const double mass=material_.density*e.volume/4.0;for(auto i:t)masses_[i]+=mass;}for(double mass:masses_)if(mass<=0)throw std::invalid_argument("FEM node has no positive lumped mass");
}
std::vector<Vec3> Solver::elasticForces() const{std::vector<Vec3>f(mesh_.positions.size());for(const auto&t:mesh_.tetrahedra){auto e=evaluateTet(mesh_,t,material_);if(e.inverted)continue;for(int k=0;k<4;++k)f[t[static_cast<std::size_t>(k)]]=f[t[static_cast<std::size_t>(k)]]+e.force[static_cast<std::size_t>(k)];}return f;}
void Solver::step(double dt,Vec3 gravity){if(dt<=0)throw std::invalid_argument("FEM dt must be positive");statistics_={};auto f=elasticForces();for(const auto&t:mesh_.tetrahedra){auto e=evaluateTet(mesh_,t,material_);statistics_.invertedElement|=e.inverted;statistics_.elasticEnergy+=e.energy;}
    for(std::size_t i=0;i<f.size();++i){f[i]=f[i]+gravity*masses_[i];statistics_.maximumForce=std::max(statistics_.maximumForce,field::length(f[i]));if(mesh_.fixed[i]){mesh_.velocities[i]={};mesh_.positions[i]=mesh_.restPositions[i];continue;}mesh_.velocities[i]=mesh_.velocities[i]+f[i]*(dt/masses_[i]);mesh_.positions[i]=mesh_.positions[i]+mesh_.velocities[i]*dt;statistics_.kineticEnergy+=0.5*masses_[i]*field::dot(mesh_.velocities[i],mesh_.velocities[i]);}}
CalibrationResult calibrateIncompressibleNeoHookean(const std::vector<UniaxialDatum>&data,double noise){CalibrationResult r;if(data.empty()||noise<=0)return r;double num=0,den=0,weightedError=0,totalWeight=0;for(const auto&d:data){if(d.stretch<=0||d.weight<=0)return r;double a=d.stretch-1.0/(d.stretch*d.stretch);num+=d.weight*a*d.nominalStress;den+=d.weight*a*a;totalWeight+=d.weight;}if(den<=1e-20)return r;r.shearModulus=num/den;for(const auto&d:data){double a=d.stretch-1.0/(d.stretch*d.stretch);double e=r.shearModulus*a-d.nominalStress;weightedError+=d.weight*e*e;}r.rmsError=std::sqrt(weightedError/totalWeight);r.information=den/(noise*noise);r.valid=std::isfinite(r.shearModulus)&&r.shearModulus>0;return r;}
} // namespace vulkax::physics::fem
