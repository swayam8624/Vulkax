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

constexpr double kHorizon = 0.0064;
constexpr double kDt = 6.25e-6;
constexpr double kDomainMin = -0.8;
constexpr double kDomainMax = 0.96;
const std::array<double,4> kCells{0.10,0.08,0.06,0.05};
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

MpmGridSettings grid(double cell) {
    if(!(cell>0.0)) throw std::runtime_error("grid cell must be positive");
    MpmGridSettings g;
    g.origin={kDomainMin,kDomainMin,kDomainMin};
    const auto cells=static_cast<std::size_t>(std::ceil((kDomainMax-kDomainMin)/cell));
    g.nx=cells; g.ny=cells; g.nz=cells;
    g.cellSize=cell;
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

const char* transferName(MpmTransferScheme s) {
    switch(s) {
        case MpmTransferScheme::APIC: return "APIC";
        case MpmTransferScheme::PIC: return "PIC";
        case MpmTransferScheme::FLIP: return "FLIP";
    }
    return "UNKNOWN";
}

struct Trace {
    double cell{};
    MpmTransferScheme transfer{MpmTransferScheme::APIC};
    std::vector<double> observations;
    double effect{};
    double maxEnergyDrift{};
    double minJ{};
};

Trace simulate(double E,double nu,const Matrix3& deformation,double cell,MpmTransferScheme transfer) {
    const auto steps=static_cast<std::size_t>(std::llround(kHorizon/kDt));
    if(std::abs(static_cast<double>(steps)*kDt-kHorizon)>1e-12)
        throw std::runtime_error("fixed diagnostic dt must divide horizon");

    std::array<std::size_t,kTimes.size()> sampleSteps{};
    for(std::size_t i=0;i<kTimes.size();++i) {
        const double exact=kTimes[i]/kDt;
        sampleSteps[i]=static_cast<std::size_t>(std::llround(exact));
        if(std::abs(static_cast<double>(sampleSteps[i])*kDt-kTimes[i])>1e-12)
            throw std::runtime_error("observation time must lie on fixed timestep");
    }

    NonlinearDeformableWorldSettings s;
    s.steps=steps;
    s.dt=kDt;
    s.material={1000.,E,nu};
    s.initialDeformation=deformation;
    s.couplingNeighborCount=20;
    s.transferScheme=transfer;

    std::array<std::vector<double>,kTimes.size()> samples;
    const auto result=vulkax::research::runNonlinearDeformableWorld(
        world(),{0,1,2,3},body(),grid(cell),s,{},
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
    t.cell=cell;
    t.transfer=transfer;
    for(const auto& sample:samples) {
        if(sample.size()!=kMarkers.size()*3U)
            throw std::runtime_error("failed to capture space/transfer observation sample");
        t.observations.insert(t.observations.end(),sample.begin(),sample.end());
    }
    t.effect=rms(t.observations,initialObs(deformation));
    t.maxEnergyDrift=result.maximumRelativeMechanicalEnergyDrift;
    t.minJ=result.minimumDeformationDeterminant;
    return t;
}

struct Case { const char* name; double E; double nu; };

} // namespace

int main(int argc,char** argv) {
    const std::filesystem::path outDir=argc>1?argv[1]:"build/fixed-forward-space-transfer";
    std::filesystem::create_directories(outDir);

    const Matrix3 target{1.111,.142,.057,.076,.889,.087,.091,.049,1.054};
    const std::array<Case,2> cases{{{"on_grid",15000.,.30},{"off_grid",15900.,.32}}};
    const std::array<MpmTransferScheme,3> transfers{
        MpmTransferScheme::APIC,MpmTransferScheme::PIC,MpmTransferScheme::FLIP};

    std::ofstream csv(outDir/"levels.csv");
    csv<<"case,transfer,grid_cell_m,grid_n,dt_s,E_pa,nu,observation_effect_rms_m,"
          "adjacent_spatial_rms_m,adjacent_spatial_fraction,"
          "rms_to_finest_same_transfer_m,fraction_to_finest_same_transfer,"
          "rms_to_apic_same_grid_m,fraction_to_apic_same_grid,"
          "max_relative_energy_drift,min_J\n";

    for(const auto& c:cases) {
        std::array<std::vector<Trace>,3> traces;
        for(std::size_t si=0;si<transfers.size();++si) {
            for(const auto cell:kCells) {
                std::cout<<"SPACE_TRANSFER_START "<<c.name
                         <<" transfer="<<transferName(transfers[si])
                         <<" cell="<<std::setprecision(17)<<cell<<"\n";
                traces[si].push_back(simulate(c.E,c.nu,target,cell,transfers[si]));
            }
        }

        for(std::size_t si=0;si<transfers.size();++si) {
            const auto& same=traces[si];
            const auto& finest=same.back();
            for(std::size_t gi=0;gi<same.size();++gi) {
                const auto& t=same[gi];
                double adjacent=std::numeric_limits<double>::quiet_NaN();
                double adjacentFrac=std::numeric_limits<double>::quiet_NaN();
                if(gi>0) {
                    adjacent=rms(same[gi-1].observations,t.observations);
                    adjacentFrac=adjacent/std::max(t.effect,1e-15);
                }
                const double toFinest=rms(t.observations,finest.observations);
                const double toFinestFrac=toFinest/std::max(t.effect,1e-15);
                const auto& apic=traces[0][gi];
                const double toApic=rms(t.observations,apic.observations);
                const double toApicFrac=toApic/std::max(t.effect,1e-15);
                const auto gridN=static_cast<std::size_t>(
                    std::ceil((kDomainMax-kDomainMin)/t.cell));

                csv<<c.name<<','<<transferName(t.transfer)<<','<<std::setprecision(17)
                   <<t.cell<<','<<gridN<<','<<kDt<<','<<c.E<<','<<c.nu<<','<<t.effect<<','
                   <<adjacent<<','<<adjacentFrac<<','<<toFinest<<','<<toFinestFrac<<','
                   <<toApic<<','<<toApicFrac<<','<<t.maxEnergyDrift<<','<<t.minJ<<"\n";

                std::cout<<"SPACE_TRANSFER "<<c.name
                         <<" transfer="<<transferName(t.transfer)
                         <<" cell="<<t.cell
                         <<" adjacent_fraction="<<adjacentFrac
                         <<" to_finest_fraction="<<toFinestFrac
                         <<" to_apic_fraction="<<toApicFrac
                         <<" energy_drift="<<t.maxEnergyDrift
                         <<" minJ="<<t.minJ<<"\n";
            }
        }
    }

    std::ofstream meta(outDir/"metadata.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.fixed_forward_space_transfer\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic-label-free-fixed-physics-space-transfer-diagnostic\",\n"
        <<"  \"dt_s\": "<<std::setprecision(17)<<kDt<<",\n"
        <<"  \"grid_cells_m\": [0.10,0.08,0.06,0.05],\n"
        <<"  \"transfers\": [\"APIC\",\"PIC\",\"FLIP\"],\n"
        <<"  \"selection_labels_used\": false,\n"
        <<"  \"warning\": \"No inverse fit or safety label is used. Finest-grid APIC is a discrete comparator only, not continuum truth.\"\n"
        <<"}\n";
    return 0;
}
