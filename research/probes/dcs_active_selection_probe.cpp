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
using vulkax::research::dcs::InterventionPoint;
using vulkax::research::dcs::Response;
using vulkax::research::dcs::SynthesizedStencil;
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
constexpr double kProbeAmplitude=0.060;
constexpr double kTargetShear=0.095;
constexpr double kTargetAxial=0.075;
constexpr double kNoise=2.0e-5;

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
    std::vector<Response> probeResponses;
    Response targetResponse;
};

std::vector<MpmParticle> body(){
    std::vector<MpmParticle> ps; std::uint64_t id=1;
    constexpr double h=.12,v=h*h*h,rho=1000.0;
    for(int z=0;z<4;++z)for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        MpmParticle p; p.id=id++; p.restPosition={(x-1.5)*h,(y-1.5)*h,(z-1.5)*h};
        p.position=p.restPosition; p.restVolume=v; p.mass=rho*v; ps.push_back(p);
    } return ps;
}
vulkax::gaussian::GaussianSplat splat(Vec3 p){
    vulkax::gaussian::GaussianSplat s; s.position=p;
    s.logScale={std::log(.06),std::log(.045),std::log(.035)};
    s.rotation={1.,0.,0.,0.}; s.opacityLogit=4.; return s;
}
GaussianCloud world(){
    GaussianCloud w;
    w.splats.push_back(splat({-.08,.04,.02})); w.splats.push_back(splat({.09,-.06,.03}));
    w.splats.push_back(splat({.04,.10,-.07})); w.splats.push_back(splat({-.05,-.08,-.06}));
    return w;
}
MpmGridSettings grid(){
    MpmGridSettings g; g.origin={-.8,-.8,-.8}; g.nx=22; g.ny=22; g.nz=22;
    g.cellSize=.08; g.boundaryCells=0; return g;
}
Matrix3 deformation(double shear,double axial){
    return {1.+axial,shear,0., 0.,1.,0., 0.,0.,1.};
}
Response simulate(double E,double nu,double shear,double axial,MpmTransferScheme scheme,double dt){
    NonlinearDeformableWorldSettings s;
    s.steps=static_cast<std::size_t>(std::llround(kHorizon/dt));
    s.dt=dt; s.material={1000.,E,nu}; s.initialDeformation=deformation(shear,axial);
    s.couplingNeighborCount=20; s.transferScheme=scheme;
    std::vector<MpmParticle> finalParticles;
    (void)vulkax::research::runNonlinearDeformableWorld(
      world(),{0,1,2,3},body(),grid(),s,{},
      [&](const auto& frame,const GaussianCloud&,const std::vector<MpmParticle>& particles){
        if(frame.step==s.steps) finalParticles=particles;
      });
    if(finalParticles.size()!=64U) throw std::runtime_error("DCS active probe missing final particle state");
    constexpr std::array<std::size_t,4> markers{5U,18U,45U,58U};
    Response out; out.reserve(12);
    for(const auto m:markers){const auto&p=finalParticles.at(m).position;out.push_back(p.x);out.push_back(p.y);out.push_back(p.z);}
    return out;
}
Response addNoise(Response y,int salt){
    for(std::size_t i=0;i<y.size();++i){
        const double q=.733*static_cast<double>(i+1)+1.191*static_cast<double>(salt+1);
        y[i]+=kNoise*(.63*std::sin(q)+.37*std::cos(1.41*q));
    } return y;
}
double rms(const Response&a,const Response&b){
    return vulkax::research::dcs::responseDistance(a,b)/std::sqrt(static_cast<double>(a.size()));
}
double datasetRms(const std::vector<Response>&a,const std::vector<Response>&b){
    if(a.size()!=b.size()||a.empty())throw std::invalid_argument("DCS active dataset mismatch");
    long double s=0.;std::size_t n=0;
    for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<a[i].size();++j){
        const long double d=static_cast<long double>(a[i][j])-b[i][j];s+=d*d;++n;
    }
    return std::sqrt(static_cast<double>(s/static_cast<long double>(n)));
}
std::vector<std::pair<double,double>> calibrationPoints(){
    return {{ kCalibrationAmplitude,0.},{-kCalibrationAmplitude,0.},
            {0., kCalibrationAmplitude},{0.,-kCalibrationAmplitude}};
}
std::vector<InterventionPoint> probePoints(){
    const double a=kProbeAmplitude;
    return {{{ a, a}},{{ a,-a}},{{-a, a}},{{-a,-a}},{{0.,0.}}};
}
std::vector<Response> simulatePoints(
    double E,double nu,MpmTransferScheme scheme,double dt,
    const std::vector<InterventionPoint>& points){
    std::vector<Response> out; out.reserve(points.size());
    for(const auto&p:points) out.push_back(simulate(E,nu,p.coordinates[0],p.coordinates[1],scheme,dt));
    return out;
}
std::vector<Response> simulateCalibration(double E,double nu,MpmTransferScheme scheme,double dt){
    std::vector<Response> out;
    for(const auto [s,a]:calibrationPoints())out.push_back(simulate(E,nu,s,a,scheme,dt));
    return out;
}
Fit fitVariant(const Variant&v,const std::vector<Response>&truth){
    constexpr std::array<double,3> Es{12000.,15000.,18000.};
    constexpr std::array<double,3> Nus{.20,.30,.40};
    Fit best;
    for(double E:Es){
        if(v.constrainNu){
            const double nu=.10; const double obj=datasetRms(simulateCalibration(E,nu,v.scheme,v.dt),truth);
            if(obj<best.objective)best={E,nu,obj};
        }else for(double nu:Nus){
            const double obj=datasetRms(simulateCalibration(E,nu,v.scheme,v.dt),truth);
            if(obj<best.objective)best={E,nu,obj};
        }
    }
    return best;
}
std::size_t selectRawMaximin(const std::vector<std::vector<Response>>&models,const UncertaintyBudget&budget){
    std::size_t best=0;double bestScore=-1.;
    const std::size_t points=models.front().size();
    for(std::size_t p=0;p<points;++p){
        std::vector<Response>w;for(const auto&m:models)w.push_back(m[p]);
        const double score=vulkax::research::dcs::worstCaseStandardizedSeparation(w,budget);
        if(score>bestScore){bestScore=score;best=p;}
    } return best;
}
std::size_t selectMaxMotion(const std::vector<std::vector<Response>>&models,std::size_t originIndex){
    std::size_t best=0;double bestMotion=-1.;
    for(std::size_t p=0;p<models.front().size();++p){
        double motion=0.;
        for(const auto&m:models)motion+=rms(m[p],m[originIndex]);
        motion/=static_cast<double>(models.size());
        if(motion>bestMotion){bestMotion=motion;best=p;}
    } return best;
}
}

