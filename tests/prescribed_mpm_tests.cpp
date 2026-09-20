#include "vulkax/research/prescribed_mpm.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

std::vector<vulkax::solvers::MpmParticle> particles() {
    std::vector<vulkax::solvers::MpmParticle> ps;
    std::uint64_t id=1;
    for(int z=0;z<2;++z) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        vulkax::solvers::MpmParticle p;
        p.id=id++;
        p.restPosition={-0.06+0.12*x,-0.06+0.12*y,-0.06+0.12*z};
        p.position=p.restPosition;
        p.mass=1.0;
        p.restVolume=0.001728;
        ps.push_back(p);
    }
    return ps;
}

vulkax::solvers::MpmGridSettings grid() {
    vulkax::solvers::MpmGridSettings g;
    g.origin={-0.6,-0.6,-0.6};
    g.nx=16;g.ny=16;g.nz=16;g.cellSize=0.08;g.boundaryCells=0;
    return g;
}

}

int main() {
    using namespace vulkax;
    {
        auto ps=particles();
        solvers::MpmMaterial material;
        material.youngModulus=0.0;
        material.poissonRatio=0.3;
        const double dt=0.01;
        const auto targetPosition=ps[0].position + math::Vec3{0.001,0.002,-0.001};
        const math::Vec3 targetVelocity{0.1,0.2,-0.1};
        const auto evidence=research::stepMpmWithPrescribedParticles(
            ps,grid(),material,dt,
            {{ps[0].id,targetPosition,targetVelocity,true}},
            {0.0,0.0,0.0});
        assert(evidence.constrainedParticleCount==1);
        assert(math::length(ps[0].position-targetPosition)<1.0e-15);
        assert(math::length(ps[0].velocity-targetVelocity)<1.0e-15);
        assert(evidence.totalConstraintImpulseMagnitude>0.0);
        assert(evidence.maximumPositionCorrection>0.0);
        assert(evidence.momentumAccountingError<1.0e-12);
        const double expectedKinetic=0.5*ps[0].mass*math::dot(targetVelocity,targetVelocity);
        assert(std::abs(evidence.kineticConstraintWork-expectedKinetic)<1.0e-12);
    }

    {
        auto ps=particles();
        bool threw=false;
        try {
            (void)research::stepMpmWithPrescribedParticles(
                ps,grid(),{},0.01,
                {{9999,{0,0,0},{0,0,0},true}},
                {0,0,0});
        } catch(const std::invalid_argument&) { threw=true; }
        assert(threw);
    }

    {
        auto ps=particles();
        bool threw=false;
        try {
            (void)research::stepMpmWithPrescribedParticles(
                ps,grid(),{},0.01,
                {{ps[0].id,{0,0,0},{0,0,0},true},
                 {ps[0].id,{0,0,0},{0,0,0},true}},
                {0,0,0});
        } catch(const std::invalid_argument&) { threw=true; }
        assert(threw);
    }
    return 0;
}
