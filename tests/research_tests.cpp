#include "vulkax/planner/solver_plan.hpp"
#include "vulkax/research/operator_influence.hpp"
#include "vulkax/visualization/scientific.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace { int failures=0; void check(bool c,const std::string&m){if(!c){++failures;std::cerr<<"FAIL: "<<m<<'\n';}} }

int main(){
    using namespace vulkax;
    {
        const std::vector<double> adjoint{1,1,1,1};
        const std::vector<double> residual{2,-1,0.5,3};
        const std::vector<double> intervention{0.1,0,0,0};
        auto influence=research::computeOperatorInfluence("forcing",adjoint,residual);
        double predicted=research::predictObjectiveDelta(influence,intervention);
        const double measured=-0.2; // exact for R(x,a)=x+a*r, J=sum(x), perturb first a by 0.1
        check(std::abs(predicted-measured)<1e-12,"Operator Influence Field predicts exact linear counterfactual");
        check(research::relativeCounterfactualError(predicted,measured)<1e-12,"counterfactual error metric");
    }
    {
        problem::ProblemIR p; p.id="mill";p.name="granular mill";p.domains.push_back({"particles",problem::DomainKind::ParticleSet,3});
        auto plan=planner::planSolver(p);check(plan.family==planner::SolverFamily::DEM&&plan.executableWithCurrentReferenceSolver,"planner maps particle problem to DEM without demo mode");
    }
    {
        field::GridShape grid{14,14,14,{-1.0,-1.0,-1.0},{2.0/13.0,2.0/13.0,2.0/13.0}};
        field::ScalarField sphere{grid,std::vector<double>(grid.cellCount())};
        for(std::uint32_t z=0;z<grid.nz;++z)for(std::uint32_t y=0;y<grid.ny;++y)for(std::uint32_t x=0;x<grid.nx;++x){double px=grid.origin.x+grid.spacing.x*x,py=grid.origin.y+grid.spacing.y*y,pz=grid.origin.z+grid.spacing.z*z;sphere.values[grid.index(x,y,z)]=0.55-std::sqrt(px*px+py*py+pz*pz);}
        auto iso=visualization::extractIsoSurface(sphere,0.0);check(iso.activeCells>0,"iso-surface identifies active cells");check(!iso.mesh.triangles.empty()&&iso.mesh.vertices.size()==iso.mesh.triangles.size()*3,"scalar field regenerates explicit 3D triangle geometry");
    }
    if(failures){std::cerr<<failures<<" research test(s) failed\n";return 1;}std::cout<<"Vulkax research/visualization tests passed\n";return 0;
}
