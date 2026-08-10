#include "vulkax/autodiff/discrete_adjoint.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::autodiff {
namespace {

State run(const StepFunction&step,State state,const Parameters&parameters,double dt,std::size_t steps){for(std::size_t i=0;i<steps;++i)state=step(state,parameters,dt);return state;}
std::vector<double> objectiveGradient(const ObjectiveFunction&objective,const State&state,double relativeStep){std::vector<double>g(state.size());for(std::size_t i=0;i<state.size();++i){const double h=relativeStep*std::max(1.0,std::abs(state[i]));auto plus=state,minus=state;plus[i]+=h;minus[i]-=h;g[i]=(objective(plus)-objective(minus))/(2*h);}return g;}
}

AdjointResult differentiateTrajectory(const StepFunction&step,const ObjectiveFunction&objective,State initialState,const Parameters&parameters,double dt,std::size_t steps,const AdjointOptions&options){if(!step||!objective||initialState.empty()||parameters.empty()||dt<=0||steps==0||options.stateFiniteDifferenceStep<=0||options.parameterFiniteDifferenceStep<=0)throw std::invalid_argument("invalid discrete adjoint request");AdjointResult result;result.trajectory.reserve(steps+1);result.trajectory.push_back(std::move(initialState));for(std::size_t k=0;k<steps;++k){State next=step(result.trajectory.back(),parameters,dt);if(next.size()!=result.trajectory.front().size())throw std::invalid_argument("step changed state dimension");result.trajectory.push_back(std::move(next));}result.objective=objective(result.trajectory.back());std::vector<double>lambda=objectiveGradient(objective,result.trajectory.back(),options.stateFiniteDifferenceStep);result.gradient.assign(parameters.size(),0.0);
    for(std::size_t reverse=0;reverse<steps;++reverse){const std::size_t k=steps-1-reverse;const State&x=result.trajectory[k];const State&baseline=result.trajectory[k+1];std::vector<double>previousLambda(x.size(),0.0);
        for(std::size_t i=0;i<x.size();++i){const double h=options.stateFiniteDifferenceStep*std::max(1.0,std::abs(x[i]));auto plus=x,minus=x;plus[i]+=h;minus[i]-=h;const State fp=step(plus,parameters,dt),fm=step(minus,parameters,dt);if(fp.size()!=x.size()||fm.size()!=x.size())throw std::invalid_argument("step dimension changed during state Jacobian");double value=0;for(std::size_t row=0;row<x.size();++row)value+=lambda[row]*(fp[row]-fm[row])/(2*h);previousLambda[i]=value;}
        for(std::size_t p=0;p<parameters.size();++p){const double h=options.parameterFiniteDifferenceStep*std::max(1.0,std::abs(parameters[p]));auto plus=parameters,minus=parameters;plus[p]+=h;minus[p]-=h;const State fp=step(x,plus,dt),fm=step(x,minus,dt);double contribution=0;for(std::size_t row=0;row<baseline.size();++row)contribution+=lambda[row]*(fp[row]-fm[row])/(2*h);result.gradient[p]+=contribution;}
        lambda=std::move(previousLambda);
    }
    return result;
}

Parameters finiteDifferenceTrajectoryGradient(const StepFunction&step,const ObjectiveFunction&objective,const State&initialState,const Parameters&parameters,double dt,std::size_t steps,double relativeStep){if(relativeStep<=0)throw std::invalid_argument("invalid finite-difference trajectory step");Parameters gradient(parameters.size());for(std::size_t p=0;p<parameters.size();++p){const double h=relativeStep*std::max(1.0,std::abs(parameters[p]));auto plus=parameters,minus=parameters;plus[p]+=h;minus[p]-=h;gradient[p]=(objective(run(step,initialState,plus,dt,steps))-objective(run(step,initialState,minus,dt,steps)))/(2*h);}return gradient;}

} // namespace vulkax::autodiff
