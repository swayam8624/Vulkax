#include "vulkax/physics/dem.hpp"
#include "vulkax/physics/dem_gpu.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {int failures=0;void check(bool c,const std::string&m){if(!c){++failures;std::cerr<<"FAIL: "<<m<<'\n';}}}
int main(int argc,char**argv){using namespace vulkax;if(argc<2||std::string(argv[1])!="Vulkan"){std::cout<<"GPU physics test skipped: Vulkan backend not explicitly requested\n";return 0;}field::ParticleSet p;p.positions={{-0.045,0,0},{0.045,0,0}};p.velocities={{},{}};p.radii={0.05,0.05};p.masses={1,1};physics::dem::Settings s;s.gravity={};s.drum.radius=2;s.drum.halfHeight=2;physics::dem::Solver cpu(p,s);cpu.step(1e-4);auto gpu=physics::dem::stepGpu(p,s,1e-4,{16});check(gpu.ok,"Vulkan GPU DEM execution: "+gpu.diagnostic);if(gpu.ok){check(gpu.overflowingCells==0,"GPU spatial hash has no overflow");check(gpu.particleContacts==1,"GPU DEM detects pair contact");check(gpu.particles.velocities[0].x<0&&gpu.particles.velocities[1].x>0,"GPU DEM generates separating impulse");check(std::abs(gpu.particles.velocities[0].x-cpu.particles().velocities[0].x)<2e-4,"GPU DEM agrees with CPU reference for pair contact");}
field::ParticleSet lattice;for(int z=0;z<4;++z)for(int y=-5;y<=5;++y)for(int x=-5;x<=5;++x){lattice.positions.push_back({x*0.09,y*0.09,(z-1.5)*0.09});lattice.velocities.push_back({});lattice.radii.push_back(0.04);lattice.masses.push_back(0.5);}auto many=physics::dem::stepGpu(lattice,s,1e-4,{32});check(many.ok,"Vulkan spatial-hash DEM handles dense 484-particle step without overflow: "+many.diagnostic);check(many.particles.positions.size()==lattice.positions.size(),"GPU DEM preserves particle count");if(failures){std::cerr<<failures<<" GPU physics test(s) failed\n";return 1;}std::cout<<"Vulkan GPU DEM tests passed on "<<gpu.deviceName<<" in "<<gpu.wallMilliseconds<<" ms\n";return 0;}
