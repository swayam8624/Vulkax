#include "vulkax/research/dark_field_counterfactual.hpp"
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
#include <string>
#include <utility>
#include <vector>

namespace {
using vulkax::gaussian::GaussianCloud;
using vulkax::math::Vec3;
using vulkax::research::NonlinearDeformableWorldSettings;
using vulkax::research::dcs::AnnihilatingStencil;
using vulkax::research::dcs::InterventionPoint;
using vulkax::research::dcs::Response;
using vulkax::research::dcs::UncertaintyBudget;
using vulkax::solvers::Matrix3;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmParticle;
using vulkax::solvers::MpmTransferScheme;

constexpr double kHorizon=0.0032;
constexpr double kTruthDt=2.5e-5;
constexpr double kCandidateDt=1.0e-4;
constexpr double kCoarseDt=4.0e-4;
constexpr double kCalibrationAmplitude=0.025;
constexpr double kHeldoutAmplitude=0.040;
constexpr double kProbeAmplitude=0.060;
constexpr double kTargetShear=0.095;
constexpr double kTargetAxial=0.075;
constexpr double kNoise=2.0e-5;
constexpr double kDecisionZ=2.0;

struct Variant {
    std::string name;
    MpmTransferScheme scheme{MpmTransferScheme::APIC};
    double dt{kCandidateDt};
    bool constrainNu{};
};
struct Fit { double E{},nu{},objective{std::numeric_limits<double>::infinity()}; };
struct Candidate {
    Variant variant;
    Fit fit;
    std::vector<Response> heldoutResponses;
    double heldoutError{};
    std::vector<Response> probeResponses;
    std::vector<Response> refinedProbeResponses;
    Response targetResponse;
    double targetError{};
};

std::vector<MpmParticle> body(){
    std::vector<MpmParticle> ps; std::uint64_t id=1;
    constexpr double h=.12,v=h*h*h,rho=1000.0;
    for(int z=0;z<4;++z)for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        MpmParticle p; p.id=id++; p.restPosition={(x-1.5)*h,(y-1.5)*h,(z-1.5)*h};
        p.position=p.restPosition;p.restVolume=v;p.mass=rho*v;ps.push_back(p);
    }
    return ps;
}
vulkax::gaussian::GaussianSplat splat(Vec3 p){
    vulkax::gaussian::GaussianSplat s;s.position=p;
    s.logScale={std::log(.06),std::log(.045),std::log(.035)};
    s.rotation={1.,0.,0.,0.};s.opacityLogit=4.;return s;
}
GaussianCloud world(){
    GaussianCloud w;
    w.splats.push_back(splat({-.08,.04,.02}));
    w.splats.push_back(splat({.09,-.06,.03}));
    w.splats.push_back(splat({.04,.10,-.07}));
    w.splats.push_back(splat({-.05,-.08,-.06}));
    return w;
}
MpmGridSettings grid(){
    MpmGridSettings g;g.origin={-.8,-.8,-.8};g.nx=22;g.ny=22;g.nz=22;
    g.cellSize=.08;g.boundaryCells=0;return g;
}
Matrix3 deformation(double shear,double axial){
    return {1.+axial,shear,0.,0.,1.,0.,0.,0.,1.};
}
Response simulate(double E,double nu,double shear,double axial,MpmTransferScheme scheme,double dt){
    if(std::abs(shear)<1e-15 && std::abs(axial)<1e-15){
        const auto ps=body();
        constexpr std::array<std::size_t,4> markers{5U,18U,45U,58U};
        Response out;out.reserve(12);
        for(auto m:markers){
            const auto&p=ps.at(m).restPosition;
            out.push_back(p.x);out.push_back(p.y);out.push_back(p.z);
        }
        return out;
    }
    NonlinearDeformableWorldSettings s;
    s.steps=static_cast<std::size_t>(std::llround(kHorizon/dt));
    s.dt=dt;s.material={1000.,E,nu};s.initialDeformation=deformation(shear,axial);
    s.couplingNeighborCount=20;s.transferScheme=scheme;
    std::vector<MpmParticle> finalParticles;
    (void)vulkax::research::runNonlinearDeformableWorld(
        world(),{0,1,2,3},body(),grid(),s,{},
        [&](const auto&frame,const GaussianCloud&,const std::vector<MpmParticle>&particles){
            if(frame.step==s.steps) finalParticles=particles;
        });
    if(finalParticles.size()!=64U) throw std::runtime_error("DCS D4V missing final state");
    constexpr std::array<std::size_t,4> markers{5U,18U,45U,58U};
    Response out;out.reserve(12);
    for(auto m:markers){
        const auto&p=finalParticles.at(m).position;
        out.push_back(p.x);out.push_back(p.y);out.push_back(p.z);
    }
    return out;
}
Response addNoise(Response y,int salt){
    for(std::size_t i=0;i<y.size();++i){
        const double q=.719*static_cast<double>(i+1)+1.173*static_cast<double>(salt+1);
        y[i]+=kNoise*(.59*std::sin(q)+.41*std::cos(1.43*q));
    }
    return y;
}
double rms(const Response&a,const Response&b){
    return vulkax::research::dcs::responseDistance(a,b)/
        std::sqrt(static_cast<double>(a.size()));
}
double datasetRms(const std::vector<Response>&a,const std::vector<Response>&b){
    if(a.size()!=b.size()||a.empty())throw std::invalid_argument("D4V dataset mismatch");
    long double s=0.;std::size_t n=0;
    for(std::size_t i=0;i<a.size();++i){
        if(a[i].size()!=b[i].size())throw std::invalid_argument("D4V width mismatch");
        for(std::size_t j=0;j<a[i].size();++j){
            const long double d=static_cast<long double>(a[i][j])-b[i][j];
            s+=d*d;++n;
        }
    }
    return std::sqrt(static_cast<double>(s/static_cast<long double>(n)));
}
std::vector<std::pair<double,double>> calibrationPoints(){
    return {{ kCalibrationAmplitude,0.},{-kCalibrationAmplitude,0.},
            {0., kCalibrationAmplitude},{0.,-kCalibrationAmplitude}};
}
std::vector<std::pair<double,double>> heldoutPoints(){
    return {{ kHeldoutAmplitude,kHeldoutAmplitude},
            {-kHeldoutAmplitude,kHeldoutAmplitude},
            { kHeldoutAmplitude,-kHeldoutAmplitude}};
}
std::vector<InterventionPoint> probePoints(){
    std::vector<InterventionPoint> out;
    for(double s:{-kProbeAmplitude,0.0,kProbeAmplitude})
        for(double a:{-kProbeAmplitude,0.0,kProbeAmplitude})
            out.push_back({{s,a}});
    return out;
}
std::vector<Response> simulatePairs(
    double E,double nu,MpmTransferScheme scheme,double dt,
    const std::vector<std::pair<double,double>>&points){
    std::vector<Response> out;out.reserve(points.size());
    for(const auto [s,a]:points)out.push_back(simulate(E,nu,s,a,scheme,dt));
    return out;
}
std::vector<Response> simulateProbe(
    double E,double nu,MpmTransferScheme scheme,double dt,
    const std::vector<InterventionPoint>&points){
    std::vector<Response> out;out.reserve(points.size());
    for(const auto&p:points)out.push_back(simulate(E,nu,p.coordinates[0],p.coordinates[1],scheme,dt));
    return out;
}
Fit fitVariant(const Variant&v,const std::vector<Response>&truth){
    constexpr std::array<double,3> Es{12000.,15000.,18000.};
    constexpr std::array<double,3> Nus{.20,.30,.40};
    Fit best;
    for(double E:Es){
        if(v.constrainNu){
            const double nu=.10;
            const double obj=datasetRms(simulatePairs(E,nu,v.scheme,v.dt,calibrationPoints()),truth);
            if(obj<best.objective)best={E,nu,obj};
        }else for(double nu:Nus){
            const double obj=datasetRms(simulatePairs(E,nu,v.scheme,v.dt,calibrationPoints()),truth);
            if(obj<best.objective)best={E,nu,obj};
        }
    }
    return best;
}
double witnessNumRms(const Candidate&c,const AnnihilatingStencil&stencil){
    const auto n=vulkax::research::dcs::applyAnnihilatingStencil(c.probeResponses,stencil,1e-9);
    const auto r=vulkax::research::dcs::applyAnnihilatingStencil(c.refinedProbeResponses,stencil,1e-9);
    return rms(n,r);
}
double dcsProgress(
    const Candidate&a,const Candidate&b,const Response&truthWitness,
    const AnnihilatingStencil&stencil,const UncertaintyBudget&shared){
    const auto wa=vulkax::research::dcs::applyAnnihilatingStencil(a.probeResponses,stencil,1e-9);
    const auto wb=vulkax::research::dcs::applyAnnihilatingStencil(b.probeResponses,stencil,1e-9);
    const double ea=rms(wa,truthWitness), eb=rms(wb,truthWitness);
    const double na=witnessNumRms(a,stencil), nb=witnessNumRms(b,stencil);
    const auto obs=vulkax::research::dcs::propagateStencilUncertainty(stencil,shared);
    const double sigma=std::sqrt(std::max(
        obs.measurementVariance+obs.repeatVariance+na*na+nb*nb,
        std::numeric_limits<double>::epsilon()));
    return (ea-eb)/sigma;
}
std::string decision(double z){
    if(z>=kDecisionZ)return "support";
    if(z<=-kDecisionZ)return "veto";
    return "unresolved";
}
std::size_t selectRawPairPoint(
    const Candidate&a,const Candidate&b,const UncertaintyBudget&shared){
    std::size_t best=0;double scoreBest=-1.;
    for(std::size_t p=0;p<a.probeResponses.size();++p){
        const double na=rms(a.probeResponses[p],a.refinedProbeResponses[p]);
        const double nb=rms(b.probeResponses[p],b.refinedProbeResponses[p]);
        const double sigma=std::sqrt(std::max(
            shared.measurementVariance+shared.repeatVariance+na*na+nb*nb,
            std::numeric_limits<double>::epsilon()));
        const double sep=rms(a.probeResponses[p],b.probeResponses[p])/sigma;
        if(sep>scoreBest){scoreBest=sep;best=p;}
    }
    return best;
}
double rawPointProgress(
    const Candidate&a,const Candidate&b,const std::vector<Response>&truthProbe,
    std::size_t p,const UncertaintyBudget&shared){
    const double ea=rms(a.probeResponses[p],truthProbe[p]);
    const double eb=rms(b.probeResponses[p],truthProbe[p]);
    const double na=rms(a.probeResponses[p],a.refinedProbeResponses[p]);
    const double nb=rms(b.probeResponses[p],b.refinedProbeResponses[p]);
    const double sigma=std::sqrt(std::max(
        shared.measurementVariance+shared.repeatVariance+na*na+nb*nb,
        std::numeric_limits<double>::epsilon()));
    return (ea-eb)/sigma;
}
double rawBundleProgress(
    const Candidate&a,const Candidate&b,const std::vector<Response>&truthProbe,
    const UncertaintyBudget&shared){
    const double ea=datasetRms(a.probeResponses,truthProbe);
    const double eb=datasetRms(b.probeResponses,truthProbe);
    const double na=datasetRms(a.probeResponses,a.refinedProbeResponses);
    const double nb=datasetRms(b.probeResponses,b.refinedProbeResponses);
    const double sigma=std::sqrt(std::max(
        shared.measurementVariance+shared.repeatVariance+na*na+nb*nb,
        std::numeric_limits<double>::epsilon()));
    return (ea-eb)/sigma;
}
double fisherScore(const Candidate&base,const InterventionPoint&p){
    constexpr double relE=.01,dNu=.01;
    const auto ep=simulate(base.fit.E*(1.+relE),base.fit.nu,p.coordinates[0],p.coordinates[1],base.variant.scheme,base.variant.dt);
    const auto em=simulate(base.fit.E*(1.-relE),base.fit.nu,p.coordinates[0],p.coordinates[1],base.variant.scheme,base.variant.dt);
    const auto np=simulate(base.fit.E,base.fit.nu+dNu,p.coordinates[0],p.coordinates[1],base.variant.scheme,base.variant.dt);
    const auto nm=simulate(base.fit.E,base.fit.nu-dNu,p.coordinates[0],p.coordinates[1],base.variant.scheme,base.variant.dt);
    double aa=0.,ab=0.,bb=0.;
    for(std::size_t i=0;i<ep.size();++i){
        const double de=(ep[i]-em[i])/(2.*relE);
        const double dn=.1*(np[i]-nm[i])/(2.*dNu);
        aa+=de*de;ab+=de*dn;bb+=dn*dn;
    }
    const double tr=aa+bb;
    const double disc=std::sqrt(std::max(0.,(aa-bb)*(aa-bb)+4.*ab*ab));
    return std::sqrt(std::max(0.,.5*(tr-disc)));
}
std::size_t selectFisher(const Candidate&base,const std::vector<InterventionPoint>&points){
    std::size_t best=0;double sBest=-1.;
    for(std::size_t p=0;p<points.size();++p){
        const double s=fisherScore(base,points[p]);
        if(s>sBest){sBest=s;best=p;}
    }
    return best;
}
std::size_t selectMaxMotion(
    const Candidate&a,const Candidate&b,const std::vector<InterventionPoint>&points){
    std::size_t origin=0;
    for(std::size_t i=0;i<points.size();++i)
        if(std::abs(points[i].coordinates[0])<1e-15&&std::abs(points[i].coordinates[1])<1e-15)
            origin=i;
    std::size_t best=0;double mBest=-1.;
    for(std::size_t p=0;p<points.size();++p){
        const double m=.5*(rms(a.probeResponses[p],a.probeResponses[origin])+
                            rms(b.probeResponses[p],b.probeResponses[origin]));
        if(m>mBest){mBest=m;best=p;}
    }
    return best;
}
}

