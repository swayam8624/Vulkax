#include "vulkax/planner/solver_plan.hpp"

#include <algorithm>
#include <cctype>

namespace vulkax::planner {
namespace { std::string lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return s;} }
SolverPlan planSolver(const problem::ProblemIR&p){SolverPlan out;bool particles=false,volume=false,rigid=false,rays=false;for(const auto&d:p.domains){particles|=d.kind==problem::DomainKind::ParticleSet;volume|=d.kind==problem::DomainKind::Volume;rigid|=d.kind==problem::DomainKind::RigidAssembly;rays|=d.kind==problem::DomainKind::RayBundle;}bool incompressible=false,solid=false,transport=false;for(const auto&o:p.operators){auto f=lower(o.family+" "+o.label+" "+o.expression);incompressible|=f.find("incompress")!=std::string::npos||f.find("pressure")!=std::string::npos;solid|=f.find("elastic")!=std::string::npos||f.find("solid")!=std::string::npos||f.find("hyperelastic")!=std::string::npos;transport|=f.find("transport")!=std::string::npos||f.find("diffusion")!=std::string::npos;}
    if(particles){out.family=SolverFamily::DEM;out.method="discrete element contact dynamics";out.reasons={"particle-set domain"};out.verificationEvidence={"momentum balance","contact overlap bound","timestep convergence"};out.executableWithCurrentReferenceSolver=true;return out;}
    if(rays){out.family=SolverFamily::RayIntegration;out.method="adaptive trajectory integration";out.reasons={"ray-bundle domain"};out.verificationEvidence={"invariant drift","step convergence"};return out;}
    if(volume&&solid){out.family=SolverFamily::FEM;out.method="tetrahedral nonlinear finite elements";out.reasons={"volume domain with elastic constitutive operator"};out.verificationEvidence={"force equilibrium","mesh convergence","energy consistency"};out.executableWithCurrentReferenceSolver=true;return out;}
    if(volume&&incompressible){out.family=SolverFamily::IncompressibleCFD;out.method="staggered MAC projection";out.reasons={"volume domain with pressure/incompressibility constraint"};out.verificationEvidence={"mass conservation","grid convergence","CFL bound"};out.executableWithCurrentReferenceSolver=true;return out;}
    if(rigid){out.family=SolverFamily::RigidDynamics;out.method="constraint-based rigid dynamics";out.reasons={"rigid assembly domain"};out.verificationEvidence={"momentum balance","constraint error"};return out;}
    if(transport||volume){out.family=SolverFamily::FieldEvolution;out.method="operator-split field evolution";out.reasons={"field transport/diffusion operators"};out.verificationEvidence={"residual norm","grid convergence","stability bound"};out.executableWithCurrentReferenceSolver=true;return out;}
    out.reasons={"problem does not yet expose enough structure for an automatic solver choice"};return out;}
} // namespace vulkax::planner
