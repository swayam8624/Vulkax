#include "vulkax/coupling/mpm_gaussian.hpp"
#include "vulkax/research/prescribed_mpm.hpp"

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
using vulkax::gaussian::GaussianSplat;
using vulkax::math::Vec3;
using vulkax::research::PrescribedParticleTarget;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmMaterial;
using vulkax::solvers::MpmParticle;
using vulkax::solvers::MpmTransferScheme;

constexpr double kDensity = 1000.0;
constexpr double kVolume = 0.0005;       // 50 x 50 x 200 mm.
constexpr double kTruthLength = 0.200;
constexpr double kObservedLength = 0.150;
constexpr double kTruthYoung = 80000.0;
constexpr double kPoisson = 0.30;
constexpr double kDt = 5.0e-5;
constexpr std::size_t kSamples = 20;
constexpr std::size_t kSubstepsPerSample = 20;
constexpr double kTrainShear = 0.015;
constexpr double kTargetShear = 0.030;

struct BodyGeometry {
    double length{};
    double cross{};
    int nCross{};
    int nLong{};
};

struct Trace {
    std::vector<std::vector<Vec3>> markers;
    double minimumJ{1.0};
    double maximumBoundaryError{};
};

struct FitResult {
    double young{};
    double rms{};
};

double sq(double x) { return x*x; }

double distance(Vec3 a,Vec3 b) {
    return vulkax::math::length(a-b);
}

BodyGeometry geometry(double length) {
    BodyGeometry g;
    g.length=length;
    g.cross=std::sqrt(kVolume/length);
    g.nCross=5;
    // Keep longitudinal particle spacing close to 10 mm in both worlds.
    g.nLong=std::max(5,static_cast<int>(std::llround(length/0.010))+1);
    return g;
}

std::vector<MpmParticle> makeParticles(const BodyGeometry& g) {
    std::vector<MpmParticle> out;
    const std::size_t count=static_cast<std::size_t>(g.nCross*g.nCross*g.nLong);
    out.reserve(count);
    const double restVolume=kVolume/static_cast<double>(count);
    const double mass=kDensity*restVolume;
    std::uint64_t id=1;
    for(int iz=0;iz<g.nLong;++iz)
        for(int iy=0;iy<g.nCross;++iy)
            for(int ix=0;ix<g.nCross;++ix) {
                const double ux=static_cast<double>(ix)/static_cast<double>(g.nCross-1);
                const double uy=static_cast<double>(iy)/static_cast<double>(g.nCross-1);
                const double uz=static_cast<double>(iz)/static_cast<double>(g.nLong-1);
                MpmParticle p;
                p.id=id++;
                p.restPosition={
                    (ux-0.5)*g.cross,
                    (uy-0.5)*g.cross,
                    (uz-0.5)*g.length
                };
                p.position=p.restPosition;
                p.mass=mass;
                p.restVolume=restVolume;
                out.push_back(p);
            }
    return out;
}

MpmGridSettings makeGrid(const BodyGeometry& g,double maxShear) {
    const double dxCross=g.cross/static_cast<double>(g.nCross-1);
    const double dxLong=g.length/static_cast<double>(g.nLong-1);
    const double cell=std::min(dxCross,dxLong);
    const double margin=4.0*cell;
    MpmGridSettings grid;
    grid.origin={-0.5*g.cross-margin,-0.5*g.cross-margin,-0.5*g.length-margin};
    grid.cellSize=cell;
    grid.boundaryCells=0;
    grid.nx=static_cast<std::size_t>(
        std::ceil((g.cross+maxShear+2.0*margin)/cell))+3U;
    grid.ny=static_cast<std::size_t>(
        std::ceil((g.cross+2.0*margin)/cell))+3U;
    grid.nz=static_cast<std::size_t>(
        std::ceil((g.length+2.0*margin)/cell))+3U;
    return grid;
}

GaussianCloud makeObservedMarkers() {
    GaussianCloud cloud;
    std::uint32_t id=1;
    // A 2 x 7 surface strip covering only the central 150 mm of the body.
    for(int row=0;row<2;++row) {
        const double x=(row==0?-0.015:0.015);
        for(int j=0;j<7;++j) {
            const double u=static_cast<double>(j)/6.0;
            GaussianSplat s;
            s.position={x,0.020,(u-0.5)*kObservedLength};
            s.logScale={std::log(0.003),std::log(0.003),std::log(0.003)};
            s.rotation={1.0,0.0,0.0,0.0};
            s.opacityLogit=4.0;
            s.id={771U,id++};
            cloud.splats.push_back(s);
        }
    }
    return cloud;
}

