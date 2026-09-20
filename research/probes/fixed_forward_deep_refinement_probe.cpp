#include "vulkax/research/nonlinear_deformable_world.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using vulkax::gaussian::GaussianCloud;
using vulkax::math::Vec3;
using vulkax::research::NonlinearDeformableWorldResult;
using vulkax::research::NonlinearDeformableWorldSettings;
using vulkax::solvers::Matrix3;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmParticle;
using vulkax::solvers::MpmTransferScheme;

constexpr double kHorizon = 0.0064;
const std::array<double,8> kDt{
    1.0e-4,5.0e-5,2.5e-5,1.25e-5,6.25e-6,3.125e-6,1.5625e-6,7.8125e-7};
const std::array<double,5> kTimes{0.0009,0.0018,0.0031,0.0047,0.0062};
const std::array<std::size_t,2> kMarkers{9U,54U};

std::vector<MpmParticle> body() {
    std::vector<MpmParticle> ps;
    std::uint64_t id=1;
    constexpr double h=.12, volume=h*h*h, density=1000.;
    for(int z=0; z<4; ++z)
        for(int y=0; y<4; ++y)
            for(int x=0; x<4; ++x) {
                MpmParticle p;
                p.id=id++;
                p.restPosition={(x-1.5)*h,(y-1.5)*h,(z-1.5)*h};
                p.position=p.restPosition;
                p.mass=density*volume;
                p.restVolume=volume;
                ps.push_back(p);
            }
    return ps;
}

vulkax::gaussian::GaussianSplat splat(Vec3 p) {
    vulkax::gaussian::GaussianSplat s;
    s.position=p;
    s.logScale={std::log(.06),std::log(.045),std::log(.035)};
    s.rotation={1.,0.,0.,0.};
    s.opacityLogit=4.;
    return s;
}

GaussianCloud world() {
    GaussianCloud w;
    w.splats.push_back(splat({-.08,.04,.02}));
    w.splats.push_back(splat({.09,-.06,.03}));
    w.splats.push_back(splat({.04,.10,-.07}));
    w.splats.push_back(splat({-.05,-.08,-.06}));
    w.splats.push_back(splat({.76,.55,-.30}));
    return w;
}

MpmGridSettings grid() {
    MpmGridSettings g;
    g.origin={-.8,-.8,-.8};
    g.nx=22; g.ny=22; g.nz=22;
    g.cellSize=.08;
    g.boundaryCells=0;
    return g;
}

Vec3 mul(const Matrix3& m, Vec3 q) {
    return {
        m[0]*q.x+m[1]*q.y+m[2]*q.z,
        m[3]*q.x+m[4]*q.y+m[5]*q.z,
        m[6]*q.x+m[7]*q.y+m[8]*q.z};
}

double rms(const std::vector<double>& a, const std::vector<double>& b) {
    if(a.size()!=b.size() || a.empty()) throw std::runtime_error("invalid RMS input");
    double sum=0.;
    for(std::size_t i=0;i<a.size();++i) {
        const double d=a[i]-b[i];
        sum+=d*d;
    }
    return std::sqrt(sum/static_cast<double>(a.size()));
}

double particleRms(const std::vector<MpmParticle>& a,const std::vector<MpmParticle>& b) {
    if(a.size()!=b.size() || a.empty()) throw std::runtime_error("invalid particle RMS input");
    double sum=0.;
    for(std::size_t i=0;i<a.size();++i) {
        const auto d=a[i].position-b[i].position;
        sum+=vulkax::math::dot(d,d);
    }
    return std::sqrt(sum/static_cast<double>(a.size()));
}

std::vector<double> initialObs(const Matrix3& d) {
    const auto b=body();
    std::vector<double> y;
    for(std::size_t ti=0;ti<kTimes.size();++ti) {
        (void)ti;
        for(const auto marker:kMarkers) {
            const auto q=mul(d,b.at(marker).restPosition);
            y.push_back(q.x); y.push_back(q.y); y.push_back(q.z);
        }
    }
    return y;
}

struct Trace {
    double dt{};
    std::vector<double> observations;
    std::vector<MpmParticle> finalParticles;
    double maxEnergyDrift{};
    double minJ{};
};

