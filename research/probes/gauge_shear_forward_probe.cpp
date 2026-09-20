#include "vulkax/research/prescribed_mpm.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using vulkax::math::Vec3;
using vulkax::research::PrescribedParticleTarget;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmMaterial;
using vulkax::solvers::MpmParticle;

struct Config {
    std::string material;
    int trial{};
    double E{},nu{},rho{},mass{};
    Vec3 span{},driver{};
    double duration{};
    double measuredMarkerNonaffine{};
    double measuredFaceNonaffine{};
};
struct Sample {
    double time{}, driverFraction{}, particleNonaffineRms{}, particleNonaffinePeak{};
};
struct Result {
    Config cfg;
    std::size_t particles{}, steps{};
    double dt{}, cell{}, minJ{1.0}, maxMomentumAccountingError{}, maxBoundaryError{};
    double nonaffineTimeRms{}, finalNonaffineRms{}, peakNonaffineRms{}, finalNonaffinePeak{};
    double ratioToMeasuredMarker{}, constraintImpulse{};
};

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out; std::stringstream ss(s); std::string x;
    while(std::getline(ss,x,',')) {
        if(!x.empty() && x.back()=='\r') x.pop_back();
        out.push_back(x);
    }
    return out;
}
double d(const std::string& x){ return std::stod(x); }

std::vector<Config> readConfig(const std::filesystem::path& path) {
    std::ifstream in(path); if(!in) throw std::runtime_error("cannot open forward config");
    std::string line; std::getline(in,line); const auto h=split(line);
    auto col=[&](const std::string& name){
        auto it=std::find(h.begin(),h.end(),name);
        if(it==h.end()) throw std::runtime_error("missing config column "+name);
        return static_cast<std::size_t>(std::distance(h.begin(),it));
    };
    const auto material=col("material"),trial=col("trial"),E=col("young_pa"),nu=col("poisson"),
      rho=col("density_kg_m3"),mass=col("mass_kg"),sx=col("span_x_m"),sy=col("span_y_m"),sz=col("span_z_m"),
      dx=col("driver_dx_m"),dy=col("driver_dy_m"),dz=col("driver_dz_m"),dur=col("duration_s"),
      mm=col("measured_marker_nonaffine_rms_m"),fm=col("measured_face_nonaffine_rms");
    std::vector<Config> out;
    while(std::getline(in,line)) {
        if(line.empty()) continue; const auto v=split(line);
        Config c; c.material=v.at(material); c.trial=std::stoi(v.at(trial)); c.E=d(v.at(E)); c.nu=d(v.at(nu));
        c.rho=d(v.at(rho)); c.mass=d(v.at(mass)); c.span={d(v.at(sx)),d(v.at(sy)),d(v.at(sz))};
        c.driver={d(v.at(dx)),d(v.at(dy)),d(v.at(dz))}; c.duration=d(v.at(dur));
        c.measuredMarkerNonaffine=d(v.at(mm)); c.measuredFaceNonaffine=d(v.at(fm)); out.push_back(c);
    }
    return out;
}

std::size_t longestAxis(Vec3 s) {
    if(s.y>=s.x && s.y>=s.z) return 1;
    if(s.z>=s.x && s.z>=s.y) return 2;
    return 0;
}
double comp(Vec3 v,std::size_t a){ return a==0?v.x:(a==1?v.y:v.z); }

