#include "vulkax/physics/cfd.hpp"
#include "vulkax/physics/dem.hpp"
#include "vulkax/physics/fem.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace { int failures=0; void check(bool c,const std::string&m){if(!c){++failures;std::cerr<<"FAIL: "<<m<<'\n';}} }

int main(){
    using namespace vulkax;
    {
        field::ParticleSet particles;
        particles.positions={{-0.045,0,0},{0.045,0,0}};
        particles.velocities={{},{}};
        particles.radii={0.05,0.05}; particles.masses={1.0,1.0};
        physics::dem::Settings settings; settings.gravity={}; settings.drum.radius=10.0; settings.drum.halfHeight=10.0;
        physics::dem::Solver solver(std::move(particles),settings); solver.step(1e-4);
        check(solver.statistics().particleContacts==1,"DEM detects one pair contact");
        check(solver.particles().velocities[0].x<0&&solver.particles().velocities[1].x>0,"DEM overlap generates separating impulse");
    }
    {
        physics::fem::TetMesh mesh;
        mesh.restPositions={{0,0,0},{1,0,0},{0,1,0},{0,0,1}}; mesh.positions=mesh.restPositions;
        mesh.velocities.assign(4,{}); mesh.tetrahedra={{{0,1,2,3}}}; mesh.fixed.assign(4,false);
        physics::fem::NeoHookeanMaterial mat{1000.0,2.0e5,2.0e6};
        physics::fem::Solver rest(mesh,mat); auto f0=rest.elasticForces();
        double restNorm=0;for(auto f:f0)restNorm+=field::length(f);check(restNorm<1e-8,"Neo-Hookean rest tetra has near-zero elastic force");
        mesh.positions[1].x=1.1; physics::fem::Solver stretched(mesh,mat); auto fs=stretched.elasticForces();
        check(fs[1].x<0.0,"stretched tetra generates restoring force");

        constexpr double mu=1.7e6;std::vector<physics::fem::UniaxialDatum> data;
        for(double stretch:{1.05,1.2,1.5,1.8})data.push_back({stretch,mu*(stretch-1.0/(stretch*stretch)),1.0});
        auto fit=physics::fem::calibrateIncompressibleNeoHookean(data,100.0);
        check(fit.valid&&std::abs(fit.shearModulus-mu)/mu<1e-12,"material calibration recovers synthetic Neo-Hookean modulus");
        check(fit.information>0,"calibration reports information measure");
    }
    {
        constexpr double pi=3.14159265358979323846;
        physics::cfd::MacGrid2D grid(32,24,1.0,1.0);
        for(std::uint32_t y=0;y<grid.ny;++y)for(std::uint32_t x=1;x<grid.nx;++x)
            grid.u[grid.uFace(x,y)]=std::sin(2*pi*static_cast<double>(x)/grid.nx)*std::sin(pi*(static_cast<double>(y)+0.5)/grid.ny);
        auto stats=physics::cfd::projectIncompressible(grid,0.05,500);
        check(stats.divergenceBefore>1e-3,"CFD test starts divergent");
        check(stats.divergenceAfter<stats.divergenceBefore*0.02,"MAC pressure projection strongly reduces divergence");
    }
    if(failures){std::cerr<<failures<<" physics test(s) failed\n";return 1;}std::cout<<"Vulkax physics reference tests passed\n";return 0;
}
