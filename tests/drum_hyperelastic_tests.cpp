#include "vulkax/solvers/hyperelastic_fem.hpp"
#include "vulkax/solvers/rotating_drum.hpp"

#include <cassert>
#include <cmath>
#include <vector>

int main() {
    using namespace vulkax;

    std::vector<solvers::DemParticle> particles;
    for(int i=0;i<24;++i) {
        const double angle=6.283185307179586*static_cast<double>(i)/24.0;
        const double radius=0.82;
        particles.push_back({{radius*std::cos(angle),radius*std::sin(angle),0.0},
                             {0.5*std::cos(angle),0.5*std::sin(angle),0.0},0.07,1.0});
    }
    const auto drum=solvers::advanceRotatingDrum(
        particles,{1.0,0.35,4.0},{5.0e-4,{0.0,-9.80665,0.0},2.0e4,15.0,0.45,0.35},800);
    assert(drum.wallCollisions>0);
    assert(drum.broadphaseCandidates>0);
    assert(drum.wallImpactEnergy>=0.0);
    assert(std::isfinite(drum.meanSpeed));
    for(const auto& p:particles) {
        assert(std::hypot(p.position.x,p.position.y)+p.radius<=1.0+1.0e-10);
        assert(std::abs(p.position.z)+p.radius<=0.35+1.0e-10);
    }

    std::vector<solvers::FemNode> nodes(4);
    nodes[0].position={0.0,0.0,0.0}; nodes[1].position={1.0,0.0,0.0};
    nodes[2].position={0.0,1.0,0.0}; nodes[3].position={0.0,0.0,1.0};
    nodes[0].fixed={true,true,true}; nodes[1].fixed={true,true,true}; nodes[2].fixed={true,true,true};
    nodes[3].force={0.0,0.0,-120.0};
    const auto nonlinear=solvers::solveNeoHookeanTetrahedral(
        nodes,{{{0,1,2,3}}},{8.0e4,0.35},{35,5.0e-3,2.0e-5,1.0e-6});
    assert(nonlinear.converged);
    assert(nonlinear.displacement[3].z<0.0);
    assert(nonlinear.strainEnergy>0.0);
    assert(nonlinear.vonMisesStress[0]>0.0);
    assert(std::isfinite(nonlinear.totalPotential));
    return 0;
}
