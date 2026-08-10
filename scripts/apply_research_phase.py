from pathlib import Path
import runpy

# Apply any still-pending prerequisite scripts first, if they remain on main.
for script in ['scripts/apply_problem_runner_migration.py','scripts/apply_verticals_phase.py','scripts/apply_gpu_dem_phase.py','scripts/apply_opengl_phase.py']:
    p=Path(script)
    if p.exists(): runpy.run_path(str(p),run_name='__main__')
for stale in ['scripts/apply_problem_runner_migration.py','scripts/apply_verticals_phase.py','scripts/apply_gpu_dem_phase.py','scripts/apply_opengl_phase.py','.github/workflows/wip-problem-runner-migration.yml','.github/workflows/wip-verticals-phase.yml','.github/workflows/wip-gpu-dem-phase.yml','.github/workflows/wip-opengl-phase.yml','.github/workflows/wip-reconcile-phases.yml']:
    p=Path(stale)
    if p.exists():p.unlink()

Path('include/vulkax/research/trajectory_influence.hpp').write_text(r'''#pragma once
#include "vulkax/autodiff/discrete_adjoint.hpp"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
namespace vulkax::research {
using EvolutionIncrement=std::function<autodiff::State(const autodiff::State&,const autodiff::Parameters&,double)>;
struct EvolutionOperator{std::string id;EvolutionIncrement increment;};
struct TrajectoryInfluenceResult{std::vector<autodiff::State>trajectory;double objective{};std::vector<std::vector<double>>influence;};
[[nodiscard]] TrajectoryInfluenceResult computeTrajectoryOperatorInfluence(const autodiff::State&initial,const autodiff::Parameters&parameters,double dt,std::size_t steps,const std::vector<EvolutionOperator>&operators,const autodiff::ObjectiveFunction&objective,double stateFiniteDifferenceStep=1e-6);
[[nodiscard]] double predictTrajectoryCounterfactual(const TrajectoryInfluenceResult&,const std::vector<std::vector<double>>&deltaScale);
[[nodiscard]] double rerunTrajectoryCounterfactual(const autodiff::State&initial,const autodiff::Parameters&parameters,double dt,std::size_t steps,const std::vector<EvolutionOperator>&operators,const autodiff::ObjectiveFunction&objective,const std::vector<std::vector<double>>&deltaScale);
}
''')
Path('src/research/trajectory_influence.cpp').write_text(r'''#include "vulkax/research/trajectory_influence.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace vulkax::research { namespace {
autodiff::State add(const autodiff::State&x,const std::vector<EvolutionOperator>&ops,const autodiff::Parameters&p,double dt,const std::vector<double>*delta){autodiff::State out=x;for(std::size_t k=0;k<ops.size();++k){auto inc=ops[k].increment(x,p,dt);if(inc.size()!=x.size())throw std::invalid_argument("evolution operator changed state dimension");const double scale=1.0+(delta?(*delta)[k]:0.0);for(std::size_t i=0;i<out.size();++i)out[i]+=scale*inc[i];}return out;}
double dot(const std::vector<double>&a,const std::vector<double>&b){if(a.size()!=b.size())throw std::invalid_argument("dot mismatch");double s=0;for(std::size_t i=0;i<a.size();++i)s+=a[i]*b[i];return s;}
std::vector<double>objectiveGradient(const autodiff::ObjectiveFunction&f,const autodiff::State&x,double hrel){std::vector<double>g(x.size());for(std::size_t i=0;i<x.size();++i){double h=hrel*std::max(1.0,std::abs(x[i]));auto a=x,b=x;a[i]+=h;b[i]-=h;g[i]=(f(a)-f(b))/(2*h);}return g;}
}
TrajectoryInfluenceResult computeTrajectoryOperatorInfluence(const autodiff::State&initial,const autodiff::Parameters&parameters,double dt,std::size_t steps,const std::vector<EvolutionOperator>&ops,const autodiff::ObjectiveFunction&objective,double h){if(initial.empty()||ops.empty()||!objective||dt<=0||steps==0||h<=0)throw std::invalid_argument("invalid trajectory influence problem");for(const auto&o:ops)if(o.id.empty()||!o.increment)throw std::invalid_argument("invalid evolution operator");TrajectoryInfluenceResult r;r.trajectory.reserve(steps+1);r.trajectory.push_back(initial);for(std::size_t n=0;n<steps;++n)r.trajectory.push_back(add(r.trajectory.back(),ops,parameters,dt,nullptr));r.objective=objective(r.trajectory.back());r.influence.assign(steps,std::vector<double>(ops.size()));auto lambda=objectiveGradient(objective,r.trajectory.back(),h);for(std::size_t rev=0;rev<steps;++rev){std::size_t n=steps-1-rev;const auto&x=r.trajectory[n];for(std::size_t k=0;k<ops.size();++k)r.influence[n][k]=dot(lambda,ops[k].increment(x,parameters,dt));std::vector<double>prev(x.size());for(std::size_t col=0;col<x.size();++col){double step=h*std::max(1.0,std::abs(x[col]));auto plus=x,minus=x;plus[col]+=step;minus[col]-=step;auto fp=add(plus,ops,parameters,dt,nullptr),fm=add(minus,ops,parameters,dt,nullptr);double v=0;for(std::size_t row=0;row<x.size();++row)v+=lambda[row]*(fp[row]-fm[row])/(2*step);prev[col]=v;}lambda=std::move(prev);}return r;}
double predictTrajectoryCounterfactual(const TrajectoryInfluenceResult&r,const std::vector<std::vector<double>>&delta){if(delta.size()!=r.influence.size())throw std::invalid_argument("counterfactual time dimension mismatch");double change=0;for(std::size_t n=0;n<delta.size();++n){if(delta[n].size()!=r.influence[n].size())throw std::invalid_argument("counterfactual operator dimension mismatch");for(std::size_t k=0;k<delta[n].size();++k)change+=r.influence[n][k]*delta[n][k];}return r.objective+change;}
double rerunTrajectoryCounterfactual(const autodiff::State&initial,const autodiff::Parameters&parameters,double dt,std::size_t steps,const std::vector<EvolutionOperator>&ops,const autodiff::ObjectiveFunction&objective,const std::vector<std::vector<double>>&delta){if(delta.size()!=steps)throw std::invalid_argument("counterfactual time dimension mismatch");auto x=initial;for(std::size_t n=0;n<steps;++n){if(delta[n].size()!=ops.size())throw std::invalid_argument("counterfactual operator dimension mismatch");x=add(x,ops,parameters,dt,&delta[n]);}return objective(x);}
}
''')
Path('tests/trajectory_influence_tests.cpp').write_text(r'''#include "vulkax/research/trajectory_influence.hpp"
#include <cassert>
#include <cmath>
int main(){using namespace vulkax;std::vector<research::EvolutionOperator>ops;ops.push_back({"kinematic",[](const autodiff::State&x,const autodiff::Parameters&,double dt){return autodiff::State{dt*x[1],0};}});ops.push_back({"spring",[](const autodiff::State&x,const autodiff::Parameters&p,double dt){return autodiff::State{0,-dt*p[0]*x[0]};}});ops.push_back({"damping",[](const autodiff::State&x,const autodiff::Parameters&p,double dt){return autodiff::State{0,-dt*p[1]*x[1]};}});autodiff::ObjectiveFunction J=[](const autodiff::State&x){return 0.5*x[0]*x[0]+0.05*x[1]*x[1];};auto r=research::computeTrajectoryOperatorInfluence({1,0},{3.0,0.2},0.01,100,ops,J);std::vector<std::vector<double>>delta(100,std::vector<double>(3));for(std::size_t n=35;n<50;++n)delta[n][1]=-0.01;const double predicted=research::predictTrajectoryCounterfactual(r,delta);const double actual=research::rerunTrajectoryCounterfactual({1,0},{3.0,0.2},0.01,100,ops,J,delta);assert(std::abs(predicted-actual)/std::max(1e-8,std::abs(actual))<0.01);double spatialTemporalVariation=0;for(std::size_t n=1;n<100;++n)spatialTemporalVariation+=std::abs(r.influence[n][1]-r.influence[n-1][1]);assert(spatialTemporalVariation>1e-5);return 0;}
''')

