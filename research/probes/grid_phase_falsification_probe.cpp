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

constexpr double kSupportWidth=0.48;
constexpr double kDensity=1000.0;
constexpr double kBaseMin=-0.8;
constexpr double kTargetMax=0.96;
constexpr double kHorizon=0.0064;
constexpr double kDt=1.5625e-6;
constexpr double kLockedPhase=0.75;
const std::array<double,4> kPhases{0.0,0.25,0.50,0.75};
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

double spacing(int n) { return kSupportWidth/static_cast<double>(n); }
double gridCell(int n) { return (2.0/3.0)*spacing(n); }

std::vector<MpmParticle> body(int n) {
    const double h=spacing(n);
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

MpmGridSettings grid(int n,double phase) {
    if(!(phase>=0.0 && phase<1.0)) throw std::runtime_error("grid phase must be in [0,1)");
    const double h=spacing(n);
    const double cell=gridCell(n);
    const double first=-0.5*static_cast<double>(n-1)*h;
    const double ratio=(first-kBaseMin)/cell;
    const double k=std::floor(ratio-phase);
    const double origin=first-(k+phase)*cell;
    const auto cells=static_cast<std::size_t>(std::ceil((kTargetMax-origin)/cell))+2U;
    MpmGridSettings g;
    g.origin={origin,origin,origin};
    g.nx=cells;g.ny=cells;g.nz=cells;
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
    if(a.size()!=b.size() || a.empty()) throw std::runtime_error("invalid RMS input");
    double sum=0.0;
    for(std::size_t i=0;i<a.size();++i) {
        const double d=a[i]-b[i];
        sum+=d*d;
    }
    return std::sqrt(sum/static_cast<double>(a.size()));
}

std::vector<double> initialObs(const Matrix3& d) {
    const auto w=world();
    std::vector<double> out;
    for(std::size_t ti=0;ti<kTimes.size();++ti) {
        (void)ti;
        for(std::size_t i=0;i<4U;++i) {
            const auto p=mul(d,w.splats[i].position);
            out.push_back(p.x);out.push_back(p.y);out.push_back(p.z);
        }
    }
    return out;
}

struct Trace {
    int n{};
    double phase{};
    double origin{};
    double cell{};
    std::vector<double> observations;
    double effect{};
    double energyDrift{};
    double minJ{};
    double mlsRms{};
};

Trace simulate(double E,double nu,const Matrix3& deformation,int n,double phase) {
    const auto steps=static_cast<std::size_t>(std::llround(kHorizon/kDt));
    std::array<std::size_t,kTimes.size()> sampleSteps{};
    for(std::size_t i=0;i<kTimes.size();++i) {
        const double exact=kTimes[i]/kDt;
        sampleSteps[i]=static_cast<std::size_t>(std::llround(exact));
        if(std::abs(static_cast<double>(sampleSteps[i])*kDt-kTimes[i])>1e-12)
            throw std::runtime_error("observation time does not lie on diagnostic timestep");
    }

    NonlinearDeformableWorldSettings s;
    s.steps=steps;
    s.dt=kDt;
    s.material={kDensity,E,nu};
    s.initialDeformation=deformation;
    s.couplingNeighborCount=20;
    s.transferScheme=MpmTransferScheme::APIC;

    const auto g=grid(n,phase);
    std::array<std::vector<double>,kTimes.size()> samples;
    const auto result=vulkax::research::runNonlinearDeformableWorld(
        world(),{0,1,2,3},body(n),g,s,{},
        [&](const auto& frame,const GaussianCloud& cloud,const std::vector<MpmParticle>&) {
            for(std::size_t ti=0;ti<sampleSteps.size();++ti) {
                if(frame.step!=sampleSteps[ti]) continue;
                for(std::size_t gi=0;gi<4U;++gi) {
                    const auto& p=cloud.splats[gi].position;
                    samples[ti].push_back(p.x);
                    samples[ti].push_back(p.y);
                    samples[ti].push_back(p.z);
                }
            }
        });

    Trace t;
    t.n=n;t.phase=phase;t.origin=g.origin.x;t.cell=g.cellSize;
    for(const auto& sample:samples) {
        if(sample.size()!=12U) throw std::runtime_error("missing grid-phase observation sample");
        t.observations.insert(t.observations.end(),sample.begin(),sample.end());
    }
    t.effect=rms(t.observations,initialObs(deformation));
    t.energyDrift=result.maximumRelativeMechanicalEnergyDrift;
    t.minJ=result.minimumDeformationDeterminant;
    t.mlsRms=result.maximumMlsRmsResidual;
    return t;
}

struct Case { const char* name; double E; double nu; };

} // namespace

