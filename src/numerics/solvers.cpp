#include "vulkax/numerics/solvers.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::numerics {
namespace {
double dot(const Vector&a,const Vector&b){double s=0;for(std::size_t i=0;i<a.size();++i)s+=a[i]*b[i];return s;}
double norm(const Vector&a){return std::sqrt(dot(a,a));}
Vector solveDense(std::vector<Vector> A,Vector b){const std::size_t n=b.size();for(std::size_t k=0;k<n;++k){std::size_t pivot=k;for(std::size_t i=k+1;i<n;++i)if(std::abs(A[i][k])>std::abs(A[pivot][k]))pivot=i;if(std::abs(A[pivot][k])<1e-14)throw std::runtime_error("singular Newton Jacobian");std::swap(A[k],A[pivot]);std::swap(b[k],b[pivot]);for(std::size_t i=k+1;i<n;++i){double f=A[i][k]/A[k][k];for(std::size_t j=k;j<n;++j)A[i][j]-=f*A[k][j];b[i]-=f*b[k];}}Vector x(n);for(std::size_t ii=n;ii-->0;){double s=b[ii];for(std::size_t j=ii+1;j<n;++j)s-=A[ii][j]*x[j];x[ii]=s/A[ii][ii];}return x;}
}
IterativeResult conjugateGradient(const LinearOperator&A,std::span<const double>b,Vector x,double tol,std::size_t maxIt){if(x.size()!=b.size())throw std::invalid_argument("CG size mismatch");Vector Ax(b.size()),r(b.size()),p(b.size()),Ap(b.size());A(x,Ax);for(std::size_t i=0;i<b.size();++i)r[i]=b[i]-Ax[i];p=r;double rr=dot(r,r);IterativeResult out;for(std::size_t it=0;it<maxIt;++it){double rn=std::sqrt(rr);if(rn<=tol){out={x,it,rn,true};return out;}A(p,Ap);double den=dot(p,Ap);if(std::abs(den)<1e-30)break;double alpha=rr/den;for(std::size_t i=0;i<x.size();++i){x[i]+=alpha*p[i];r[i]-=alpha*Ap[i];}double next=dot(r,r);double beta=next/rr;for(std::size_t i=0;i<p.size();++i)p[i]=r[i]+beta*p[i];rr=next;}out={x,maxIt,std::sqrt(rr),false};return out;}
Vector rk4Step(const OdeRhs&rhs,double t,std::span<const double>s,double dt){const std::size_t n=s.size();Vector k1(n),k2(n),k3(n),k4(n),tmp(s.begin(),s.end()),out(s.begin(),s.end());rhs(t,s,k1);for(std::size_t i=0;i<n;++i)tmp[i]=s[i]+0.5*dt*k1[i];rhs(t+0.5*dt,tmp,k2);for(std::size_t i=0;i<n;++i)tmp[i]=s[i]+0.5*dt*k2[i];rhs(t+0.5*dt,tmp,k3);for(std::size_t i=0;i<n;++i)tmp[i]=s[i]+dt*k3[i];rhs(t+dt,tmp,k4);for(std::size_t i=0;i<n;++i)out[i]=s[i]+dt*(k1[i]+2*k2[i]+2*k3[i]+k4[i])/6.0;return out;}
IterativeResult newtonSolve(const NonlinearResidual&f,Vector x,double tol,std::size_t maxIt,double h){if(x.empty())throw std::invalid_argument("Newton state empty");Vector r(x.size());for(std::size_t it=0;it<maxIt;++it){f(x,r);double rn=norm(r);if(rn<=tol)return{x,it,rn,true};std::vector<Vector>J(x.size(),Vector(x.size()));for(std::size_t j=0;j<x.size();++j){Vector xp=x,rp(x.size());double step=h*std::max(1.0,std::abs(x[j]));xp[j]+=step;f(xp,rp);for(std::size_t i=0;i<x.size();++i)J[i][j]=(rp[i]-r[i])/step;}Vector rhs(r.size());for(std::size_t i=0;i<r.size();++i)rhs[i]=-r[i];Vector dx=solveDense(std::move(J),std::move(rhs));double alpha=1.0;Vector candidate=x,rc(x.size());while(alpha>1e-4){for(std::size_t i=0;i<x.size();++i)candidate[i]=x[i]+alpha*dx[i];f(candidate,rc);if(norm(rc)<rn)break;alpha*=0.5;}x=candidate;}f(x,r);return{x,maxIt,norm(r),false};}
double cflTimeStep(double dx,double speed,double safety,double ceiling){if(dx<=0||safety<=0||safety>1||ceiling<=0)throw std::invalid_argument("invalid CFL input");if(speed<=1e-15)return ceiling;return std::min(ceiling,safety*dx/speed);}
} // namespace vulkax::numerics