int main(int argc,char**argv){
    const std::filesystem::path outDir=argc>1?argv[1]:"build/dcs-active-selection";
    std::filesystem::create_directories(outDir);
    std::ofstream cases(outDir/"cases.csv");
    cases<<"truth_id,variant,fit_objective_m,dcs_error_m,raw_error_m,maxmotion_error_m,random_error_m,target_error_m,"
           "dcs_maximin_separation,raw_point,maxmotion_point,random_point,provenance\n";
    std::ofstream stencils(outDir/"stencils.csv");
    stencils<<"truth_id,point_index,shear,axial,weight,provenance\n";

    const std::array<double,2> truthEs{13750.,16250.};
    const std::array<double,2> truthNus{.24,.36};
    const std::array<Variant,4> variants{{
      {"apic",MpmTransferScheme::APIC,kCandidateDt,false},
      {"pic",MpmTransferScheme::PIC,kCandidateDt,false},
      {"constrained_nu",MpmTransferScheme::APIC,kCandidateDt,true},
      {"coarse_apic",MpmTransferScheme::APIC,kCoarseDt,false}
    }};
    const auto points=probePoints();
    UncertaintyBudget budget;
    budget.measurementVariance=kNoise*kNoise;
    budget.repeatVariance=kNoise*kNoise;
    budget.numericalVariance=kNoise*kNoise; // discovery placeholder; D2 validation replaces this with measured refinement variance.

    int truthId=0;int rows=0;
    for(double truthE:truthEs)for(double truthNu:truthNus){
        ++truthId;
        auto calTruth=simulateCalibration(truthE,truthNu,MpmTransferScheme::APIC,kTruthDt);
        for(std::size_t i=0;i<calTruth.size();++i)calTruth[i]=addNoise(std::move(calTruth[i]),truthId*101+static_cast<int>(i));

        std::vector<Candidate> candidates;
        std::vector<std::vector<Response>> modelResponses;
        for(const auto&variant:variants){
            Candidate c; c.variant=variant;c.fit=fitVariant(variant,calTruth);
            c.probeResponses=simulatePoints(c.fit.E,c.fit.nu,variant.scheme,variant.dt,points);
            c.targetResponse=simulate(c.fit.E,c.fit.nu,kTargetShear,kTargetAxial,variant.scheme,variant.dt);
            modelResponses.push_back(c.probeResponses);candidates.push_back(std::move(c));
        }

        const SynthesizedStencil dcs=vulkax::research::dcs::synthesizeMaximinAnnihilatingStencil(
            points,2,modelResponses,budget,1.0e-9,128);
        const std::size_t rawPoint=selectRawMaximin(modelResponses,budget);
        constexpr std::size_t originIndex=4;
        const std::size_t maxMotionPoint=selectMaxMotion(modelResponses,originIndex);
        const std::size_t randomPoint=static_cast<std::size_t>(truthId)%points.size();

        auto truthProbe=simulatePoints(truthE,truthNu,MpmTransferScheme::APIC,kTruthDt,points);
        for(std::size_t i=0;i<truthProbe.size();++i)truthProbe[i]=addNoise(std::move(truthProbe[i]),truthId*307+static_cast<int>(i));
        const auto truthDcs=vulkax::research::dcs::applyAnnihilatingStencil(truthProbe,dcs.stencil,1.0e-9);
        const auto truthTarget=addNoise(
            simulate(truthE,truthNu,kTargetShear,kTargetAxial,MpmTransferScheme::APIC,kTruthDt),
            truthId*701);

        for(std::size_t p=0;p<points.size();++p)
            stencils<<truthId<<','<<p<<','<<points[p].coordinates[0]<<','<<points[p].coordinates[1]<<','
                    <<dcs.stencil.weights[p]<<",synthetic-dcs-active-discovery\n";

        for(const auto&candidate:candidates){
            const auto candidateDcs=vulkax::research::dcs::applyAnnihilatingStencil(
                candidate.probeResponses,dcs.stencil,1.0e-9);
            const double dcsError=rms(candidateDcs,truthDcs);
            const double rawError=rms(candidate.probeResponses[rawPoint],truthProbe[rawPoint]);
            const double maxMotionError=rms(candidate.probeResponses[maxMotionPoint],truthProbe[maxMotionPoint]);
            const double randomError=rms(candidate.probeResponses[randomPoint],truthProbe[randomPoint]);
            const double targetError=rms(candidate.targetResponse,truthTarget);
            cases<<truthId<<','<<candidate.variant.name<<','<<std::setprecision(17)
                 <<candidate.fit.objective<<','<<dcsError<<','<<rawError<<','<<maxMotionError<<','<<randomError<<','
                 <<targetError<<','<<dcs.worstCaseStandardizedSeparation<<','
                 <<rawPoint<<','<<maxMotionPoint<<','<<randomPoint
                 <<",synthetic-dcs-active-discovery\n";
            ++rows;
            std::cout<<"DCS_ACTIVE truth="<<truthId<<" variant="<<candidate.variant.name
                     <<" dcs="<<dcsError<<" raw="<<rawError<<" maxmotion="<<maxMotionError
                     <<" random="<<randomError<<" target="<<targetError<<"\n";
        }
    }
    cases.close();stencils.close();
    std::ofstream meta(outDir/"summary.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.dcs.active_selection_discovery\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic-dcs-active-discovery\",\n"
        <<"  \"truth_count\": "<<truthId<<",\n"
        <<"  \"candidate_count\": "<<rows<<",\n"
        <<"  \"selector_order\": 2,\n"
        <<"  \"selection_labels_used\": false,\n"
        <<"  \"numerical_variance_status\": \"placeholder_equal_noise_variance_discovery_only\",\n"
        <<"  \"warning\": \"Discovery only. Fresh D2 validation must replace numerical variance and freeze all selectors.\"\n"
        <<"}\n";
}
