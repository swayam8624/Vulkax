#include "vulkax/autodiff/discrete_adjoint.hpp"

#include <cassert>
#include <cmath>

int main(){using namespace vulkax::autodiff;const StepFunction oscillator=[](const State&x,const Parameters&p,double dt){const double stiffness=p[0],damping=p[1];State next(2);const double acceleration=-stiffness*x[0]-damping*x[1];next[1]=x[1]+dt*acceleration;next[0]=x[0]+dt*next[1];return next;};const ObjectiveFunction objective=[](const State&x){return 0.5*(x[0]-0.2)*(x[0]-0.2)+0.1*x[1]*x[1];};const State initial{1.0,0.0};const Parameters parameters{3.2,0.18};const auto adjoint=differentiateTrajectory(oscillator,objective,initial,parameters,0.01,120);const auto finite=finiteDifferenceTrajectoryGradient(oscillator,objective,initial,parameters,0.01,120);assert(adjoint.gradient.size()==2);for(std::size_t i=0;i<2;++i){const double scale=std::max({1e-8,std::abs(adjoint.gradient[i]),std::abs(finite[i])});assert(std::abs(adjoint.gradient[i]-finite[i])/scale<2e-5);}assert(adjoint.trajectory.size()==121);assert(std::isfinite(adjoint.objective));return 0;}
