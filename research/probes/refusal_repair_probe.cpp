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

constexpr double kTruthDt = 1.0e-4;
constexpr double kHorizon = 0.008;
constexpr double kNoise = 2.0e-5;
const std::array<double, 5> kTimes{0.0016,0.0032,0.0048,0.0064,0.0080};
const std::array<std::size_t, 2> kMarkers{15U,63U};

std::vector<MpmParticle> makeBody() {
    std::vector<MpmParticle> ps;
    std::uint64_t id=1;
    constexpr double h=0.12, volume=h*h*h, density=1000.0;
    for(int z=0; z<4; ++z)
        for(int y=0; y<4; ++y)
            for(int x=0; x<4; ++x) {
                MpmParticle p;
                p.id=id++;
                p.restPosition={(static_cast<double>(x)-1.5)*h,
                                (static_cast<double>(y)-1.5)*h,
                                (static_cast<double>(z)-1.5)*h};
                p.position=p.restPosition;
                p.restVolume=volume;
                p.mass=density*volume;
                ps.push_back(p);
            }
    return ps;
}

vulkax::gaussian::GaussianSplat splat(Vec3 p) {
    vulkax::gaussian::GaussianSplat s;
    s.position=p;
    s.logScale={std::log(0.06),std::log(0.045),std::log(0.035)};
    s.rotation={1.0,0.0,0.0,0.0};
    s.opacityLogit=4.0;
    return s;
}

GaussianCloud makeWorld() {
    GaussianCloud w;
    w.splats.push_back(splat({-0.08,0.04,0.02}));
    w.splats.push_back(splat({0.09,-0.06,0.03}));
    w.splats.push_back(splat({0.04,0.10,-0.07}));
    w.splats.push_back(splat({-0.05,-0.08,-0.06}));
    w.splats.push_back(splat({0.76,0.55,-0.30}));
    return w;
}

MpmGridSettings makeGrid() {
    MpmGridSettings g;
    g.origin={-0.80,-0.80,-0.80};
    g.nx=22; g.ny=22; g.nz=22;
    g.cellSize=0.08;
    g.boundaryCells=0;
    return g;
}

Vec3 mul(const Matrix3& m, Vec3 v) {
    return {
        m[0]*v.x+m[1]*v.y+m[2]*v.z,
        m[3]*v.x+m[4]*v.y+m[5]*v.z,
        m[6]*v.x+m[7]*v.y+m[8]*v.z
    };
}

struct Trace {
    double dt{};
    std::vector<std::vector<Vec3>> positions;
};

Trace simulate(double E,double nu,const Matrix3& def,double dt,MpmTransferScheme scheme) {
    const std::size_t steps=static_cast<std::size_t>(std::llround(kHorizon/dt));
    if(std::abs(static_cast<double>(steps)*dt-kHorizon)>1.0e-12)
        throw std::runtime_error("dt must divide horizon");

    NonlinearDeformableWorldSettings s;
    s.steps=steps;
    s.dt=dt;
    s.material={1000.0,E,nu};
    s.initialDeformation=def;
    s.couplingNeighborCount=20;
    s.transferScheme=scheme;

    Trace t;
    t.dt=dt;
    t.positions.resize(steps+1U);
    const auto body=makeBody();
    t.positions.front().reserve(body.size());
    for(const auto& p:body) t.positions.front().push_back(mul(def,p.restPosition));

    (void)vulkax::research::runNonlinearDeformableWorld(
        makeWorld(),{0,1,2,3},makeBody(),makeGrid(),s,{},
        [&](const auto& frame,const GaussianCloud&,const std::vector<MpmParticle>& particles) {
            auto& dst=t.positions.at(frame.step);
            dst.reserve(particles.size());
            for(const auto& p:particles) dst.push_back(p.position);
        });
    return t;
}

std::vector<double> observe(const Trace& t,std::size_t first,std::size_t last) {
    std::vector<double> y;
    for(std::size_t ti=first; ti<last; ++ti) {
        const auto step=static_cast<std::size_t>(std::llround(kTimes[ti]/t.dt));
        for(const auto marker:kMarkers) {
            const auto& p=t.positions.at(step).at(marker);
            y.push_back(p.x); y.push_back(p.y); y.push_back(p.z);
        }
    }
    return y;
}