int main(int argc,char** argv) {
    const std::filesystem::path outDir=argc>1?argv[1]:"build/grid-phase-falsification";
    std::filesystem::create_directories(outDir);
    const Matrix3 target{1.111,.142,.057,.076,.889,.087,.091,.049,1.054};
    const std::array<Case,2> cases{{{"on_grid",15000.,.30},{"off_grid",15900.,.32}}};

    std::ofstream phaseCsv(outDir/"phase_sweep.csv");
    phaseCsv<<"case,n_axis,phase,grid_origin_m,grid_cell_m,observation_effect_rms_m,"
              "rms_to_phase_075_m,fraction_to_phase_075,max_relative_energy_drift,min_J,max_mls_rms\n";

    std::ofstream coreCsv(outDir/"phase_locked_corefinement.csv");
    coreCsv<<"case,n_axis,phase,grid_origin_m,grid_cell_m,observation_effect_rms_m,"
             "adjacent_rms_m,adjacent_fraction,rms_to_finest_m,fraction_to_finest,"
             "max_relative_energy_drift,min_J,max_mls_rms\n";

    for(const auto& c:cases) {
        std::vector<Trace> phaseTraces;
        for(const double phase:kPhases) {
            std::cout<<"GRID_PHASE_START "<<c.name<<" phase="<<phase<<"\n";
            phaseTraces.push_back(simulate(c.E,c.nu,target,4,phase));
        }
        const auto& baseline=phaseTraces.back();
        for(const auto& t:phaseTraces) {
            const double d=rms(t.observations,baseline.observations);
            const double frac=d/std::max(t.effect,1e-15);
            phaseCsv<<c.name<<','<<t.n<<','<<std::setprecision(17)<<t.phase<<','<<t.origin<<','
                    <<t.cell<<','<<t.effect<<','<<d<<','<<frac<<','<<t.energyDrift<<','
                    <<t.minJ<<','<<t.mlsRms<<"\n";
            std::cout<<"GRID_PHASE "<<c.name<<" phase="<<t.phase
                     <<" fraction_to_075="<<frac<<" origin="<<t.origin
                     <<" energy="<<t.energyDrift<<" minJ="<<t.minJ<<"\n";
        }

        std::vector<Trace> locked;
        for(const int n:kResolution) {
            std::cout<<"PHASE_LOCKED_COREFINE_START "<<c.name<<" n="<<n<<"\n";
            locked.push_back(simulate(c.E,c.nu,target,n,kLockedPhase));
        }
        const auto& finest=locked.back();
        for(std::size_t i=0;i<locked.size();++i) {
            const auto& t=locked[i];
            double adjacent=std::numeric_limits<double>::quiet_NaN();
            double adjacentFrac=std::numeric_limits<double>::quiet_NaN();
            if(i>0) {
                adjacent=rms(locked[i-1].observations,t.observations);
                adjacentFrac=adjacent/std::max(t.effect,1e-15);
            }
            const double toFinest=rms(t.observations,finest.observations);
            const double toFinestFrac=toFinest/std::max(t.effect,1e-15);
            coreCsv<<c.name<<','<<t.n<<','<<std::setprecision(17)<<t.phase<<','<<t.origin<<','
                   <<t.cell<<','<<t.effect<<','<<adjacent<<','<<adjacentFrac<<','<<toFinest<<','
                   <<toFinestFrac<<','<<t.energyDrift<<','<<t.minJ<<','<<t.mlsRms<<"\n";
            std::cout<<"PHASE_LOCKED_COREFINE "<<c.name<<" n="<<t.n
                     <<" adjacent_fraction="<<adjacentFrac
                     <<" to_finest_fraction="<<toFinestFrac
                     <<" origin="<<t.origin<<"\n";
        }
    }

    std::ofstream meta(outDir/"metadata.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.grid_phase_falsification\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic-label-free-fixed-physics-grid-phase-diagnostic\",\n"
        <<"  \"dt_s\": "<<std::setprecision(17)<<kDt<<",\n"
        <<"  \"phase_sweep\": [0.0,0.25,0.5,0.75],\n"
        <<"  \"locked_phase\": "<<kLockedPhase<<",\n"
        <<"  \"co_refinement_n_axis\": [4,5,6],\n"
        <<"  \"selection_labels_used\": false,\n"
        <<"  \"warning\": \"Grid phase is a numerical-coordinate diagnostic, not a physical parameter. No safety labels or inverse fit are used.\"\n"
        <<"}\n";
    return 0;
}