Path('include/vulkax/planning/tournament.hpp').write_text(r'''#pragma once
#include "vulkax/planning/solver_plan.hpp"
#include <cstddef>
#include <string>
#include <vector>
namespace vulkax::planning {
struct PilotEvidence{std::string id;SolverPlan plan;double observedRelativeError{};double wallSeconds{};std::uint64_t memoryBytes{};bool stable{true};};
struct TournamentDecision{std::size_t selected{};std::vector<std::string>reasons;};
[[nodiscard]] TournamentDecision selectEmpiricalPlan(const std::vector<PilotEvidence>&,double requiredRelativeError,const ComputeBudget&);
}
''')
Path('src/planning/tournament.cpp').write_text(r'''#include "vulkax/planning/tournament.hpp"
#include <limits>
#include <stdexcept>
namespace vulkax::planning {
TournamentDecision selectEmpiricalPlan(const std::vector<PilotEvidence>&e,double tol,const ComputeBudget&budget){if(e.empty()||tol<=0)throw std::invalid_argument("invalid solver tournament");std::size_t best=e.size();double bestTime=std::numeric_limits<double>::infinity();for(std::size_t i=0;i<e.size();++i){const auto&p=e[i];if(!p.stable||p.observedRelativeError>tol)continue;if(budget.wallSeconds&&p.wallSeconds>*budget.wallSeconds)continue;if(budget.gpuMemoryBytes&&p.memoryBytes>*budget.gpuMemoryBytes)continue;if(p.wallSeconds<bestTime){best=i;bestTime=p.wallSeconds;}}TournamentDecision d;if(best==e.size()){double bestError=std::numeric_limits<double>::infinity();for(std::size_t i=0;i<e.size();++i)if(e[i].stable&&e[i].observedRelativeError<bestError){best=i;bestError=e[i].observedRelativeError;}if(best==e.size())throw std::runtime_error("all solver tournament candidates were unstable");d.reasons.push_back("No pilot met every requested accuracy/budget constraint; selected the lowest-error stable fallback.");}else d.reasons.push_back("Selected the fastest empirically stable pilot satisfying requested error and resource budgets.");d.selected=best;d.reasons.push_back("Observed relative error="+std::to_string(e[best].observedRelativeError)+", pilot wall time="+std::to_string(e[best].wallSeconds)+" s.");return d;}
}
''')
Path('tests/tournament_tests.cpp').write_text(r'''#include "vulkax/planning/tournament.hpp"
#include <cassert>
int main(){using namespace vulkax::planning;SolverPlan p;std::vector<PilotEvidence>e={{"fast",p,0.08,0.4,100,true},{"good",p,0.015,1.2,200,true},{"best",p,0.004,5.0,400,true},{"bad",p,0.001,0.1,100,false}};ComputeBudget b;b.wallSeconds=3.0;b.gpuMemoryBytes=300;auto d=selectEmpiricalPlan(e,0.02,b);assert(d.selected==1);auto fallback=selectEmpiricalPlan(e,0.002,b);assert(fallback.selected==3||fallback.selected==2);return 0;}
''')

