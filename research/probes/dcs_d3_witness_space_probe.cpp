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
    std::vector<Response> refinedProbeResponses;
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
    if(std::abs(shear)<1.0e-15 && std::abs(axial)<1.0e-15){
        const auto particles=body();
        constexpr std::array<std::size_t,4> markers{5U,18U,45U,58U};
        Response out; out.reserve(12);
        for(const auto m:markers){
            const auto&p=particles.at(m).restPosition;
            out.push_back(p.x);out.push_back(p.y);out.push_back(p.z);
        }
        return out;
    }
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
    if(finalParticles.size()!=64U) throw std::runtime_error("DCS D3 probe missing final particle state");
    constexpr std::array<std::size_t,4> markers{5U,18U,45U,58U};
    Response out; out.reserve(12);
    for(const auto m:markers){
        const auto&p=finalParticles.at(m).position;
        out.push_back(p.x);out.push_back(p.y);out.push_back(p.z);
    }
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
    if(a.size()!=b.size()||a.empty())throw std::invalid_argument("DCS D3 dataset mismatch");
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
    std::vector<InterventionPoint> points;
    for(const double shear:{-kProbeAmplitude,0.0,kProbeAmplitude})
        for(const double axial:{-kProbeAmplitude,0.0,kProbeAmplitude})
            points.push_back({{shear,axial}});
    return points;
}
std::vector<Response> simulatePoints(
    double E,double nu,MpmTransferScheme scheme,double dt,
    const std::vector<InterventionPoint>& points){
    std::vector<Response> out; out.reserve(points.size());
    for(const auto&p:points)
        out.push_back(simulate(E,nu,p.coordinates[0],p.coordinates[1],scheme,dt));
    return out;
}
std::vector<Response> simulateCalibration(double E,double nu,MpmTransferScheme scheme,double dt){
    std::vector<Response> out;
    for(const auto [s,a]:calibrationPoints()) out.push_back(simulate(E,nu,s,a,scheme,dt));
    return out;
}
Fit fitVariant(const Variant&v,const std::vector<Response>&truth){
    constexpr std::array<double,3> Es{12000.,15000.,18000.};
    constexpr std::array<double,3> Nus{.20,.30,.40};
    Fit best;
    for(double E:Es){
        if(v.constrainNu){
            const double nu=.10;
            const double obj=datasetRms(simulateCalibration(E,nu,v.scheme,v.dt),truth);
            if(obj<best.objective)best={E,nu,obj};
        }else for(double nu:Nus){
            const double obj=datasetRms(simulateCalibration(E,nu,v.scheme,v.dt),truth);
            if(obj<best.objective)best={E,nu,obj};
        }
    }
    return best;
}
double pointPairAwareSeparation(
    const std::vector<std::vector<Response>>&nominal,
    const std::vector<std::vector<Response>>&refined,
    std::size_t point,
    const UncertaintyBudget&shared){
    double worst=std::numeric_limits<double>::infinity();
    for(std::size_t i=0;i<nominal.size();++i){
        const double ni=rms(nominal[i][point],refined[i][point]);
        for(std::size_t j=i+1;j<nominal.size();++j){
            const double nj=rms(nominal[j][point],refined[j][point]);
            const double variance=shared.measurementVariance+shared.repeatVariance+ni*ni+nj*nj;
            const double separation=rms(nominal[i][point],nominal[j][point])/
                std::sqrt(std::max(variance,std::numeric_limits<double>::epsilon()));
            worst=std::min(worst,separation);
        }
    }
    return worst;
}
std::size_t selectRawPairAware(
    const std::vector<std::vector<Response>>&nominal,
    const std::vector<std::vector<Response>>&refined,
    const UncertaintyBudget&shared){
    std::size_t best=0;double bestScore=-1.;
    for(std::size_t p=0;p<nominal.front().size();++p){
        const double score=pointPairAwareSeparation(nominal,refined,p,shared);
        if(score>bestScore){bestScore=score;best=p;}
    }
    return best;
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
double fisherSmallestSingularValue(double E,double nu,const InterventionPoint&point,double dt){
    constexpr double relativeE=.01;
    constexpr double deltaNu=.01;
    const auto ep=simulate(E*(1.+relativeE),nu,point.coordinates[0],point.coordinates[1],MpmTransferScheme::APIC,dt);
    const auto em=simulate(E*(1.-relativeE),nu,point.coordinates[0],point.coordinates[1],MpmTransferScheme::APIC,dt);
    const auto np=simulate(E,nu+deltaNu,point.coordinates[0],point.coordinates[1],MpmTransferScheme::APIC,dt);
    const auto nm=simulate(E,nu-deltaNu,point.coordinates[0],point.coordinates[1],MpmTransferScheme::APIC,dt);
    double aa=0.,ab=0.,bb=0.;
    for(std::size_t i=0;i<ep.size();++i){
        const double dLogE=(ep[i]-em[i])/(2.*relativeE);
        const double dNuScaled=((np[i]-nm[i])/(2.*deltaNu))*.1;
        aa+=dLogE*dLogE; ab+=dLogE*dNuScaled; bb+=dNuScaled*dNuScaled;
    }
    const double tr=aa+bb;
    const double disc=std::sqrt(std::max(0.,(aa-bb)*(aa-bb)+4.*ab*ab));
    return std::sqrt(std::max(0.,.5*(tr-disc)));
}
std::size_t selectFisherPoint(const Candidate&nominal,const std::vector<InterventionPoint>&points){
    std::size_t best=0;double bestScore=-1.;
    for(std::size_t p=0;p<points.size();++p){
        const double score=fisherSmallestSingularValue(
            nominal.fit.E,nominal.fit.nu,points[p],nominal.variant.dt);
        if(score>bestScore){bestScore=score;best=p;}
    }
    return best;
}
double maximumWitnessNumericalRms(
    const std::vector<Candidate>&candidates,const AnnihilatingStencil&stencil){
    double result=0.0;
    for(const auto&c:candidates){
        const auto nominal=vulkax::research::dcs::applyAnnihilatingStencil(
            c.probeResponses,stencil,1.0e-9);
        const auto refined=vulkax::research::dcs::applyAnnihilatingStencil(
            c.refinedProbeResponses,stencil,1.0e-9);
        result=std::max(result,rms(nominal,refined));
    }
    return result;
}
}