std::vector<PrescribedParticleTarget> boundaryTargets(
    const std::vector<MpmParticle>& particles,
    const std::vector<MpmParticle>& initial,
    const BodyGeometry& g,
    double displacement,
    double velocity) {

    std::vector<PrescribedParticleTarget> targets;
    const double lo=-0.5*g.length;
    const double hi=+0.5*g.length;
    const double eps=1.0e-10;
    for(std::size_t i=0;i<particles.size();++i) {
        const auto& r=initial[i].restPosition;
        if(std::abs(r.z-lo)<=eps) {
            targets.push_back({particles[i].id,r,{0,0,0},true});
        } else if(std::abs(r.z-hi)<=eps) {
            targets.push_back({
                particles[i].id,
                r+Vec3{displacement,0,0},
                {velocity,0,0},
                true
            });
        }
    }
    if(targets.empty()) throw std::runtime_error("support-mismatch probe created no boundary targets");
    return targets;
}

Trace simulate(double length,double young,double totalShear) {
    const BodyGeometry g=geometry(length);
    auto particles=makeParticles(g);
    const auto initial=particles;
    const auto grid=makeGrid(g,totalShear);
    const MpmMaterial material{kDensity,young,kPoisson};

    GaussianCloud cloud=makeObservedMarkers();
    const auto binding=vulkax::coupling::bindGaussianCloudToMpm(cloud,particles,24);
    if(binding.embedding.maximumAffineReproductionError>1.0e-8)
        throw std::runtime_error("support-mismatch marker embedding is not affine-exact enough");

    Trace trace;
    trace.markers.reserve(kSamples+1);
    trace.markers.push_back({});
    for(const auto& s:cloud.splats) trace.markers.back().push_back(s.position);
    trace.minimumJ=std::numeric_limits<double>::infinity();

    const std::size_t totalSubsteps=kSamples*kSubstepsPerSample;
    const double duration=static_cast<double>(totalSubsteps)*kDt;
    const double shearVelocity=totalShear/duration;

    for(std::size_t step=1;step<=totalSubsteps;++step) {
        const double alpha=static_cast<double>(step)/static_cast<double>(totalSubsteps);
        const double displacement=totalShear*alpha;
        const auto targets=boundaryTargets(
            particles,initial,g,displacement,shearVelocity);
        const auto ev=vulkax::research::stepMpmWithPrescribedParticles(
            particles,grid,material,kDt,targets,{0,0,0},MpmTransferScheme::APIC,0.0);
        trace.minimumJ=std::min(
            trace.minimumJ,ev.unconstrainedStep.minimumDeformationDeterminant);

        for(const auto& target:targets) {
            const auto it=std::find_if(
                particles.begin(),particles.end(),
                [&](const MpmParticle& p){return p.id==target.particleId;});
            if(it==particles.end()) throw std::runtime_error("boundary particle disappeared");
            trace.maximumBoundaryError=std::max(
                trace.maximumBoundaryError,distance(it->position,target.position));
        }

        if(step%kSubstepsPerSample==0U) {
            vulkax::coupling::updateGaussianCloudFromMpm(binding,particles,cloud);
            trace.markers.push_back({});
            for(const auto& s:cloud.splats) trace.markers.back().push_back(s.position);
        }
    }
    return trace;
}

double traceRms(const Trace& a,const Trace& b,std::size_t firstSample=1) {
    if(a.markers.size()!=b.markers.size()) throw std::runtime_error("trace sample count mismatch");
    double sum=0.0; std::size_t n=0;
    for(std::size_t f=firstSample;f<a.markers.size();++f) {
        if(a.markers[f].size()!=b.markers[f].size())
            throw std::runtime_error("trace marker count mismatch");
        for(std::size_t i=0;i<a.markers[f].size();++i) {
            const Vec3 d=a.markers[f][i]-b.markers[f][i];
            sum+=vulkax::math::dot(d,d);
            ++n;
        }
    }
    return std::sqrt(sum/std::max<std::size_t>(n,1U));
}

double targetEffect(const Trace& truth) {
    const auto& rest=truth.markers.front();
    const auto& last=truth.markers.back();
    double sum=0.0;
    for(std::size_t i=0;i<rest.size();++i) {
        const Vec3 d=last[i]-rest[i];
        sum+=vulkax::math::dot(d,d);
    }
    return std::sqrt(sum/static_cast<double>(rest.size()));
}