Path('include/vulkax/experiment/sequential_design.hpp').write_text(r'''#pragma once
#include <functional>
#include <string>
#include <vector>
namespace vulkax::experiment {
struct Hypothesis{std::string id;std::function<double(double design)>predict;double weight{1.0};};
struct DesignChoice{double design{};double expectedSeparation{};};
[[nodiscard]] DesignChoice chooseDiscriminatingDesign(const std::vector<Hypothesis>&,const std::vector<double>&candidateDesigns,double noiseStd);
void updateHypothesisWeights(std::vector<Hypothesis>&,double design,double observation,double noiseStd);
}
''')
Path('src/experiment/sequential_design.cpp').write_text(r'''#include "vulkax/experiment/sequential_design.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
namespace vulkax::experiment {
DesignChoice chooseDiscriminatingDesign(const std::vector<Hypothesis>&h,const std::vector<double>&d,double noise){if(h.size()<2||d.empty()||noise<=0)throw std::invalid_argument("invalid sequential experiment design");double weightSum=0;for(const auto&m:h){if(!m.predict||m.weight<0)throw std::invalid_argument("invalid hypothesis");weightSum+=m.weight;}if(weightSum<=0)throw std::invalid_argument("hypothesis weights sum to zero");DesignChoice best{d.front(),-1};for(double x:d){double mean=0;for(const auto&m:h)mean+=(m.weight/weightSum)*m.predict(x);double variance=0;for(const auto&m:h){double e=m.predict(x)-mean;variance+=(m.weight/weightSum)*e*e;}double score=variance/(noise*noise);if(score>best.expectedSeparation)best={x,score};}return best;}
void updateHypothesisWeights(std::vector<Hypothesis>&h,double x,double y,double noise){if(noise<=0)throw std::invalid_argument("noise must be positive");double maxLog=-1e300;std::vector<double>logw(h.size());for(std::size_t i=0;i<h.size();++i){if(!h[i].predict||h[i].weight<=0){logw[i]=-1e300;continue;}double e=(y-h[i].predict(x))/noise;logw[i]=std::log(h[i].weight)-0.5*e*e;maxLog=std::max(maxLog,logw[i]);}double sum=0;for(std::size_t i=0;i<h.size();++i){h[i].weight=std::exp(logw[i]-maxLog);sum+=h[i].weight;}if(sum<=0)throw std::runtime_error("hypothesis update underflowed");for(auto&m:h)m.weight/=sum;}
}
''')
Path('tests/sequential_design_tests.cpp').write_text(r'''#include "vulkax/experiment/sequential_design.hpp"
#include <algorithm>
#include <cassert>
#include <vector>
int main(){using namespace vulkax::experiment;std::vector<Hypothesis>h={{"linear",[](double x){return 1.2*x;},1},{"quadratic",[](double x){return x*x;},1},{"saturating",[](double x){return 2*x/(1+x);},1}};const std::vector<double>d={0.3,0.6,1.0,1.5,2.0};for(int round=0;round<4;++round){auto choice=chooseDiscriminatingDesign(h,d,0.05);double observation=choice.design*choice.design;updateHypothesisWeights(h,choice.design,observation,0.05);}auto best=std::max_element(h.begin(),h.end(),[](const auto&a,const auto&b){return a.weight<b.weight;});assert(best->id=="quadratic");assert(best->weight>0.95);return 0;}
''')