int main(int argc,char**argv){
    const std::filesystem::path outDir=argc>1?argv[1]:"build/dcs-d3-discovery";
    std::filesystem::create_directories(outDir);
    std::ofstream cases(outDir/"cases.csv");
    cases<<"truth_id,variant,fit_objective_m,adaptive_error_m,order2_error_m,order3_error_m,"
           "raw_bundle_error_m,raw_error_m,fisher_error_m,maxmotion_error_m,random_error_m,target_error_m,"
           "adaptive_order,adaptive_separation,order2_separation,order3_separation,"
           "order2_numerical_rms_m,order3_numerical_rms_m,raw_point,fisher_point,maxmotion_point,random_point,provenance\n";
    std::ofstream stencils(outDir/"stencils.csv");
    stencils<<"truth_id,order,point_index,shear,axial,weight,moment_residual,provenance\n";

    const std::array<double,3> truthEs{13125.,15125.,16975.};
    const std::array<double,2> truthNus{.26,.33};
    const std::array<Variant,4> variants{{
      {"apic",MpmTransferScheme::APIC,kCandidateDt,false},
      {"pic",MpmTransferScheme::PIC,kCandidateDt,false},
      {"constrained_nu",MpmTransferScheme::APIC,kCandidateDt,true},
      {"coarse_apic",MpmTransferScheme::APIC,kCoarseDt,false}
    }};
    const auto points=probePoints();
    UncertaintyBudget shared;
    shared.measurementVariance=kNoise*kNoise;
    shared.repeatVariance=kNoise*kNoise;
    shared.numericalVariance=0.0;

    int truthId=0;int rows=0;
    for(double truthE:truthEs)for(double truthNu:truthNus){
        ++truthId;
        auto calTruth=simulateCalibration(truthE,truthNu,MpmTransferScheme::APIC,kTruthDt);
        for(std::size_t i=0;i<calTruth.size();++i)
            calTruth[i]=addNoise(std::move(calTruth[i]),truthId*109+static_cast<int>(i));

        std::vector<Candidate> candidates;
        std::vector<std::vector<Response>> nominalResponses,refinedResponses;
        for(const auto&variant:variants){
            Candidate c;c.variant=variant;c.fit=fitVariant(variant,calTruth);
            c.probeResponses=simulatePoints(c.fit.E,c.fit.nu,variant.scheme,variant.dt,points);
            c.refinedProbeResponses=simulatePoints(
                c.fit.E,c.fit.nu,variant.scheme,variant.dt*.5,points);
            c.targetResponse=simulate(
                c.fit.E,c.fit.nu,kTargetShear,kTargetAxial,variant.scheme,variant.dt);
            nominalResponses.push_back(c.probeResponses);
            refinedResponses.push_back(c.refinedProbeResponses);
            candidates.push_back(std::move(c));
        }

        const SynthesizedStencil order2=
            vulkax::research::dcs::synthesizeWitnessSpaceMaximinStencil(
                points,2,nominalResponses,refinedResponses,shared,1.0e-9,128);
        const SynthesizedStencil order3=
            vulkax::research::dcs::synthesizeWitnessSpaceMaximinStencil(
                points,3,nominalResponses,refinedResponses,shared,1.0e-9,128);
        const bool choose3=
            order3.worstCaseStandardizedSeparation>order2.worstCaseStandardizedSeparation;
        const auto&adaptive=choose3?order3:order2;
        const int adaptiveOrder=choose3?3:2;

        const std::size_t rawPoint=selectRawPairAware(nominalResponses,refinedResponses,shared);
        const std::size_t fisherPoint=selectFisherPoint(candidates.front(),points);
        constexpr std::size_t originIndex=4;
        const std::size_t maxMotionPoint=selectMaxMotion(nominalResponses,originIndex);
        const std::size_t randomPoint=static_cast<std::size_t>(truthId*3U+1U)%points.size();

        auto truthProbe=simulatePoints(truthE,truthNu,MpmTransferScheme::APIC,kTruthDt,points);
        for(std::size_t i=0;i<truthProbe.size();++i)
            truthProbe[i]=addNoise(std::move(truthProbe[i]),truthId*313+static_cast<int>(i));
        const auto truthOrder2=vulkax::research::dcs::applyAnnihilatingStencil(
            truthProbe,order2.stencil,1.0e-9);
        const auto truthOrder3=vulkax::research::dcs::applyAnnihilatingStencil(
            truthProbe,order3.stencil,1.0e-9);
        const auto truthAdaptive=choose3?truthOrder3:truthOrder2;
        const auto truthTarget=addNoise(
            simulate(truthE,truthNu,kTargetShear,kTargetAxial,MpmTransferScheme::APIC,kTruthDt),
            truthId*709);

        const double order2Num=maximumWitnessNumericalRms(candidates,order2.stencil);
        const double order3Num=maximumWitnessNumericalRms(candidates,order3.stencil);

        for(std::size_t p=0;p<points.size();++p){
            stencils<<truthId<<",2,"<<p<<','<<points[p].coordinates[0]<<','<<points[p].coordinates[1]<<','
                    <<order2.stencil.weights[p]<<','<<order2.momentValidation.maximumAbsoluteResidual
                    <<",synthetic-dcs-d3-discovery\n";
            stencils<<truthId<<",3,"<<p<<','<<points[p].coordinates[0]<<','<<points[p].coordinates[1]<<','
                    <<order3.stencil.weights[p]<<','<<order3.momentValidation.maximumAbsoluteResidual
                    <<",synthetic-dcs-d3-discovery\n";
        }

        for(const auto&candidate:candidates){
            const auto c2=vulkax::research::dcs::applyAnnihilatingStencil(
                candidate.probeResponses,order2.stencil,1.0e-9);
            const auto c3=vulkax::research::dcs::applyAnnihilatingStencil(
                candidate.probeResponses,order3.stencil,1.0e-9);
            const auto&ca=choose3?c3:c2;
            const double adaptiveError=rms(ca,truthAdaptive);
            const double order2Error=rms(c2,truthOrder2);
            const double order3Error=rms(c3,truthOrder3);
            const double rawBundleError=datasetRms(candidate.probeResponses,truthProbe);
            const double rawError=rms(candidate.probeResponses[rawPoint],truthProbe[rawPoint]);
            const double fisherError=rms(candidate.probeResponses[fisherPoint],truthProbe[fisherPoint]);
            const double maxMotionError=rms(candidate.probeResponses[maxMotionPoint],truthProbe[maxMotionPoint]);
            const double randomError=rms(candidate.probeResponses[randomPoint],truthProbe[randomPoint]);
            const double targetError=rms(candidate.targetResponse,truthTarget);

            cases<<truthId<<','<<candidate.variant.name<<','<<std::setprecision(17)
                 <<candidate.fit.objective<<','<<adaptiveError<<','<<order2Error<<','<<order3Error<<','
                 <<rawBundleError<<','<<rawError<<','<<fisherError<<','<<maxMotionError<<','<<randomError<<','<<targetError<<','
                 <<adaptiveOrder<<','<<adaptive.worstCaseStandardizedSeparation<<','
                 <<order2.worstCaseStandardizedSeparation<<','<<order3.worstCaseStandardizedSeparation<<','
                 <<order2Num<<','<<order3Num<<','<<rawPoint<<','<<fisherPoint<<','<<maxMotionPoint<<','<<randomPoint
                 <<",synthetic-dcs-d3-discovery\n";
            ++rows;
        }

        std::cout<<"DCS_D3 truth="<<truthId
                 <<" adaptive_order="<<adaptiveOrder
                 <<" sep2="<<order2.worstCaseStandardizedSeparation
                 <<" sep3="<<order3.worstCaseStandardizedSeparation
                 <<" num2="<<order2Num<<" num3="<<order3Num<<"\n";
    }

    cases.close();stencils.close();
    std::ofstream meta(outDir/"summary.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.dcs.d3_discovery\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic-dcs-d3-discovery\",\n"
        <<"  \"truth_count\": "<<truthId<<",\n"
        <<"  \"candidate_count\": "<<rows<<",\n"
        <<"  \"intervention_points\": 9,\n"
        <<"  \"candidate_orders\": [2,3],\n"
        <<"  \"selection_labels_used\": false,\n"
        <<"  \"uncertainty\": \"direct nominal-vs-half-dt witness-space RMS\",\n"
        <<"  \"protocol\": \"research/benchmarks/DCS_D3_DISCOVERY_PROTOCOL.md\",\n"
        <<"  \"warning\": \"Discovery partition only; never use as confirmatory validation.\"\n"
        <<"}\n";
}
