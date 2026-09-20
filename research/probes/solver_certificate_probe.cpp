#include "vulkax/research/nonlinear_deformable_world.hpp"

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
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using vulkax::gaussian::GaussianCloud;
using vulkax::math::Vec3;
using vulkax::research::NonlinearDeformableWorldSettings;
using vulkax::solvers::Matrix3;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmParticle;
using vulkax::solvers::MpmTransferScheme;

constexpr double kTruthDt=1.0e-4;
constexpr double kHorizon=0.008;
constexpr double kNoise=2.0e-5;
const std::array<double,5> kTimes{0.0016,0.0032,0.0048,0.0064,0.0080};
const std::array<std::size_t,2> kMarkers{15U,63U};

std::vector<MpmParticle> makeBody(){
    std::vector<MpmParticle> ps; std::uint64_t id=1;
    constexpr double h=0.12, vol=h*h*h, rho=1000.0;
    for(int z=0;z<4;++z) for(int y=0;y<4;++y) for(int x=0;x<4;++x){
        MpmParticle p; p.id=id++;
        p.restPosition={(static_cast<double>(x)-1.5)*h,(static_cast<double>(y)-1.5)*h,(static_cast<double>(z)-1.5)*h};
        p.position=p.restPosition; p.restVolume=vol; p.mass=rho*vol; ps.push_back(p);
    }
    return ps;
}
vulkax::gaussian::GaussianSplat splat(Vec3 p){
    vulkax::gaussian::GaussianSplat s; s.position=p;
    s.logScale={std::log(0.06),std::log(0.045),std::log(0.035)};
    s.rotation={1.0,0.0,0.0,0.0}; s.opacityLogit=4.0; return s;
}
GaussianCloud makeWorld(){
    GaussianCloud w;
    w.splats.push_back(splat({-0.08,0.04,0.02}));
    w.splats.push_back(splat({0.09,-0.06,0.03}));
    w.splats.push_back(splat({0.04,0.10,-0.07}));
    w.splats.push_back(splat({-0.05,-0.08,-0.06}));
    w.splats.push_back(splat({0.76,0.55,-0.30}));
    return w;
}
MpmGridSettings makeGrid(){
    MpmGridSettings g; g.origin={-0.80,-0.80,-0.80};
    g.nx=22; g.ny=22; g.nz=22; g.cellSize=0.08; g.boundaryCells=0; return g;
}
Vec3 mul(const Matrix3&m,Vec3 v){
    return {m[0]*v.x+m[1]*v.y+m[2]*v.z,m[3]*v.x+m[4]*v.y+m[5]*v.z,m[6]*v.x+m[7]*v.y+m[8]*v.z};
}
struct Trace{
    double dt{};
    std::vector<std::vector<Vec3>> positions;
    double maxEnergyDrift{};
    double minJ{1.0};
};
Trace simulate(double E,double nu,const Matrix3&def,double dt,MpmTransferScheme scheme){
    const std::size_t steps=static_cast<std::size_t>(std::llround(kHorizon/dt));
    if(std::abs(static_cast<double>(steps)*dt-kHorizon)>1.0e-12) throw std::runtime_error("dt must divide horizon");
    NonlinearDeformableWorldSettings s; s.steps=steps; s.dt=dt; s.material={1000.0,E,nu};
    s.initialDeformation=def; s.couplingNeighborCount=20; s.transferScheme=scheme;
    Trace t; t.dt=dt; t.positions.resize(steps+1U);
    const auto body=makeBody(); t.positions.front().reserve(body.size());
    for(const auto&p:body) t.positions.front().push_back(mul(def,p.restPosition));
    const auto result=vulkax::research::runNonlinearDeformableWorld(
        makeWorld(),{0,1,2,3},makeBody(),makeGrid(),s,{},
        [&](const auto&frame,const GaussianCloud&,const std::vector<MpmParticle>&particles){
            auto&dst=t.positions.at(frame.step); dst.reserve(particles.size());
            for(const auto&p:particles) dst.push_back(p.position);
        });
    t.maxEnergyDrift=result.maximumRelativeMechanicalEnergyDrift;
    t.minJ=result.minimumDeformationDeterminant;
    return t;
}
std::vector<double> observe(const Trace&t,std::size_t a,std::size_t b){
    std::vector<double> y;
    for(std::size_t ti=a;ti<b;++ti){
        const auto step=static_cast<std::size_t>(std::llround(kTimes[ti]/t.dt));
        for(const auto m:kMarkers){
            const auto&p=t.positions.at(step).at(m);
            y.push_back(p.x); y.push_back(p.y); y.push_back(p.z);
        }
    }
    return y;
}
std::vector<double> noisy(const std::vector<double>&truth,int salt){
    auto y=truth;
    for(std::size_t i=0;i<y.size();++i){
        const double q=0.731*static_cast<double>(i+1U)+1.173*static_cast<double>(salt+1);
        y[i]+=kNoise*(0.67*std::sin(q)+0.33*std::cos(1.913*q));
    }
    return y;
}
double rms(const std::vector<double>&a,const std::vector<double>&b){
    if(a.size()!=b.size()||a.empty()) throw std::runtime_error("invalid RMS");
    double s=0.0; for(std::size_t i=0;i<a.size();++i){const double d=a[i]-b[i];s+=d*d;}
    return std::sqrt(s/static_cast<double>(a.size()));
}
double defDistance(const Matrix3&a,const Matrix3&b){
    double s=0.0; for(std::size_t i=0;i<a.size();++i){const double d=a[i]-b[i];s+=d*d;} return std::sqrt(s);
}
std::vector<double> initialObservable(const Matrix3&def,std::size_t a,std::size_t b){
    const auto body=makeBody(); std::vector<double> y;
    for(std::size_t ti=a;ti<b;++ti){(void)ti;for(const auto m:kMarkers){
        const auto p=mul(def,body.at(m).restPosition); y.push_back(p.x);y.push_back(p.y);y.push_back(p.z);
    }}
    return y;
}
struct Spectrum{double smin{},smax{},condition{};};
Spectrum spectrum(double E,double nu,const Matrix3&def,double dt,MpmTransferScheme scheme){
    constexpr double re=0.01,dn=0.01;
    const auto ep=observe(simulate(E*(1.0+re),nu,def,dt,scheme),0,5);
    const auto em=observe(simulate(E*(1.0-re),nu,def,dt,scheme),0,5);
    const auto np=observe(simulate(E,nu+dn,def,dt,scheme),0,5);
    const auto nm=observe(simulate(E,nu-dn,def,dt,scheme),0,5);
    double aa=0.0,ab=0.0,bb=0.0;
    for(std::size_t i=0;i<ep.size();++i){
        const double x=(ep[i]-em[i])/(2.0*re);
        const double z=((np[i]-nm[i])/(2.0*dn))*0.1;
        aa+=x*x;ab+=x*z;bb+=z*z;
    }
    const double tr=aa+bb,disc=std::sqrt(std::max(0.0,(aa-bb)*(aa-bb)+4.0*ab*ab));
    const double lmax=0.5*(tr+disc),lmin=std::max(0.0,0.5*(tr-disc));
    Spectrum s; s.smax=std::sqrt(lmax);s.smin=std::sqrt(lmin);
    s.condition=s.smin>1e-18?s.smax/s.smin:1.0e30; return s;
}
enum class Variant{FineAPIC,CoarseAPIC,PIC,ConstrainedNu};
std::string vname(Variant v){
    switch(v){
        case Variant::FineAPIC:return "fine_apic";
        case Variant::CoarseAPIC:return "coarse_apic";
        case Variant::PIC:return "pic";
        case Variant::ConstrainedNu:return "constrained_nu";
    } return "unknown";
}
struct Fit{
    double E{},nu{},dt{},fitRms{},heldoutRms{};
    MpmTransferScheme scheme{MpmTransferScheme::APIC};
    Variant variant{Variant::FineAPIC};
};
Fit fitModel(Variant v,const Matrix3&def,const std::vector<double>&fitObs,const std::vector<double>&heldObs){
    Fit best;best.variant=v;best.dt=v==Variant::CoarseAPIC?4e-4:kTruthDt;
    best.scheme=v==Variant::PIC?MpmTransferScheme::PIC:MpmTransferScheme::APIC;
    best.fitRms=std::numeric_limits<double>::infinity();
    const std::array<double,5> Es{12000,13500,15000,16500,18000};
    const std::array<double,5> Nus{0.20,0.25,0.30,0.35,0.40};
    for(const auto E:Es){
        if(v==Variant::ConstrainedNu){
            constexpr double nu=0.10;
            const auto t=simulate(E,nu,def,best.dt,best.scheme);
            const double loss=rms(observe(t,0,2),fitObs);
            if(loss<best.fitRms){best.E=E;best.nu=nu;best.fitRms=loss;best.heldoutRms=rms(observe(t,2,5),heldObs);}
        }else{
            for(const auto nu:Nus){
                const auto t=simulate(E,nu,def,best.dt,best.scheme);
                const double loss=rms(observe(t,0,2),fitObs);
                if(loss<best.fitRms){best.E=E;best.nu=nu;best.fitRms=loss;best.heldoutRms=rms(observe(t,2,5),heldObs);}
            }
        }
    }
    return best;
}
struct Intervention{std::string name;Matrix3 def;};

} // namespace