std::vector<double> noisy(const std::vector<double>& truth,int salt) {
    auto y=truth;
    for(std::size_t i=0;i<y.size();++i) {
        const double q=0.733*static_cast<double>(i+1U)+1.119*static_cast<double>(salt+1);
        y[i]+=kNoise*(0.65*std::sin(q)+0.35*std::cos(1.879*q));
    }
    return y;
}

double rms(const std::vector<double>& a,const std::vector<double>& b) {
    if(a.size()!=b.size()||a.empty()) throw std::runtime_error("invalid RMS");
    double s=0.0;
    for(std::size_t i=0;i<a.size();++i) {
        const double d=a[i]-b[i];
        s+=d*d;
    }
    return std::sqrt(s/static_cast<double>(a.size()));
}

std::vector<double> initialObservable(const Matrix3& def,std::size_t first,std::size_t last) {
    const auto body=makeBody();
    std::vector<double> y;
    for(std::size_t ti=first; ti<last; ++ti) {
        (void)ti;
        for(const auto marker:kMarkers) {
            const auto p=mul(def,body.at(marker).restPosition);
            y.push_back(p.x); y.push_back(p.y); y.push_back(p.z);
        }
    }
    return y;
}

struct Spectrum { double smin{}, smax{}, condition{}; };

Spectrum spectrum(double E,double nu,const Matrix3& def,double dt,MpmTransferScheme scheme) {
    constexpr double relE=0.01, dNu=0.01;
    const auto ep=observe(simulate(E*(1.0+relE),nu,def,dt,scheme),0,5);
    const auto em=observe(simulate(E*(1.0-relE),nu,def,dt,scheme),0,5);
    const auto np=observe(simulate(E,nu+dNu,def,dt,scheme),0,5);
    const auto nm=observe(simulate(E,nu-dNu,def,dt,scheme),0,5);

    double aa=0.0,ab=0.0,bb=0.0;
    for(std::size_t i=0;i<ep.size();++i) {
        const double a=(ep[i]-em[i])/(2.0*relE);
        const double b=((np[i]-nm[i])/(2.0*dNu))*0.1;
        aa+=a*a; ab+=a*b; bb+=b*b;
    }
    const double tr=aa+bb;
    const double disc=std::sqrt(std::max(0.0,(aa-bb)*(aa-bb)+4.0*ab*ab));
    const double lmax=0.5*(tr+disc);
    const double lmin=std::max(0.0,0.5*(tr-disc));
    Spectrum s;
    s.smax=std::sqrt(lmax);
    s.smin=std::sqrt(lmin);
    s.condition=s.smin>1.0e-18?s.smax/s.smin:1.0e30;
    return s;
}

enum class Variant { FineAPIC, CoarseAPIC, PIC, ConstrainedNu };

std::string name(Variant v) {
    switch(v) {
        case Variant::FineAPIC:return "fine_apic";
        case Variant::CoarseAPIC:return "coarse_apic";
        case Variant::PIC:return "pic";
        case Variant::ConstrainedNu:return "constrained_nu";
    }
    return "unknown";
}

struct Fit {
    double E{},nu{},dt{},fitRms{},heldoutRms{},evidenceRms{},objective{};
    MpmTransferScheme scheme{MpmTransferScheme::APIC};
    Variant variant{Variant::FineAPIC};
};

std::array<double,5> eGrid(){ return {12000.0,13500.0,15000.0,16500.0,18000.0}; }
std::array<double,5> nuGrid(){ return {0.20,0.25,0.30,0.35,0.40}; }

Fit initialFit(Variant v,const Matrix3& train,
               const std::vector<double>& fitObs,const std::vector<double>& heldObs) {
    Fit best;
    best.variant=v;
    best.dt=v==Variant::CoarseAPIC?4.0e-4:kTruthDt;
    best.scheme=v==Variant::PIC?MpmTransferScheme::PIC:MpmTransferScheme::APIC;
    best.objective=std::numeric_limits<double>::infinity();

    for(const auto E:eGrid()) {
        if(v==Variant::ConstrainedNu) {
            constexpr double nu=0.10;
            const auto tr=simulate(E,nu,train,best.dt,best.scheme);
            const double fit=rms(observe(tr,0,2),fitObs);
            if(fit<best.objective) {
                best.E=E;best.nu=nu;best.fitRms=fit;best.objective=fit;
                best.heldoutRms=rms(observe(tr,2,5),heldObs);
            }
        } else {
            for(const auto nu:nuGrid()) {
                const auto tr=simulate(E,nu,train,best.dt,best.scheme);
                const double fit=rms(observe(tr,0,2),fitObs);
                if(fit<best.objective) {
                    best.E=E;best.nu=nu;best.fitRms=fit;best.objective=fit;
                    best.heldoutRms=rms(observe(tr,2,5),heldObs);
                }
            }
        }
    }
    return best;
}