FitResult fitYoung(double length,const Trace& truth) {
    const std::array<double,9> candidates{
        40000.0,50000.0,60000.0,70000.0,80000.0,
        90000.0,100000.0,110000.0,120000.0
    };
    FitResult best{0.0,std::numeric_limits<double>::infinity()};
    for(const double young:candidates) {
        const auto pred=simulate(length,young,kTrainShear);
        const double e=traceRms(pred,truth);
        if(e<best.rms) best={young,e};
    }
    return best;
}

struct Row {
    std::string model;
    double bodyLength{};
    double fittedYoung{};
    double fitRms{};
    double fitRelative{};
    double targetRms{};
    double targetRelative{};
    double minimumJ{};
    double maximumBoundaryError{};
};

Row evaluateModel(std::string name,double length,const Trace& trainTruth,const Trace& targetTruth) {
    const auto fit=fitYoung(length,trainTruth);
    const auto pred=simulate(length,fit.young,kTargetShear);
    const double tr=traceRms(pred,targetTruth);
    return {
        std::move(name),
        length,
        fit.young,
        fit.rms,
        fit.rms/std::max(targetEffect(trainTruth),1.0e-15),
        tr,
        tr/std::max(targetEffect(targetTruth),1.0e-15),
        pred.minimumJ,
        pred.maximumBoundaryError
    };
}

void writeRow(std::ofstream& out,const Row& r) {
    out<<r.model<<','<<std::setprecision(17)
       <<r.bodyLength<<','<<r.fittedYoung<<','<<r.fitRms<<','<<r.fitRelative<<','
       <<r.targetRms<<','<<r.targetRelative<<','<<r.minimumJ<<','
       <<r.maximumBoundaryError<<",synthetic\n";
}

} // namespace

int main(int argc,char** argv) {
    const std::filesystem::path outDir =
        argc>1 ? std::filesystem::path(argv[1]) : "build/observation-support-mismatch";
    std::filesystem::create_directories(outDir);

    const auto trainTruth=simulate(kTruthLength,kTruthYoung,kTrainShear);
    const auto targetTruth=simulate(kTruthLength,kTruthYoung,kTargetShear);

    const Row correct=evaluateModel(
        "full_body_correct_support",kTruthLength,trainTruth,targetTruth);
    const Row truncated=evaluateModel(
        "marker_envelope_truncated_body",kObservedLength,trainTruth,targetTruth);

    std::ofstream out(outDir/"cases.csv");
    if(!out) throw std::runtime_error("cannot create observation-support probe CSV");
    out<<"model,body_length_m,fitted_young_pa,fit_rms_m,fit_relative_error,target_rms_m,"
          "target_relative_error,min_J,max_boundary_error_m,provenance\n";
    writeRow(out,correct);
    writeRow(out,truncated);

    const double fitRatio=truncated.fitRms/std::max(correct.fitRms,1.0e-15);
    const double targetRatio=truncated.targetRms/std::max(correct.targetRms,1.0e-15);

    std::ofstream meta(outDir/"summary.json");
    meta<<"{\n"
        <<"  \"schema\":\"vulkax.observation_support_mismatch_probe\",\n"
        <<"  \"version\":1,\n"
        <<"  \"provenance\":\"synthetic\",\n"
        <<"  \"truth_body_length_m\":"<<kTruthLength<<",\n"
        <<"  \"observed_marker_span_m\":"<<kObservedLength<<",\n"
        <<"  \"truth_young_pa\":"<<kTruthYoung<<",\n"
        <<"  \"train_shear_m\":"<<kTrainShear<<",\n"
        <<"  \"target_shear_m\":"<<kTargetShear<<",\n"
        <<"  \"fit_ratio_truncated_to_correct\":"<<fitRatio<<",\n"
        <<"  \"target_ratio_truncated_to_correct\":"<<targetRatio<<",\n"
        <<"  \"warning\":\"Synthetic positive-control only. The probe tests whether observation support/body support mismatch can be absorbed into a material refit; it is not a real-data causal attribution or novelty claim.\"\n"
        <<"}\n";

    std::cout<<std::setprecision(10)
             <<"CORRECT fitted_E="<<correct.fittedYoung
             <<" fit_m="<<correct.fitRms
             <<" fit_rel="<<correct.fitRelative
             <<" target_rel="<<correct.targetRelative<<"\n"
             <<"TRUNCATED fitted_E="<<truncated.fittedYoung
             <<" fit_m="<<truncated.fitRms
             <<" fit_rel="<<truncated.fitRelative
             <<" target_rel="<<truncated.targetRelative<<"\n"
             <<"FIT_RATIO="<<fitRatio<<" TARGET_RATIO="<<targetRatio<<"\n";
    return 0;
}