std::vector<MpmParticle> makeBody(const Config& c,double cell,std::array<std::size_t,3>& counts) {
    counts={
      std::max<std::size_t>(3,static_cast<std::size_t>(std::llround(c.span.x/cell))+1),
      std::max<std::size_t>(3,static_cast<std::size_t>(std::llround(c.span.y/cell))+1),
      std::max<std::size_t>(3,static_cast<std::size_t>(std::llround(c.span.z/cell))+1)};
    for(auto& n:counts) n=std::min<std::size_t>(n,12);
    const double volume=c.span.x*c.span.y*c.span.z/static_cast<double>(counts[0]*counts[1]*counts[2]);
    std::vector<MpmParticle> ps; ps.reserve(counts[0]*counts[1]*counts[2]); std::uint64_t id=1;
    for(std::size_t k=0;k<counts[2];++k) for(std::size_t j=0;j<counts[1];++j) for(std::size_t i=0;i<counts[0];++i) {
        const double x=-.5*c.span.x + c.span.x*static_cast<double>(i)/static_cast<double>(counts[0]-1);
        const double y=-.5*c.span.y + c.span.y*static_cast<double>(j)/static_cast<double>(counts[1]-1);
        const double z=-.5*c.span.z + c.span.z*static_cast<double>(k)/static_cast<double>(counts[2]-1);
        MpmParticle p; p.id=id++; p.restPosition={x,y,z}; p.position=p.restPosition;
        p.restVolume=volume; p.mass=c.rho*volume; ps.push_back(p);
    }
    return ps;
}
MpmGridSettings makeGrid(const Config& c,double cell) {
    const double margin=4.0*cell;
    MpmGridSettings g; g.cellSize=cell; g.boundaryCells=0;
    g.origin={-.5*c.span.x-margin,-.5*c.span.y-margin,-.5*c.span.z-margin};
    g.nx=static_cast<std::size_t>(std::ceil((c.span.x+2*margin)/cell))+2;
    g.ny=static_cast<std::size_t>(std::ceil((c.span.y+2*margin)/cell))+2;
    g.nz=static_cast<std::size_t>(std::ceil((c.span.z+2*margin)/cell))+2;
    return g;
}

std::pair<double,double> nonaffine(const std::vector<MpmParticle>& ps,const Config& c,std::size_t axis,double frac) {
    const double lo=-.5*comp(c.span,axis), span=comp(c.span,axis);
    double sq=0.,peak=0.;
    for(const auto& p:ps) {
        const double alpha=(comp(p.restPosition,axis)-lo)/span;
        const Vec3 expected=p.restPosition+c.driver*(frac*alpha);
        const double e=vulkax::math::length(p.position-expected);
        sq+=e*e; peak=std::max(peak,e);
    }
    return {std::sqrt(sq/static_cast<double>(ps.size())),peak};
}