Fit repairedFit(Variant v,const Matrix3& train,const Matrix3& evidenceDef,
                const std::vector<double>& fitObs,const std::vector<double>& heldObs,
                const std::vector<double>& evidenceObs) {
    Fit best;
    best.variant=v;
    best.dt=v==Variant::CoarseAPIC?4.0e-4:kTruthDt;
    best.scheme=v==Variant::PIC?MpmTransferScheme::PIC:MpmTransferScheme::APIC;
    best.objective=std::numeric_limits<double>::infinity();

    for(const auto E:eGrid()) {
        if(v==Variant::ConstrainedNu) {
            constexpr double nu=0.10;
            const auto trainTrace=simulate(E,nu,train,best.dt,best.scheme);
            const auto evidenceTrace=simulate(E,nu,evidenceDef,best.dt,best.scheme);
            const double fit=rms(observe(trainTrace,0,2),fitObs);
            const double ev=rms(observe(evidenceTrace,0,5),evidenceObs);
            const double objective=std::sqrt(0.5*(fit*fit+ev*ev));
            if(objective<best.objective) {
                best={E,nu,best.dt,fit,rms(observe(trainTrace,2,5),heldObs),ev,objective,best.scheme,v};
            }
        } else {
            for(const auto nu:nuGrid()) {
                const auto trainTrace=simulate(E,nu,train,best.dt,best.scheme);
                const auto evidenceTrace=simulate(E,nu,evidenceDef,best.dt,best.scheme);
                const double fit=rms(observe(trainTrace,0,2),fitObs);
                const double ev=rms(observe(evidenceTrace,0,5),evidenceObs);
                const double objective=std::sqrt(0.5*(fit*fit+ev*ev));
                if(objective<best.objective) {
                    best={E,nu,best.dt,fit,rms(observe(trainTrace,2,5),heldObs),ev,objective,best.scheme,v};
                }
            }
        }
    }
    return best;
}

struct Intervention { std::string name; Matrix3 def; };

double targetRelativeError(const Fit& fit,const Matrix3& target,double truthE,double truthNu) {
    const auto truth=observe(simulate(truthE,truthNu,target,kTruthDt,MpmTransferScheme::APIC),0,5);
    const auto pred=observe(simulate(fit.E,fit.nu,target,fit.dt,fit.scheme),0,5);
    const double effect=rms(truth,initialObservable(target,0,5));
    return rms(pred,truth)/std::max(effect,1.0e-15);
}

double numericalFraction(const Fit& fit,const Matrix3& target) {
    const auto base=observe(simulate(fit.E,fit.nu,target,fit.dt,fit.scheme),0,5);
    const auto fine=observe(simulate(fit.E,fit.nu,target,0.5*fit.dt,fit.scheme),0,5);
    const double effect=rms(base,initialObservable(target,0,5));
    return rms(base,fine)/std::max(effect,1.0e-15);
}

double schemeFraction(const Fit& fit,const Matrix3& target) {
    const auto base=observe(simulate(fit.E,fit.nu,target,fit.dt,fit.scheme),0,5);
    const auto altScheme=fit.scheme==MpmTransferScheme::PIC?MpmTransferScheme::APIC:MpmTransferScheme::PIC;
    const auto alt=observe(simulate(fit.E,fit.nu,target,fit.dt,altScheme),0,5);
    const double effect=rms(base,initialObservable(target,0,5));
    return rms(base,alt)/std::max(effect,1.0e-15);
}

} // namespace