int main(int argc,char**argv){
    const std::filesystem::path outDir=argc>1?argv[1]:"build/dcs-d4v-discovery";
    std::filesystem::create_directories(outDir);
    std::ofstream out(outDir/"proposals.csv");
    out<<"truth_id,baseline,repair,baseline_holdout_m,repair_holdout_m,baseline_target_m,repair_target_m,label,"
          "dcs_progress_z,dcs_decision,dcs_separation,dcs_num_baseline_m,dcs_num_repair_m,moment_residual,"
          "raw_bundle_progress_z,raw_bundle_decision,raw_point_progress_z,raw_point_decision,"
          "fisher_progress_z,fisher_decision,maxmotion_progress_z,maxmotion_decision,"
          "raw_point,fisher_point,maxmotion_point,provenance\n";

    const std::array<double,3> truthEs{13625.,15875.,17425.};
    const std::array<double,2> truthNus{.25,.35};
    const std::array<Variant,4> variants{{
        {"apic",MpmTransferScheme::APIC,kCandidateDt,false},
        {"pic",MpmTransferScheme::PIC,kCandidateDt,false},
        {"constrained_nu",MpmTransferScheme::APIC,kCandidateDt,true},
        {"coarse_apic",MpmTransferScheme::APIC,kCoarseDt,false}
    }};
    const auto probe=probePoints();
    UncertaintyBudget shared;
    shared.measurementVariance=kNoise*kNoise;
    shared.repeatVariance=kNoise*kNoise;
    shared.numericalVariance=0.0;

    int truthId=0,proposalCount=0;
    for(double truthE:truthEs)for(double truthNu:truthNus){
        ++truthId;
        auto calTruth=simulatePairs(truthE,truthNu,MpmTransferScheme::APIC,kTruthDt,calibrationPoints());
        for(std::size_t i=0;i<calTruth.size();++i)
            calTruth[i]=addNoise(std::move(calTruth[i]),truthId*127+static_cast<int>(i));

        auto heldTruth=simulatePairs(truthE,truthNu,MpmTransferScheme::APIC,kTruthDt,heldoutPoints());
        for(std::size_t i=0;i<heldTruth.size();++i)
            heldTruth[i]=addNoise(std::move(heldTruth[i]),truthId*211+static_cast<int>(i));
        auto truthProbe=simulateProbe(truthE,truthNu,MpmTransferScheme::APIC,kTruthDt,probe);
        for(std::size_t i=0;i<truthProbe.size();++i)
            truthProbe[i]=addNoise(std::move(truthProbe[i]),truthId*317+static_cast<int>(i));
        const auto truthTarget=addNoise(
            simulate(truthE,truthNu,kTargetShear,kTargetAxial,MpmTransferScheme::APIC,kTruthDt),
            truthId*719);

        std::vector<Candidate> candidates;
        for(const auto&v:variants){
            Candidate c;c.variant=v;c.fit=fitVariant(v,calTruth);
            c.heldoutResponses=simulatePairs(c.fit.E,c.fit.nu,v.scheme,v.dt,heldoutPoints());
            c.heldoutError=datasetRms(c.heldoutResponses,heldTruth);
            c.probeResponses=simulateProbe(c.fit.E,c.fit.nu,v.scheme,v.dt,probe);
            c.refinedProbeResponses=simulateProbe(c.fit.E,c.fit.nu,v.scheme,v.dt*.5,probe);
            c.targetResponse=simulate(c.fit.E,c.fit.nu,kTargetShear,kTargetAxial,v.scheme,v.dt);
            c.targetError=rms(c.targetResponse,truthTarget);
            candidates.push_back(std::move(c));
        }

        for(std::size_t i=0;i<candidates.size();++i){
            for(std::size_t j=i+1;j<candidates.size();++j){
                const Candidate*base=&candidates[i];
                const Candidate*repair=&candidates[j];
                if(base->heldoutError==repair->heldoutError)continue;
                if(repair->heldoutError>base->heldoutError)std::swap(base,repair);

                std::vector<std::vector<Response>> nominal{
                    base->probeResponses,repair->probeResponses};
                std::vector<std::vector<Response>> refined{
                    base->refinedProbeResponses,repair->refinedProbeResponses};
                const auto stencil=vulkax::research::dcs::synthesizeWitnessSpaceMaximinStencil(
                    probe,2,nominal,refined,shared,1e-9,128);
                const auto truthWitness=vulkax::research::dcs::applyAnnihilatingStencil(
                    truthProbe,stencil.stencil,1e-9);
                const double zDcs=dcsProgress(*base,*repair,truthWitness,stencil.stencil,shared);
                const double nBase=witnessNumRms(*base,stencil.stencil);
                const double nRepair=witnessNumRms(*repair,stencil.stencil);

                const double zBundle=rawBundleProgress(*base,*repair,truthProbe,shared);
                const auto rawPoint=selectRawPairPoint(*base,*repair,shared);
                const double zRaw=rawPointProgress(*base,*repair,truthProbe,rawPoint,shared);
                const auto fisherPoint=selectFisher(*base,probe);
                const double zFisher=rawPointProgress(*base,*repair,truthProbe,fisherPoint,shared);
                const auto maxPoint=selectMaxMotion(*base,*repair,probe);
                const double zMax=rawPointProgress(*base,*repair,truthProbe,maxPoint,shared);

                const bool deceptive=repair->targetError>base->targetError;
                const std::string label=deceptive?"deceptive":"beneficial";
                out<<truthId<<','<<base->variant.name<<','<<repair->variant.name<<','
                   <<std::setprecision(17)<<base->heldoutError<<','<<repair->heldoutError<<','
                   <<base->targetError<<','<<repair->targetError<<','<<label<<','
                   <<zDcs<<','<<decision(zDcs)<<','<<stencil.worstCaseStandardizedSeparation<<','
                   <<nBase<<','<<nRepair<<','<<stencil.momentValidation.maximumAbsoluteResidual<<','
                   <<zBundle<<','<<decision(zBundle)<<','
                   <<zRaw<<','<<decision(zRaw)<<','
                   <<zFisher<<','<<decision(zFisher)<<','
                   <<zMax<<','<<decision(zMax)<<','
                   <<rawPoint<<','<<fisherPoint<<','<<maxPoint
                   <<",synthetic-dcs-d4v-discovery\n";
                ++proposalCount;
            }
        }
    }
    out.close();
    std::ofstream meta(outDir/"summary.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.dcs.d4v_repair_veto_discovery\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic-dcs-d4v-discovery\",\n"
        <<"  \"truth_count\": "<<truthId<<",\n"
        <<"  \"proposal_count\": "<<proposalCount<<",\n"
        <<"  \"selection_labels_used\": false,\n"
        <<"  \"decision_threshold_abs_z\": "<<kDecisionZ<<",\n"
        <<"  \"protocol\": \"research/benchmarks/DCS_D4V_REPAIR_VETO_DISCOVERY_PROTOCOL.md\",\n"
        <<"  \"warning\": \"Discovery only; do not tune this partition.\"\n"
        <<"}\n";
}