int main(int argc,char**argv){
    const std::filesystem::path dir=argc>1?std::filesystem::path(argv[1]):"build/solver-certificate";
    std::filesystem::create_directories(dir);
    std::ofstream out(dir/"cases.csv");
    out<<"case_id,truth_id,variant,intervention,truth_E_pa,truth_nu,fitted_E_pa,fitted_nu,"
          "fit_rms_m,heldout_rms_m,sensitivity_smin,condition_number,intervention_distance,"
          "numerical_disagreement_m,scheme_disagreement_m,candidate_effect_m,numerical_fraction,"
          "scheme_fraction,counterfactual_rms_m,counterfactual_relative_error,unsafe_10pct,provenance\n";
    const Matrix3 train{1.030,0.008,0, 0,0.985,0.003, 0,0,0.985};
    const std::array<Intervention,3> interventions{{
        {"mild_shear",{1.025,0.035,0.005,0.005,0.980,0.020,0,0.008,0.995}},
        {"mixed",{1.060,0.060,0.010,0.010,0.950,0.050,0,0.020,1.015}},
        {"strong_mixed",{1.100,0.100,0.025,0.025,0.915,0.085,0.005,0.040,1.030}}
    }};
    const std::array<double,3> Es{12000,15000,18000},Nus{0.20,0.35,0.40};
    const std::array<Variant,4> vars{Variant::FineAPIC,Variant::CoarseAPIC,Variant::PIC,Variant::ConstrainedNu};
    int truthId=0,caseId=0;
    for(const auto truthE:Es)for(const auto truthNu:Nus){
        ++truthId;
        const auto truthTrain=simulate(truthE,truthNu,train,kTruthDt,MpmTransferScheme::APIC);
        const auto fitObs=noisy(observe(truthTrain,0,2),truthId*17+1);
        const auto heldObs=noisy(observe(truthTrain,2,5),truthId*17+2);
        for(const auto v:vars){
            const auto fit=fitModel(v,train,fitObs,heldObs);
            const auto sp=spectrum(fit.E,fit.nu,train,fit.dt,fit.scheme);
            for(const auto&iv:interventions){
                ++caseId;
                const auto truthCf=simulate(truthE,truthNu,iv.def,kTruthDt,MpmTransferScheme::APIC);
                const auto cand=simulate(fit.E,fit.nu,iv.def,fit.dt,fit.scheme);
                const auto truthY=observe(truthCf,0,5),candY=observe(cand,0,5);
                const double cf=rms(candY,truthY);
                const double truthEffect=rms(truthY,initialObservable(iv.def,0,5));
                const double rel=cf/std::max(truthEffect,1e-15);
                const auto refined=simulate(fit.E,fit.nu,iv.def,fit.dt*0.5,fit.scheme);
                const double numerical=rms(candY,observe(refined,0,5));
                const auto alt=fit.scheme==MpmTransferScheme::PIC?MpmTransferScheme::APIC:MpmTransferScheme::PIC;
                const auto altTrace=simulate(fit.E,fit.nu,iv.def,fit.dt,alt);
                const double scheme=rms(candY,observe(altTrace,0,5));
                const double candEffect=rms(candY,initialObservable(iv.def,0,5));
                out<<caseId<<','<<truthId<<','<<vname(v)<<','<<iv.name<<','<<std::setprecision(17)
                   <<truthE<<','<<truthNu<<','<<fit.E<<','<<fit.nu<<','<<fit.fitRms<<','<<fit.heldoutRms<<','
                   <<sp.smin<<','<<sp.condition<<','<<defDistance(train,iv.def)<<','<<numerical<<','<<scheme<<','
                   <<candEffect<<','<<numerical/std::max(candEffect,1e-15)<<','
                   <<scheme/std::max(candEffect,1e-15)<<','<<cf<<','<<rel<<','<<(rel>0.10?1:0)<<",synthetic\n";
            }
        }
    }
    out.close();
    std::ofstream meta(dir/"summary.json");
    meta<<"{\n  \"schema\": \"vulkax.counterfactual_certificate_dataset\",\n  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic\",\n  \"case_count\": "<<caseId<<",\n"
        <<"  \"unsafe_definition\": \"counterfactual RMS > 10% of truth dynamic response amplitude\",\n"
        <<"  \"warning\": \"Exploratory solver-integrated certificate dataset; not publication evidence\"\n}\n";
    std::cout<<"generated_cases="<<caseId<<"\n";
}