int main(int argc,char** argv) {
    const std::filesystem::path dir=argc>1?std::filesystem::path(argv[1]):"build/refusal-repair";
    std::filesystem::create_directories(dir);
    std::ofstream out(dir/"cases.csv");
    out<<"case_id,truth_id,variant,selected_evidence,truth_E_pa,truth_nu,"
          "initial_E_pa,initial_nu,repaired_E_pa,repaired_nu,"
          "initial_fit_rms_m,initial_heldout_rms_m,repaired_fit_rms_m,repaired_heldout_rms_m,"
          "evidence_rms_m,evidence_noise_ratio,selected_smin,selected_condition,"
          "relative_E_shift,absolute_nu_shift,pre_target_relative_error,post_target_relative_error,"
          "numerical_fraction,scheme_fraction,post_safe_10pct,provenance\n";

    const Matrix3 train{1.030,0.008,0, 0,0.985,0.003, 0,0,0.985};
    const std::array<Intervention,4> evidenceCandidates{{
        {"xy_shear",{1.0,0.09,0, 0.02,1.0,0, 0,0,1.0}},
        {"xz_shear",{1.0,0,0.09, 0,1.0,0, 0.02,0,1.0}},
        {"mixed",{1.060,0.060,0.010, 0.010,0.950,0.050, 0,0.020,1.015}},
        {"off_axis",{1.045,0.030,0.055, 0.015,0.970,0.020, 0.025,0.010,1.005}}
    }};
    const Matrix3 target{1.080,0.020,0.085, 0.070,0.940,0.025, 0.010,0.060,1.020};
    const std::array<double,3> truthEs{12000.0,15000.0,18000.0};
    const std::array<double,3> truthNus{0.20,0.35,0.40};
    const std::array<Variant,4> variants{
        Variant::FineAPIC,Variant::CoarseAPIC,Variant::PIC,Variant::ConstrainedNu
    };

    int truthId=0,caseId=0;
    for(const auto truthE:truthEs) for(const auto truthNu:truthNus) {
        ++truthId;
        const auto truthTrain=simulate(truthE,truthNu,train,kTruthDt,MpmTransferScheme::APIC);
        const auto fitObs=noisy(observe(truthTrain,0,2),truthId*31+1);
        const auto heldObs=noisy(observe(truthTrain,2,5),truthId*31+2);

        for(const auto variant:variants) {
            const auto before=initialFit(variant,train,fitObs,heldObs);

            const Intervention* selected=&evidenceCandidates.front();
            Spectrum selectedSpectrum{};
            double selectedSmin=-1.0;
            for(const auto& candidate:evidenceCandidates) {
                const auto sp=spectrum(before.E,before.nu,candidate.def,before.dt,before.scheme);
                if(sp.smin>selectedSmin) {
                    selectedSmin=sp.smin;
                    selected=&candidate;
                    selectedSpectrum=sp;
                }
            }

            const auto truthEvidence=simulate(truthE,truthNu,selected->def,kTruthDt,MpmTransferScheme::APIC);
            const auto evidenceObs=noisy(observe(truthEvidence,0,5),truthId*101+static_cast<int>(variant)+7);
            const auto after=repairedFit(variant,train,selected->def,fitObs,heldObs,evidenceObs);

            const double preTarget=targetRelativeError(before,target,truthE,truthNu);
            const double postTarget=targetRelativeError(after,target,truthE,truthNu);
            const double numFrac=numericalFraction(after,target);
            const double schFrac=schemeFraction(after,target);
            const double eShift=std::abs(after.E-before.E)/std::max(std::abs(before.E),1.0);
            const double nuShift=std::abs(after.nu-before.nu);

            ++caseId;
            out<<caseId<<','<<truthId<<','<<name(variant)<<','<<selected->name<<','<<std::setprecision(17)
               <<truthE<<','<<truthNu<<','<<before.E<<','<<before.nu<<','<<after.E<<','<<after.nu<<','
               <<before.fitRms<<','<<before.heldoutRms<<','<<after.fitRms<<','<<after.heldoutRms<<','
               <<after.evidenceRms<<','<<after.evidenceRms/kNoise<<','
               <<selectedSpectrum.smin<<','<<selectedSpectrum.condition<<','
               <<eShift<<','<<nuShift<<','<<preTarget<<','<<postTarget<<','
               <<numFrac<<','<<schFrac<<','<<(postTarget<=0.10?1:0)<<",synthetic\n";
        }
    }
    out.close();

    std::ofstream meta(dir/"summary.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.refusal_repair_probe\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic\",\n"
        <<"  \"case_count\": "<<caseId<<",\n"
        <<"  \"measurement_noise_m\": "<<std::setprecision(17)<<kNoise<<",\n"
        <<"  \"target_is_disjoint_from_requested_evidence\": true,\n"
        <<"  \"warning\": \"Synthetic closed-loop diagnostic, not real-world or novelty evidence\"\n"
        <<"}\n";
    std::cout<<"generated_refusal_repair_cases="<<caseId<<"\n";
    return 0;
}
