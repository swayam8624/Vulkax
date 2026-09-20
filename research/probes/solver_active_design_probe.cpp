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

std::vector<MpmParticle> makeBody() {
    std::vector<MpmParticle> particles;
    std::uint64_t id=1;
    constexpr double spacing=0.12, restVolume=spacing*spacing*spacing, density=1000.0;
    for(int iz=0;iz<4;++iz) for(int iy=0;iy<4;++iy) for(int ix=0;ix<4;++ix) {
        MpmParticle p;
        p.id=id++;
        p.restPosition={(static_cast<double>(ix)-1.5)*spacing,
                        (static_cast<double>(iy)-1.5)*spacing,
                        (static_cast<double>(iz)-1.5)*spacing};
        p.position=p.restPosition; p.restVolume=restVolume; p.mass=density*restVolume;
        particles.push_back(p);
    }
    return particles;
}

vulkax::gaussian::GaussianSplat splat(Vec3 p) {
    vulkax::gaussian::GaussianSplat s;
    s.position=p; s.logScale={std::log(0.06),std::log(0.045),std::log(0.035)};
    s.rotation={1.0,0.0,0.0,0.0}; s.opacityLogit=4.0; return s;
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
    MpmGridSettings g; g.origin={-0.80,-0.80,-0.80}; g.nx=22; g.ny=22; g.nz=22;
    g.cellSize=0.08; g.boundaryCells=0; return g;
}

struct Trace { std::vector<std::vector<Vec3>> positions; };

Trace simulate(double young,double poisson,const Matrix3& deformation) {
    constexpr std::size_t steps=30;
    NonlinearDeformableWorldSettings s;
    s.steps=steps; s.dt=2.0e-4; s.material={1000.0,young,poisson};
    s.initialDeformation=deformation; s.couplingNeighborCount=20;
    Trace t; t.positions.resize(steps+1U);
    (void)vulkax::research::runNonlinearDeformableWorld(
        makeWorld(),{0,1,2,3},makeBody(),makeGrid(),s,{},
        [&](const auto& frame,const GaussianCloud&,const std::vector<MpmParticle>& particles) {
            auto& dst=t.positions.at(frame.step); dst.reserve(particles.size());
            for(const auto& p:particles) dst.push_back(p.position);
        });
    return t;
}

std::vector<double> observe(const Trace& t) {
    const std::array<std::size_t,3> steps{10,20,30};
    const std::array<std::size_t,2> markers{15,63};
    std::vector<double> y; y.reserve(18);
    for(const auto step:steps) for(const auto marker:markers) {
        const auto& p=t.positions.at(step).at(marker);
        y.push_back(p.x); y.push_back(p.y); y.push_back(p.z);
    }
    return y;
}

struct Spectrum { double smax{}; double smin{}; double condition{}; double determinant{}; };

Spectrum sensitivitySpectrum(double E,double nu,const Matrix3& deformation) {
    constexpr double relE=0.01;
    constexpr double dNu=0.01;
    const auto ep=observe(simulate(E*(1.0+relE),nu,deformation));
    const auto em=observe(simulate(E*(1.0-relE),nu,deformation));
    const auto np=observe(simulate(E,nu+dNu,deformation));
    const auto nm=observe(simulate(E,nu-dNu,deformation));
    double aa=0.0,ab=0.0,bb=0.0;
    for(std::size_t i=0;i<ep.size();++i) {
        // Dimensionless parameter directions: d/d(log E), and change of nu by 0.1.
        const double a=(ep[i]-em[i])/(2.0*relE);
        const double b=((np[i]-nm[i])/(2.0*dNu))*0.1;
        aa+=a*a; ab+=a*b; bb+=b*b;
    }
    const double trace=aa+bb;
    const double disc=std::sqrt(std::max(0.0,(aa-bb)*(aa-bb)+4.0*ab*ab));
    const double lmax=0.5*(trace+disc);
    const double lmin=std::max(0.0,0.5*(trace-disc));
    Spectrum out;
    out.smax=std::sqrt(lmax); out.smin=std::sqrt(lmin);
    out.condition=out.smin>1.0e-18 ? out.smax/out.smin : std::numeric_limits<double>::infinity();
    out.determinant=aa*bb-ab*ab;
    return out;
}

double noisyScore(const Trace& candidate,const Trace& truth,int salt) {
    const auto c=observe(candidate), t=observe(truth);
    constexpr double noise=2.0e-5;
    double sum=0.0;
    for(std::size_t i=0;i<c.size();++i) {
        const double phase=0.73*static_cast<double>(i+1)+1.17*static_cast<double>(salt);
        const double measured=t[i]+noise*(0.7*std::sin(phase)+0.3*std::cos(1.9*phase));
        const double d=c[i]-measured; sum+=d*d;
    }
    return std::sqrt(sum/static_cast<double>(c.size()));
}

struct Candidate { double E{}; double nu{}; double trainScore{}; double designedScore{}; };