Result run(const Config& c,const std::filesystem::path& outDir) {
    const double maxSpan=std::max({c.span.x,c.span.y,c.span.z});
    const double cell=std::max(0.012,std::min(0.020,maxSpan/8.0));
    std::array<std::size_t,3> counts{}; auto ps=makeBody(c,cell,counts); const auto initial=ps;
    const auto grid=makeGrid(c,cell); const std::size_t axis=longestAxis(c.span);
    const double wave=std::sqrt(std::max(c.E,1.0)/std::max(c.rho,1e-9));
    const double dtStable=0.32*cell/std::max(wave,1e-9);
    const double dt=std::min(1.0e-4,dtStable);
    const std::size_t steps=std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(c.duration/dt)));
    const double actualDt=c.duration/static_cast<double>(steps);
    const MpmMaterial mat{c.rho,c.E,c.nu};

    const double amin=-.5*comp(c.span,axis),amax=.5*comp(c.span,axis);
    const Vec3 velocity=c.driver/c.duration;
    std::vector<Sample> samples; const std::size_t stride=std::max<std::size_t>(1,steps/120);
    double minJ=std::numeric_limits<double>::infinity(),maxMom=0.,maxBoundary=0.,impulse=0.,sumTimeSq=0.; std::size_t sampleCount=0;
    double peakRms=0.,finalRms=0.,finalPeak=0.;

    for(std::size_t step=1;step<=steps;++step) {
        const double frac=static_cast<double>(step)/static_cast<double>(steps);
        std::vector<PrescribedParticleTarget> targets; targets.reserve(ps.size()/3);
        for(const auto& p:ps) {
            const auto& r=initial.at(static_cast<std::size_t>(p.id-1));
            const double a=comp(r.restPosition,axis);
            if(std::abs(a-amin)<1e-12) targets.push_back({p.id,r.restPosition,{0,0,0},true});
            else if(std::abs(a-amax)<1e-12) targets.push_back({p.id,r.restPosition+c.driver*frac,velocity,true});
        }
        const auto ev=vulkax::research::stepMpmWithPrescribedParticles(ps,grid,mat,actualDt,targets,{0,0,0});
        minJ=std::min(minJ,ev.unconstrainedStep.minimumDeformationDeterminant);
        maxMom=std::max(maxMom,ev.momentumAccountingError);
        maxBoundary=std::max(maxBoundary,ev.maximumPositionCorrection);
        impulse+=ev.totalConstraintImpulseMagnitude;
        if(step%stride==0 || step==steps) {
            const auto [rms,pk]=nonaffine(ps,c,axis,frac);
            samples.push_back({step*actualDt,frac,rms,pk}); sumTimeSq+=rms*rms; ++sampleCount;
            peakRms=std::max(peakRms,rms); if(step==steps){finalRms=rms;finalPeak=pk;}
        }
    }

    std::ofstream curve(outDir/(c.material+"_forward_curve.csv"));
    curve<<"time_s,driver_fraction,particle_nonaffine_rms_m,particle_nonaffine_peak_m\n";
    for(const auto&s:samples)curve<<std::setprecision(17)<<s.time<<','<<s.driverFraction<<','<<s.particleNonaffineRms<<','<<s.particleNonaffinePeak<<'\n';

    Result r; r.cfg=c;r.particles=ps.size();r.steps=steps;r.dt=actualDt;r.cell=cell;r.minJ=minJ;
    r.maxMomentumAccountingError=maxMom;r.maxBoundaryError=maxBoundary;r.nonaffineTimeRms=std::sqrt(sumTimeSq/static_cast<double>(sampleCount));
    r.finalNonaffineRms=finalRms;r.peakNonaffineRms=peakRms;r.finalNonaffinePeak=finalPeak;
    r.ratioToMeasuredMarker=r.nonaffineTimeRms/std::max(c.measuredMarkerNonaffine,1e-15);r.constraintImpulse=impulse;return r;
}
}

int main(int argc,char**argv){
    if(argc<3){std::cerr<<"usage: vulkax_gauge_forward_probe CONFIG.csv OUTDIR\n";return 2;}
    const auto cfgs=readConfig(argv[1]); const std::filesystem::path outDir=argv[2]; std::filesystem::create_directories(outDir);
    std::ofstream out(outDir/"forward_results.csv");
    out<<"material,trial,young_pa,poisson,density_kg_m3,particles,steps,dt_s,cell_m,min_J,max_momentum_accounting_error,max_boundary_correction_m,particle_nonaffine_time_rms_m,particle_nonaffine_peak_rms_m,final_particle_nonaffine_rms_m,final_particle_nonaffine_peak_m,measured_marker_nonaffine_rms_m,sim_to_measured_marker_ratio,accumulated_constraint_impulse,provenance\n";
    for(const auto& c:cfgs){
        const auto r=run(c,outDir);
        out<<c.material<<','<<c.trial<<','<<std::setprecision(17)<<c.E<<','<<c.nu<<','<<c.rho<<','<<r.particles<<','<<r.steps<<','<<r.dt<<','<<r.cell<<','<<r.minJ<<','<<r.maxMomentumAccountingError<<','<<r.maxBoundaryError<<','<<r.nonaffineTimeRms<<','<<r.peakNonaffineRms<<','<<r.finalNonaffineRms<<','<<r.finalNonaffinePeak<<','<<c.measuredMarkerNonaffine<<','<<r.ratioToMeasuredMarker<<','<<r.constraintImpulse<<",measured-config+vulkax-forward\n";
        std::cout<<"FORWARD "<<c.material<<" particles "<<r.particles<<" steps "<<r.steps<<" dt "<<r.dt
                 <<" minJ "<<r.minJ<<" nonaffine_time_rms "<<r.nonaffineTimeRms
                 <<" measured_marker_rms "<<c.measuredMarkerNonaffine<<" ratio "<<r.ratioToMeasuredMarker<<"\n";
    }
}
