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
constexpr double kResolutionThreshold=2.0;

struct Variant {
    std::string name;
    MpmTransferScheme scheme{MpmTransferScheme::APIC};
    double dt{kCandidateDt};
    bool constrainNu{};
};
struct Fit {
    double E{},nu{},objective{std::numeric_limits<double>::infinity()};
};
struct Candidate {
    Variant variant;
    Fit fit;
    std::vector<Response> nominal;
    std::vector<Response> refined;
    Response target;
};

std::vector<MpmParticle> body(){
    std::vector<MpmParticle> ps; std::uint64_t id=1;
    constexpr double h=.12,v=h*h*h,rho=1000.0;
    for(int z=0;z<4;++z)for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        MpmParticle p;
        p.id=id++;
        p.restPosition={(x-1.5)*h,(y-1.5)*h,(z-1.5)*h};
        p.position=p.restPosition;
        p.restVolume=v;
        p.mass=rho*v;
        ps.push_back(p);
    }
    return ps;
}

vulkax::gaussian::GaussianSplat splat(Vec3 p){
    vulkax::gaussian::GaussianSplat s;
    s.position=p;
    s.logScale={std::log(.06),std::log(.045),std::log(.035)};
    s.rotation={1.,0.,0.,0.};
    s.opacityLogit=4.;
    return s;
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
    MpmGridSettings g;
    g.origin={-.8,-.8,-.8};
    g.nx=22; g.ny=22; g.nz=22;
    g.cellSize=.08;
    g.boundaryCells=0;
    return g;
}

Matrix3 deformation(double shear,double axial){
    return {1.+axial,shear,0., 0.,1.,0., 0.,0.,1.};
}

Response restResponse(){
    const auto particles=body();
    constexpr std::array<std::size_t,4> markers{5U,18U,45U,58U};
    Response out; out.reserve(12);
    for(const auto m:markers){
        const auto&p=particles.at(m).restPosition;
        out.push_back(p.x); out.push_back(p.y); out.push_back(p.z);
    }
    return out;
}

Response simulate(double E,double nu,double shear,double axial,
                  MpmTransferScheme scheme,double dt){
    if(std::abs(shear)<1e-15 && std::abs(axial)<1e-15)
        return restResponse();

    NonlinearDeformableWorldSettings s;
    s.steps=static_cast<std::size_t>(std::llround(kHorizon/dt));
    s.dt=dt;
    s.material={1000.,E,nu};
    s.initialDeformation=deformation(shear,axial);
    s.couplingNeighborCount=20;
    s.transferScheme=scheme;

    std::vector<MpmParticle> finalParticles;
    (void)vulkax::research::runNonlinearDeformableWorld(
        world(),{0,1,2,3},body(),grid(),s,{},
        [&](const auto& frame,const GaussianCloud&,
            const std::vector<MpmParticle>& particles){
            if(frame.step==s.steps) finalParticles=particles;
        });
    if(finalParticles.size()!=64U)
        throw std::runtime_error("DCS3 missing final particle state");

    constexpr std::array<std::size_t,4> markers{5U,18U,45U,58U};
    Response out; out.reserve(12);
    for(const auto m:markers){
        const auto&p=finalParticles.at(m).position;
        out.push_back(p.x); out.push_back(p.y); out.push_back(p.z);
    }
    return out;
}

Response addNoise(Response y,int salt){
    for(std::size_t i=0;i<y.size();++i){
        const double q=.733*static_cast<double>(i+1)+1.191*static_cast<double>(salt+1);
        y[i]+=kNoise*(.63*std::sin(q)+.37*std::cos(1.41*q));
    }
    return y;
}

double rms(const Response&a,const Response&b){
    return vulkax::research::dcs::responseDistance(a,b)/
        std::sqrt(static_cast<double>(a.size()));
}