void write(std::ofstream& out,const std::string& intervention,const std::string& metric,double value) {
    out<<intervention<<','<<metric<<','<<std::setprecision(17)<<value<<",synthetic\n";
}

} // namespace

int main(int argc,char** argv) {
    const std::filesystem::path outDir=argc>1?std::filesystem::path(argv[1]):"build/solver-design";
    std::filesystem::create_directories(outDir);
    std::ofstream out(outDir/"solver_design.csv");
    if(!out) throw std::runtime_error("cannot create solver-design CSV");
    out<<"intervention,metric,value,provenance\n";

    constexpr double truthE=15000.0, truthNu=0.35;
    const Matrix3 train{
        1.030,0.008,0.000,
        0.000,0.985,0.003,
        0.000,0.000,0.985
    };
    const std::vector<std::pair<std::string,Matrix3>> designs{
        {"training_repeat",train},
        {"xy_shear",{1.0,0.09,0.0, 0.02,1.0,0.0, 0.0,0.0,1.0}},
        {"xz_shear",{1.0,0.0,0.09, 0.0,1.0,0.0, 0.02,0.0,1.0}},
        {"isotropic_compression",{0.965,0.0,0.0, 0.0,0.965,0.0, 0.0,0.0,0.965}},
        {"isotropic_expansion",{1.035,0.0,0.0, 0.0,1.035,0.0, 0.0,0.0,1.035}},
        {"mixed",{1.06,0.06,0.01, 0.01,0.95,0.05, 0.0,0.02,1.015}}
    };

    std::string bestName;
    Matrix3 bestDef{};
    double bestSmin=-1.0;
    for(const auto& [name,def]:designs) {
        const auto s=sensitivitySpectrum(truthE,truthNu,def);
        write(out,name,"singular_max",s.smax);
        write(out,name,"singular_min",s.smin);
        write(out,name,"condition_number",s.condition);
        write(out,name,"fisher_det_proxy",s.determinant);
        if(s.smin>bestSmin) { bestSmin=s.smin; bestName=name; bestDef=def; }
    }

    const Trace truthTrain=simulate(truthE,truthNu,train);
    const Trace truthDesigned=simulate(truthE,truthNu,bestDef);

    Candidate trainBest{0,0,std::numeric_limits<double>::infinity(),0};
    Candidate combinedBest{0,0,0,std::numeric_limits<double>::infinity()};
    for(int e=0;e<=12;++e) {
        const double E=12000.0+500.0*static_cast<double>(e);
        for(int n=0;n<=12;++n) {
            const double nu=0.15+0.025*static_cast<double>(n);
            const Trace trainCandidate=simulate(E,nu,train);
            const double trainScore=noisyScore(trainCandidate,truthTrain,1);
            if(trainScore<trainBest.trainScore) trainBest={E,nu,trainScore,0.0};

            const Trace designCandidate=simulate(E,nu,bestDef);
            const double designScore=noisyScore(designCandidate,truthDesigned,2);
            const double combined=std::sqrt(0.5*(trainScore*trainScore+designScore*designScore));
            if(combined<combinedBest.designedScore)
                combinedBest={E,nu,trainScore,combined};
        }
    }

    write(out,"selected","selected_singular_min",bestSmin);
    // Stable numeric ID for CI parsing; the human-readable name is printed and recorded in summary JSON.
    write(out,"train_only_fit","fitted_E_pa",trainBest.E);
    write(out,"train_only_fit","fitted_nu",trainBest.nu);
    write(out,"train_only_fit","relative_E_error",std::abs(trainBest.E-truthE)/truthE);
    write(out,"train_only_fit","absolute_nu_error",std::abs(trainBest.nu-truthNu));
    write(out,"train_only_fit","noisy_fit_rms_m",trainBest.trainScore);
    write(out,"train_plus_design_fit","fitted_E_pa",combinedBest.E);
    write(out,"train_plus_design_fit","fitted_nu",combinedBest.nu);
    write(out,"train_plus_design_fit","relative_E_error",std::abs(combinedBest.E-truthE)/truthE);
    write(out,"train_plus_design_fit","absolute_nu_error",std::abs(combinedBest.nu-truthNu));
    write(out,"train_plus_design_fit","combined_noisy_rms_m",combinedBest.designedScore);
    out.close();

    std::ofstream meta(outDir/"summary.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.solver_active_design_probe\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic\",\n"
        <<"  \"selected_intervention\": \""<<bestName<<"\",\n"
        <<"  \"warning\": \"Optimal-design positive probe; generic sensitivity/Fisher experiment design is prior art\"\n"
        <<"}\n";

    std::cout<<std::setprecision(10)
             <<"selected_intervention="<<bestName<<"\n"
             <<"selected_singular_min="<<bestSmin<<"\n"
             <<"train_only_E="<<trainBest.E<<" train_only_nu="<<trainBest.nu<<"\n"
             <<"train_plus_design_E="<<combinedBest.E<<" train_plus_design_nu="<<combinedBest.nu<<"\n";
    return 0;
}