Trace simulate(double E,double nu,const Matrix3& deformation,double dt) {
    const auto steps=static_cast<std::size_t>(std::llround(kHorizon/dt));
    if(steps==0 || std::abs(static_cast<double>(steps)*dt-kHorizon)>1e-12)
        throw std::runtime_error("deep-refinement dt must divide horizon");

    std::array<std::size_t,kTimes.size()> sampleSteps{};
    for(std::size_t i=0;i<kTimes.size();++i) {
        const double exact=kTimes[i]/dt;
        sampleSteps[i]=static_cast<std::size_t>(std::llround(exact));
        if(std::abs(static_cast<double>(sampleSteps[i])*dt-kTimes[i])>1e-12)
            throw std::runtime_error("observation time must lie exactly on refinement timestep");
    }

    NonlinearDeformableWorldSettings s;
    s.steps=steps;
    s.dt=dt;
    s.material={1000.,E,nu};
    s.initialDeformation=deformation;
    s.couplingNeighborCount=20;
    s.transferScheme=MpmTransferScheme::APIC;

    std::array<std::vector<double>,kTimes.size()> samples;
    const auto result=vulkax::research::runNonlinearDeformableWorld(
        world(),{0,1,2,3},body(),grid(),s,{},
        [&](const auto& frame,const GaussianCloud&,const std::vector<MpmParticle>& ps) {
            for(std::size_t ti=0;ti<sampleSteps.size();++ti) {
                if(frame.step!=sampleSteps[ti]) continue;
                auto& dst=samples[ti];
                for(const auto marker:kMarkers) {
                    const auto& q=ps.at(marker).position;
                    dst.push_back(q.x); dst.push_back(q.y); dst.push_back(q.z);
                }
            }
        });

    Trace t;
    t.dt=dt;
    for(std::size_t ti=0;ti<samples.size();++ti) {
        if(samples[ti].size()!=kMarkers.size()*3U)
            throw std::runtime_error("failed to capture deep-refinement observation sample");
        t.observations.insert(t.observations.end(),samples[ti].begin(),samples[ti].end());
    }
    t.finalParticles=result.finalParticles;
    t.maxEnergyDrift=result.maximumRelativeMechanicalEnergyDrift;
    t.minJ=result.minimumDeformationDeterminant;
    return t;
}

struct Case { const char* name; double E; double nu; };

} // namespace

int main(int argc,char** argv) {
    const std::filesystem::path outDir=argc>1?argv[1]:"build/fixed-forward-deep-refinement";
    std::filesystem::create_directories(outDir);

    const Matrix3 target{1.111,.142,.057,.076,.889,.087,.091,.049,1.054};
    const std::array<Case,2> cases{{{"on_grid",15000.,.30},{"off_grid",15900.,.32}}};

    std::ofstream csv(outDir/"levels.csv");
    csv<<"case,dt_s,steps,E_pa,nu,observation_effect_rms_m,adjacent_observation_rms_m,"
          "adjacent_observation_fraction,observation_rms_to_finest_m,"
          "observation_fraction_to_finest,adjacent_particle_rms_m,particle_rms_to_finest_m,"
          "max_relative_energy_drift,min_J\n";

    for(const auto& c:cases) {
        std::vector<Trace> traces;
        traces.reserve(kDt.size());
        for(const auto dt:kDt) {
            std::cout<<"DEEP_REFINEMENT_START "<<c.name<<" dt="<<std::setprecision(17)<<dt<<"\n";
            traces.push_back(simulate(c.E,c.nu,target,dt));
        }

        const auto initial=initialObs(target);
        const auto& finest=traces.back();
        for(std::size_t i=0;i<traces.size();++i) {
            const auto& t=traces[i];
            const double effect=rms(t.observations,initial);
            const double toFinest=rms(t.observations,finest.observations);
            const double toFinestFrac=toFinest/std::max(effect,1e-15);
            double adjacentObs=std::numeric_limits<double>::quiet_NaN();
            double adjacentFrac=std::numeric_limits<double>::quiet_NaN();
            double adjacentParticle=std::numeric_limits<double>::quiet_NaN();
            if(i>0) {
                adjacentObs=rms(traces[i-1].observations,t.observations);
                adjacentFrac=adjacentObs/std::max(effect,1e-15);
                adjacentParticle=particleRms(traces[i-1].finalParticles,t.finalParticles);
            }
            const double particleToFinest=particleRms(t.finalParticles,finest.finalParticles);
            const auto steps=static_cast<std::size_t>(std::llround(kHorizon/t.dt));
            csv<<c.name<<','<<std::setprecision(17)<<t.dt<<','<<steps<<','<<c.E<<','<<c.nu<<','
               <<effect<<','<<adjacentObs<<','<<adjacentFrac<<','<<toFinest<<','<<toFinestFrac<<','
               <<adjacentParticle<<','<<particleToFinest<<','<<t.maxEnergyDrift<<','<<t.minJ<<"\n";
            std::cout<<"DEEP_REFINEMENT "<<c.name
                     <<" dt="<<t.dt
                     <<" adj_frac="<<adjacentFrac
                     <<" to_finest_frac="<<toFinestFrac
                     <<" energy_drift="<<t.maxEnergyDrift
                     <<" minJ="<<t.minJ<<"\n";
        }
    }

    std::ofstream meta(outDir/"metadata.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.fixed_forward_deep_refinement\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic-label-free-fixed-physics-numerical-diagnostic\",\n"
        <<"  \"transfer\": \"APIC\",\n"
        <<"  \"grid_cell_m\": 0.08,\n"
        <<"  \"dt_ladder_s\": [0.0001,0.00005,0.000025,0.0000125,0.00000625,0.000003125,0.0000015625],\n"
        <<"  \"warning\": \"No inverse fit, safety label, or acceptance-policy tuning is performed. The finest level is a discrete numerical reference, not continuum truth.\"\n"
        <<"}\n";
    return 0;
}