p=Path('CMakeLists.txt');t=p.read_text();
for old in ['project(Vulkax VERSION 0.21.0 LANGUAGES CXX)','project(Vulkax VERSION 0.20.0 LANGUAGES CXX)','project(Vulkax VERSION 0.19.0 LANGUAGES CXX)','project(Vulkax VERSION 0.18.0 LANGUAGES CXX)']:t=t.replace(old,'project(Vulkax VERSION 0.22.0 LANGUAGES CXX)')
if 'src/research/trajectory_influence.cpp' not in t:t=t.replace('src/research/operator_influence.cpp src/research/local_operator_influence.cpp','src/research/operator_influence.cpp src/research/local_operator_influence.cpp src/research/trajectory_influence.cpp')
if 'src/planning/tournament.cpp' not in t:t=t.replace('src/planning/solver_plan.cpp','src/planning/solver_plan.cpp src/planning/tournament.cpp')
if 'src/experiment/sequential_design.cpp' not in t:t=t.replace('src/experiment/design.cpp','src/experiment/design.cpp src/experiment/sequential_design.cpp')
for test in ['trajectory_influence','tournament','sequential_design']:
    if test+')' not in t:t=t.replace('opengl_probe)',f'opengl_probe {test})') if 'opengl_probe)' in t else t.replace('gpu_dem)',f'gpu_dem {test})') if 'gpu_dem)' in t else t.replace('problem_runner)',f'problem_runner {test})')
p.write_text(t)
