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
using vulkax::research::NonlinearDeformableWorldSettings;
using vulkax::solvers::Matrix3;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmParticle;
using vulkax::solvers::MpmTransferScheme;

constexpr double kSupportWidth = 0.48;
constexpr double kDensity = 1000.0;
constexpr double kDomainMin = -0.8;
constexpr double kDomainMax = 0.96;
constexpr double kHorizon = 0.0064;
constexpr double kDt = 1.5625e-6;
const std::array<int,3> kResolution{4,5,6};
const std::array<double,5> kTimes{0.0009,0.0018,0.0031,0.0047,0.0062};

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

std::vector<MpmParticle> body(int n) {
    if(n<4) throw std::runtime_error("co-refinement requires n >= 4");
    const double h=kSupportWidth/static_cast<double>(n);
    const double particleVolume=h*h*h;
    const double particleMass=kDensity*particleVolume;
    const double center=0.5*static_cast<double>(n-1);
    std::vector<MpmParticle> ps;
    ps.reserve(static_cast<std::size_t>(n*n*n));
    std::uint64_t id=1;
    for(int z=0;z<n;++z)
        for(int y=0;y<n;++y)
            for(int x=0;x<n;++x) {
                MpmParticle p;
                p.id=id++;
                p.restPosition={
                    (static_cast<double>(x)-center)*h,
                    (static_cast<double>(y)-center)*h,
                    (static_cast<double>(z)-center)*h};
                p.position=p.restPosition;
                p.mass=particleMass;
                p.restVolume=particleVolume;
                ps.push_back(p);
            }
    return ps;
}

double gridCell(int n) {
    const double particleSpacing=kSupportWidth/static_cast<double>(n);
    return (2.0/3.0)*particleSpacing;
}

MpmGridSettings grid(int n) {
    const double cell=gridCell(n);
    const auto cells=static_cast<std::size_t>(std::ceil((kDomainMax-kDomainMin)/cell));
    MpmGridSettings g;
    g.origin={kDomainMin,kDomainMin,kDomainMin};
    g.nx=cells; g.ny=cells; g.nz=cells;
    g.cellSize=cell;
    g.boundaryCells=0;
    return g;
}

Vec3 mul(const Matrix3& m,Vec3 q) {
    return {
        m[0]*q.x+m[1]*q.y+m[2]*q.z,
        m[3]*q.x+m[4]*q.y+m[5]*q.z,
        m[6]*q.x+m[7]*q.y+m[8]*q.z};
}

double rms(const std::vector<double>& a,const std::vector<double>& b) {
    if(a.size()!=b.size() || a.empty()) throw std::runtime_error("invalid RMS vectors");
    double sum=0.0;
    for(std::size_t i=0;i<a.size();++i) {
        const double d=a[i]-b[i];
        sum+=d*d;
    }
    return std::sqrt(sum/static_cast<double>(a.size()));
}

std::vector<double> initialObs(const Matrix3& deformation) {
    const auto w=world();
    std::vector<double> out;
    for(std::size_t ti=0;ti<kTimes.size();++ti) {
        (void)ti;
        for(std::size_t i=0;i<4U;++i) {
            const auto p=mul(deformation,w.splats[i].position);
            out.push_back(p.x);out.push_back(p.y);out.push_back(p.z);
        }
    }
    return out;
}

struct Trace {
    int n{};
    double cell{};
    std::vector<double> observations;
    double effect{};
    double maxEnergyDrift{};
    double minJ{};
    double maxMlsRms{};
    double maxMls{};
};

Trace simulate(double E,double nu,const Matrix3& deformation,int n) {
    const auto steps=static_cast<std::size_t>(std::llround(kHorizon/kDt));
    if(std::abs(static_cast<double>(steps)*kDt-kHorizon)>1e-12)
        throw std::runtime_error("co-refinement timestep must divide horizon");

    std::array<std::size_t,kTimes.size()> sampleSteps{};
    for(std::size_t i=0;i<kTimes.size();++i) {
        const double exact=kTimes[i]/kDt;
        sampleSteps[i]=static_cast<std::size_t>(std::llround(exact));
        if(std::abs(static_cast<double>(sampleSteps[i])*kDt-kTimes[i])>1e-12)
            throw std::runtime_error("co-refinement observation time must lie on timestep");
    }

    NonlinearDeformableWorldSettings s;
    s.steps=steps;
    s.dt=kDt;
    s.material={kDensity,E,nu};
    s.initialDeformation=deformation;
    s.couplingNeighborCount=20;
    s.transferScheme=MpmTransferScheme::APIC;

    std::array<std::vector<double>,kTimes.size()> samples;
    const auto result=vulkax::research::runNonlinearDeformableWorld(
        world(),{0,1,2,3},body(n),grid(n),s,{},
        [&](const auto& frame,const GaussianCloud& cloud,const std::vector<MpmParticle>&) {
            for(std::size_t ti=0;ti<sampleSteps.size();++ti) {
                if(frame.step!=sampleSteps[ti]) continue;
                auto& dst=samples[ti];
                for(std::size_t gi=0;gi<4U;++gi) {
                    const auto& p=cloud.splats[gi].position;
                    dst.push_back(p.x);dst.push_back(p.y);dst.push_back(p.z);
                }
            }
        });

    Trace t;
    t.n=n;
    t.cell=gridCell(n);
    for(const auto& sample:samples) {
        if(sample.size()!=12U)
            throw std::runtime_error("failed to capture co-refinement Gaussian observations");
        t.observations.insert(t.observations.end(),sample.begin(),sample.end());
    }
    t.effect=rms(t.observations,initialObs(deformation));
    t.maxEnergyDrift=result.maximumRelativeMechanicalEnergyDrift;
    t.minJ=result.minimumDeformationDeterminant;
    t.maxMlsRms=result.maximumMlsRmsResidual;
    t.maxMls=result.maximumMlsResidual;
    return t;
}