double datasetRms(const std::vector<Response>&a,const std::vector<Response>&b){
    if(a.size()!=b.size()||a.empty())
        throw std::invalid_argument("DCS3 dataset mismatch");
    long double s=0.; std::size_t n=0;
    for(std::size_t i=0;i<a.size();++i){
        if(a[i].size()!=b[i].size())
            throw std::invalid_argument("DCS3 response width mismatch");
        for(std::size_t j=0;j<a[i].size();++j){
            const long double d=static_cast<long double>(a[i][j])-b[i][j];
            s+=d*d; ++n;
        }
    }
    return std::sqrt(static_cast<double>(s/static_cast<long double>(n)));
}

std::vector<std::pair<double,double>> calibrationPoints(){
    return {
        { kCalibrationAmplitude,0.},
        {-kCalibrationAmplitude,0.},
        {0., kCalibrationAmplitude},
        {0.,-kCalibrationAmplitude}
    };
}

std::vector<InterventionPoint> probePoints(){
    const double a=kProbeAmplitude;
    std::vector<InterventionPoint> points;
    for(const double shear:{-a,0.0,a})
        for(const double axial:{-a,0.0,a})
            points.push_back({{shear,axial}});
    return points;
}

std::size_t originIndex(const std::vector<InterventionPoint>&points){
    for(std::size_t i=0;i<points.size();++i)
        if(std::abs(points[i].coordinates[0])<1e-15 &&
           std::abs(points[i].coordinates[1])<1e-15)
            return i;
    throw std::runtime_error("DCS3 probe lattice lacks origin");
}

std::vector<Response> simulatePoints(
    double E,double nu,MpmTransferScheme scheme,double dt,
    const std::vector<InterventionPoint>&points){
    std::vector<Response> out; out.reserve(points.size());
    for(const auto&p:points)
        out.push_back(simulate(
            E,nu,p.coordinates[0],p.coordinates[1],scheme,dt));
    return out;
}

std::vector<Response> simulateCalibration(
    double E,double nu,MpmTransferScheme scheme,double dt){
    std::vector<Response> out;
    for(const auto [s,a]:calibrationPoints())
        out.push_back(simulate(E,nu,s,a,scheme,dt));
    return out;
}

Fit fitVariant(const Variant&v,const std::vector<Response>&truth){
    constexpr std::array<double,3> Es{12000.,15000.,18000.};
    constexpr std::array<double,3> Nus{.20,.30,.40};
    Fit best;
    for(const double E:Es){
        if(v.constrainNu){
            const double nu=.10;
            const double obj=datasetRms(
                simulateCalibration(E,nu,v.scheme,v.dt),truth);
            if(obj<best.objective) best={E,nu,obj};
        }else{
            for(const double nu:Nus){
                const double obj=datasetRms(
                    simulateCalibration(E,nu,v.scheme,v.dt),truth);
                if(obj<best.objective) best={E,nu,obj};
            }
        }
    }
    return best;
}

double rawPointScore(
    std::size_t point,
    const std::vector<std::vector<Response>>&nominal,
    const std::vector<std::vector<Response>>&refined,
    const UncertaintyBudget&observation){
    double worst=std::numeric_limits<double>::infinity();
    for(std::size_t a=0;a<nominal.size();++a){
        const double na=rms(nominal[a][point],refined[a][point]);
        for(std::size_t b=a+1;b<nominal.size();++b){
            const double nb=rms(nominal[b][point],refined[b][point]);
            const double sep=rms(nominal[a][point],nominal[b][point]);
            const double variance=
                observation.measurementVariance+
                observation.repeatVariance+na*na+nb*nb;
            worst=std::min(worst,sep/std::sqrt(variance));
        }
    }
    return worst;
}

std::size_t selectRawGuarded(
    const std::vector<std::vector<Response>>&nominal,
    const std::vector<std::vector<Response>>&refined,
    const UncertaintyBudget&observation){
    std::size_t best=0; double bestScore=-1.;
    for(std::size_t p=0;p<nominal.front().size();++p){
        const double score=rawPointScore(p,nominal,refined,observation);
        if(score>bestScore){bestScore=score;best=p;}
    }
    return best;
}