struct Case { const char* name; double E; double nu; };

} // namespace

int main(int argc,char** argv) {
    const std::filesystem::path outDir=argc>1?argv[1]:"build/fixed-forward-co-refinement";
    std::filesystem::create_directories(outDir);

    const Matrix3 target{1.111,.142,.057,.076,.889,.087,.091,.049,1.054};
    const std::array<Case,2> cases{{{"on_grid",15000.,.30},{"off_grid",15900.,.32}}};

    std::ofstream csv(outDir/"levels.csv");
    csv<<"case,n_axis,particles,particle_spacing_m,grid_cell_m,grid_n,dt_s,E_pa,nu,"
          "observation_effect_rms_m,adjacent_observation_rms_m,adjacent_observation_fraction,"
          "rms_to_finest_m,fraction_to_finest,max_relative_energy_drift,min_J,"
          "max_mls_rms_residual,max_mls_residual\n";

    for(const auto& c:cases) {
        std::vector<Trace> traces;
        for(const int n:kResolution) {
            std::cout<<"COREFINE_START "<<c.name<<" n="<<n
                     <<" cell="<<std::setprecision(17)<<gridCell(n)<<"\n";
            traces.push_back(simulate(c.E,c.nu,target,n));
        }
        const auto& finest=traces.back();
        for(std::size_t i=0;i<traces.size();++i) {
            const auto& t=traces[i];
            double adjacent=std::numeric_limits<double>::quiet_NaN();
            double adjacentFraction=std::numeric_limits<double>::quiet_NaN();
            if(i>0) {
                adjacent=rms(traces[i-1].observations,t.observations);
                adjacentFraction=adjacent/std::max(t.effect,1e-15);
            }
            const double toFinest=rms(t.observations,finest.observations);
            const double toFinestFraction=toFinest/std::max(t.effect,1e-15);
            const double spacing=kSupportWidth/static_cast<double>(t.n);
            const auto gn=static_cast<std::size_t>(std::ceil((kDomainMax-kDomainMin)/t.cell));
            csv<<c.name<<','<<t.n<<','<<(t.n*t.n*t.n)<<','<<std::setprecision(17)
               <<spacing<<','<<t.cell<<','<<gn<<','<<kDt<<','<<c.E<<','<<c.nu<<','
               <<t.effect<<','<<adjacent<<','<<adjacentFraction<<','<<toFinest<<','
               <<toFinestFraction<<','<<t.maxEnergyDrift<<','<<t.minJ<<','
               <<t.maxMlsRms<<','<<t.maxMls<<"\n";
            std::cout<<"COREFINE "<<c.name<<" n="<<t.n
                     <<" adjacent_fraction="<<adjacentFraction
                     <<" to_finest_fraction="<<toFinestFraction
                     <<" energy_drift="<<t.maxEnergyDrift
                     <<" minJ="<<t.minJ
                     <<" mls_rms="<<t.maxMlsRms<<"\n";
        }
    }

    std::ofstream meta(outDir/"metadata.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.fixed_forward_co_refinement\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic-label-free-fixed-physics-particle-grid-corefinement\",\n"
        <<"  \"dt_s\": "<<std::setprecision(17)<<kDt<<",\n"
        <<"  \"support_width_m\": "<<kSupportWidth<<",\n"
        <<"  \"resolution_n_axis\": [4,5,6],\n"
        <<"  \"grid_to_particle_spacing_ratio\": 0.6666666666666666,\n"
        <<"  \"selection_labels_used\": false,\n"
        <<"  \"warning\": \"Particle mass/rest volume and physical support are held constant under co-refinement. The n=6 result is a discrete comparator, not continuum truth.\"\n"
        <<"}\n";
    return 0;
}