std::size_t selectMaxMotion(
    const std::vector<std::vector<Response>>&models,std::size_t origin){
    std::size_t best=0; double bestMotion=-1.;
    for(std::size_t p=0;p<models.front().size();++p){
        double motion=0.;
        for(const auto&m:models)
            motion+=rms(m[p],m[origin]);
        motion/=static_cast<double>(models.size());
        if(motion>bestMotion){bestMotion=motion;best=p;}
    }
    return best;
}

double fisherSmallestSingularValue(
    double E,double nu,const InterventionPoint&point,double dt){
    constexpr double relativeE=.01;
    constexpr double deltaNu=.01;
    const auto ep=simulate(E*(1.+relativeE),nu,
        point.coordinates[0],point.coordinates[1],
        MpmTransferScheme::APIC,dt);
    const auto em=simulate(E*(1.-relativeE),nu,
        point.coordinates[0],point.coordinates[1],
        MpmTransferScheme::APIC,dt);
    const auto np=simulate(E,nu+deltaNu,
        point.coordinates[0],point.coordinates[1],
        MpmTransferScheme::APIC,dt);
    const auto nm=simulate(E,nu-deltaNu,
        point.coordinates[0],point.coordinates[1],
        MpmTransferScheme::APIC,dt);
    double aa=0.,ab=0.,bb=0.;
    for(std::size_t i=0;i<ep.size();++i){
        const double dLogE=(ep[i]-em[i])/(2.*relativeE);
        const double dNuScaled=((np[i]-nm[i])/(2.*deltaNu))*.1;
        aa+=dLogE*dLogE; ab+=dLogE*dNuScaled; bb+=dNuScaled*dNuScaled;
    }
    const double tr=aa+bb;
    const double disc=std::sqrt(std::max(
        0.,(aa-bb)*(aa-bb)+4.*ab*ab));
    return std::sqrt(std::max(0.,.5*(tr-disc)));
}

std::size_t selectFisherPoint(
    const Candidate&nominal,const std::vector<InterventionPoint>&points){
    std::size_t best=0; double bestScore=-1.;
    for(std::size_t p=0;p<points.size();++p){
        const double score=fisherSmallestSingularValue(
            nominal.fit.E,nominal.fit.nu,points[p],nominal.variant.dt);
        if(score>bestScore){bestScore=score;best=p;}
    }
    return best;
}

double selectedWitnessNumericalRms(
    const SynthesizedStencil&stencil,
    const std::vector<std::vector<Response>>&nominal,
    const std::vector<std::vector<Response>>&refined){
    double maximum=0.0;
    for(std::size_t m=0;m<nominal.size();++m){
        const auto a=vulkax::research::dcs::applyAnnihilatingStencil(
            nominal[m],stencil.stencil,1e-9);
        const auto b=vulkax::research::dcs::applyAnnihilatingStencil(
            refined[m],stencil.stencil,1e-9);
        maximum=std::max(maximum,rms(a,b));
    }
    return maximum;
}

std::vector<UncertaintyBudget> conservativeNumericalBudgets(
    const std::vector<std::vector<Response>>&nominal,
    const std::vector<std::vector<Response>>&refined){
    std::vector<UncertaintyBudget> out(nominal.size());
    for(std::size_t m=0;m<nominal.size();++m){
        double maximum=0.0;
        for(std::size_t p=0;p<nominal[m].size();++p)
            maximum=std::max(maximum,rms(nominal[m][p],refined[m][p]));
        out[m].numericalVariance=maximum*maximum;
    }
    return out;
}

}

int main(int argc,char**argv){
    const std::filesystem::path outDir=
        argc>1?argv[1]:"build/dcs3-guarded-discovery";
    std::filesystem::create_directories(outDir);

    std::ofstream cases(outDir/"cases.csv");
    cases<<"truth_id,variant,fit_objective_m,selected_order,"
           "guarded_order2_score,guarded_order3_score,"
           "conservative_order2_score,conservative_order3_score,"
           "selected_score,resolved,selected_numerical_rms_m,moment_residual,"
           "dcs_error_m,raw_bundle_error_m,raw_error_m,fisher_error_m,"
           "maxmotion_error_m,random_error_m,target_error_m,"
           "raw_point,fisher_point,maxmotion_point,random_point,provenance\n";

    std::ofstream stencils(outDir/"stencils.csv");
    stencils<<"truth_id,selected_order,point_index,shear,axial,weight,provenance\n";

    const std::array<double,3> truthEs{13250.,14750.,16750.};
    const std::array<double,2> truthNus{.26,.32};
    const std::array<Variant,4> variants{{
        {"apic",MpmTransferScheme::APIC,kCandidateDt,false},
        {"pic",MpmTransferScheme::PIC,kCandidateDt,false},
        {"constrained_nu",MpmTransferScheme::APIC,kCandidateDt,true},
        {"coarse_apic",MpmTransferScheme::APIC,kCoarseDt,false}
    }};

    const auto points=probePoints();
    const auto origin=originIndex(points);
    UncertaintyBudget observation;
    observation.measurementVariance=kNoise*kNoise;
    observation.repeatVariance=kNoise*kNoise;

    int truthId=0;
    int rowCount=0;
    for(const double truthE:truthEs){
        for(const double truthNu:truthNus){
            ++truthId;
            auto calibrationTruth=simulateCalibration(
                truthE,truthNu,MpmTransferScheme::APIC,kTruthDt);
            for(std::size_t i=0;i<calibrationTruth.size();++i)
                calibrationTruth[i]=addNoise(
                    std::move(calibrationTruth[i]),
                    truthId*101+static_cast<int>(i));

            std::vector<Candidate> candidates;
            std::vector<std::vector<Response>> nominal;
            std::vector<std::vector<Response>> refined;
            for(const auto&variant:variants){
                Candidate c;
                c.variant=variant;
                c.fit=fitVariant(variant,calibrationTruth);
                c.nominal=simulatePoints(
                    c.fit.E,c.fit.nu,variant.scheme,variant.dt,points);
                c.refined=simulatePoints(
                    c.fit.E,c.fit.nu,variant.scheme,variant.dt*.5,points);
                c.target=simulate(
                    c.fit.E,c.fit.nu,kTargetShear,kTargetAxial,
                    variant.scheme,variant.dt);
                nominal.push_back(c.nominal);
                refined.push_back(c.refined);
                candidates.push_back(std::move(c));
            }

            const auto guarded2=
                vulkax::research::dcs::synthesizeNumericallyGuardedMaximinStencil(
                    points,2,nominal,refined,observation,1e-9,128);
            const auto guarded3=
                vulkax::research::dcs::synthesizeNumericallyGuardedMaximinStencil(
                    points,3,nominal,refined,observation,1e-9,128);

            const auto rawNumerical=
                conservativeNumericalBudgets(nominal,refined);
            const auto conservative2=
                vulkax::research::dcs::synthesizePairAwareMaximinStencil(
                    points,2,nominal,observation,rawNumerical,1e-9,128);
            const auto conservative3=
                vulkax::research::dcs::synthesizePairAwareMaximinStencil(
                    points,3,nominal,observation,rawNumerical,1e-9,128);

            const SynthesizedStencil&selected=
                guarded3.worstCaseStandardizedSeparation>
                guarded2.worstCaseStandardizedSeparation?guarded3:guarded2;
            const int selectedOrder=
                (&selected==&guarded3)?3:2;
            const bool resolved=
                selected.worstCaseStandardizedSeparation>=kResolutionThreshold;
            const double witnessNumerical=
                selectedWitnessNumericalRms(selected,nominal,refined);

            const std::size_t rawPoint=
                selectRawGuarded(nominal,refined,observation);
            const std::size_t fisherPoint=
                selectFisherPoint(candidates.front(),points);
            const std::size_t maxMotionPoint=
                selectMaxMotion(nominal,origin);
            const std::size_t randomPoint=
                static_cast<std::size_t>(truthId)%points.size();

            auto truthProbe=simulatePoints(
                truthE,truthNu,MpmTransferScheme::APIC,kTruthDt,points);
            for(std::size_t i=0;i<truthProbe.size();++i)
                truthProbe[i]=addNoise(
                    std::move(truthProbe[i]),
                    truthId*307+static_cast<int>(i));
            const auto truthWitness=
                vulkax::research::dcs::applyAnnihilatingStencil(
                    truthProbe,selected.stencil,1e-9);
            const auto truthTarget=addNoise(
                simulate(
                    truthE,truthNu,kTargetShear,kTargetAxial,
                    MpmTransferScheme::APIC,kTruthDt),
                truthId*701);

            for(std::size_t p=0;p<points.size();++p){
                stencils<<truthId<<','<<selectedOrder<<','<<p<<','
                        <<points[p].coordinates[0]<<','
                        <<points[p].coordinates[1]<<','
                        <<selected.stencil.weights[p]
                        <<",synthetic-dcs3-guarded-discovery\n";
            }

            for(const auto&candidate:candidates){
                const auto candidateWitness=
                    vulkax::research::dcs::applyAnnihilatingStencil(
                        candidate.nominal,selected.stencil,1e-9);
                const double dcsError=rms(candidateWitness,truthWitness);
                const double rawBundleError=
                    datasetRms(candidate.nominal,truthProbe);
                const double rawError=
                    rms(candidate.nominal[rawPoint],truthProbe[rawPoint]);
                const double fisherError=
                    rms(candidate.nominal[fisherPoint],truthProbe[fisherPoint]);
                const double maxMotionError=
                    rms(candidate.nominal[maxMotionPoint],
                        truthProbe[maxMotionPoint]);
                const double randomError=
                    rms(candidate.nominal[randomPoint],truthProbe[randomPoint]);
                const double targetError=
                    rms(candidate.target,truthTarget);

                cases<<truthId<<','<<candidate.variant.name<<','
                     <<std::setprecision(17)<<candidate.fit.objective<<','
                     <<selectedOrder<<','
                     <<guarded2.worstCaseStandardizedSeparation<<','
                     <<guarded3.worstCaseStandardizedSeparation<<','
                     <<conservative2.worstCaseStandardizedSeparation<<','
                     <<conservative3.worstCaseStandardizedSeparation<<','
                     <<selected.worstCaseStandardizedSeparation<<','
                     <<(resolved?1:0)<<','
                     <<witnessNumerical<<','
                     <<selected.momentValidation.maximumAbsoluteResidual<<','
                     <<dcsError<<','<<rawBundleError<<','<<rawError<<','
                     <<fisherError<<','<<maxMotionError<<','<<randomError<<','
                     <<targetError<<','
                     <<rawPoint<<','<<fisherPoint<<','<<maxMotionPoint<<','
                     <<randomPoint
                     <<",synthetic-dcs3-guarded-discovery\n";
                ++rowCount;
            }

            std::cout<<"DCS3 truth="<<truthId
                     <<" order="<<selectedOrder
                     <<" score="<<selected.worstCaseStandardizedSeparation
                     <<" guarded2="<<guarded2.worstCaseStandardizedSeparation
                     <<" guarded3="<<guarded3.worstCaseStandardizedSeparation
                     <<" conservative2="<<conservative2.worstCaseStandardizedSeparation
                     <<" conservative3="<<conservative3.worstCaseStandardizedSeparation
                     <<" numerical="<<witnessNumerical
                     <<" resolved="<<(resolved?1:0)<<"\n";
        }
    }

    cases.close();
    stencils.close();
    std::ofstream meta(outDir/"summary.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.dcs3.guarded_discovery\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic-dcs3-guarded-discovery\",\n"
        <<"  \"truth_count\": "<<truthId<<",\n"
        <<"  \"candidate_count\": "<<rowCount<<",\n"
        <<"  \"probe_lattice\": \"3x3_shear_axial\",\n"
        <<"  \"orders_tested\": [2,3],\n"
        <<"  \"resolution_threshold\": "<<kResolutionThreshold<<",\n"
        <<"  \"selection_labels_used\": false,\n"
        <<"  \"warning\": \"Discovery partition only; do not use as confirmatory evidence.\"\n"
        <<"}\n";
}
