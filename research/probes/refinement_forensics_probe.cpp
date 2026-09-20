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

constexpr double kHorizon=0.0064;
constexpr double kReferenceDt=1.25e-5;
const std::array<double,6> kDt{4e-4,2e-4,1e-4,5e-5,2.5e-5,1.25e-5};
const std::array<double,5> kTimes{0.0009,0.0018,0.0031,0.0047,0.0062};
const std::array<std::size_t,2> kMarkers{9U,54U};
const std::array<double,5> kEs{12000,13500,15000,16500,18000};
const std::array<double,5> kNus{.20,.25,.30,.35,.40};

std::vector<MpmParticle> body(){
    std::vector<MpmParticle> ps; std::uint64_t id=1;
    constexpr double h=.12,v=h*h*h,rho=1000.;
    for(int z=0;z<4;++z)for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        MpmParticle p; p.id=id++;
        p.restPosition={(x-1.5)*h,(y-1.5)*h,(z-1.5)*h};
        p.position=p.restPosition; p.mass=rho*v; p.restVolume=v; ps.push_back(p);
    }
    return ps;
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
    w.splats.push_back(splat({.76,.55,-.30})); return w;
}
MpmGridSettings grid(){
    MpmGridSettings g; g.origin={-.8,-.8,-.8}; g.nx=22; g.ny=22; g.nz=22;
    g.cellSize=.08; g.boundaryCells=0; return g;
}
Vec3 mul(const Matrix3&m,Vec3 q){
    return {m[0]*q.x+m[1]*q.y+m[2]*q.z,
            m[3]*q.x+m[4]*q.y+m[5]*q.z,
            m[6]*q.x+m[7]*q.y+m[8]*q.z};
}
struct Trace{double dt{};std::vector<std::vector<Vec3>> p;};
Trace sim(double E,double nu,const Matrix3&d,double dt){
    const auto steps=static_cast<std::size_t>(std::llround(kHorizon/dt));
    if(std::abs(static_cast<double>(steps)*dt-kHorizon)>1e-12)
        throw std::runtime_error("refinement dt must divide horizon");
    NonlinearDeformableWorldSettings s; s.steps=steps; s.dt=dt; s.material={1000.,E,nu};
    s.initialDeformation=d; s.couplingNeighborCount=20; s.transferScheme=MpmTransferScheme::APIC;
    Trace t; t.dt=dt; t.p.resize(steps+1); const auto b=body();
    for(const auto&q:b)t.p[0].push_back(mul(d,q.restPosition));
    (void)vulkax::research::runNonlinearDeformableWorld(
        world(),{0,1,2,3},body(),grid(),s,{},
        [&](const auto&f,const GaussianCloud&,const std::vector<MpmParticle>&ps){
            for(const auto&q:ps)t.p.at(f.step).push_back(q.position);
        });
    return t;
}
std::vector<double> obs(const Trace&t,std::size_t a=0,std::size_t b=5){
    std::vector<double> y;
    for(std::size_t ti=a;ti<b;++ti){
        const auto step=static_cast<std::size_t>(std::llround(kTimes[ti]/t.dt));
        for(auto m:kMarkers){
            const auto&q=t.p.at(step).at(m); y.push_back(q.x);y.push_back(q.y);y.push_back(q.z);
        }
    }
    return y;
}
std::vector<double> initialObs(const Matrix3&d){
    const auto b=body(); std::vector<double> y;
    for(std::size_t ti=0;ti<5;++ti)for(auto m:kMarkers){
        const auto q=mul(d,b.at(m).restPosition); y.push_back(q.x);y.push_back(q.y);y.push_back(q.z);
    }
    return y;
}
double rms(const std::vector<double>&a,const std::vector<double>&b){
    if(a.size()!=b.size()||a.empty())throw std::runtime_error("bad rms vectors");
    double s=0.; for(std::size_t i=0;i<a.size();++i){const double d=a[i]-b[i];s+=d*d;}
    return std::sqrt(s/static_cast<double>(a.size()));
}
struct Fit{double E{},nu{},obj{},fit{},evidence{},heldout{};};
Fit fitGrid(double dt,const Matrix3&train,const Matrix3&evidence,
            const std::vector<double>&fitObs,const std::vector<double>&heldObs,
            const std::vector<double>&evidenceObs){
    Fit best; best.obj=std::numeric_limits<double>::infinity();
    for(double E:kEs)for(double nu:kNus){
        const auto tr=sim(E,nu,train,dt);
        const double fr=rms(obs(tr,0,2),fitObs);
        const double hr=rms(obs(tr,2,5),heldObs);
        const double er=rms(obs(sim(E,nu,evidence,dt)),evidenceObs);
        const double objective=std::sqrt(.5*(fr*fr+er*er));
        if(objective<best.obj)best={E,nu,objective,fr,er,hr};
    }
    return best;
}
struct Case{const char* name;double E;double nu;};

} // namespace

int main(int argc,char**argv){
    const std::filesystem::path outDir=argc>1?argv[1]:"build/refinement-forensics";
    std::filesystem::create_directories(outDir);
    const Matrix3 train{1.019,.024,.004,.014,.981,.015,.006,.012,.994};
    const Matrix3 evidence{1.049,.039,.069,.032,.949,.043,.039,.018,1.021};
    const Matrix3 target{1.111,.142,.057,.076,.889,.087,.091,.049,1.054};
    const std::array<Case,2> cases{{{"on_grid",15000.,.30},{"off_grid",15900.,.32}}};

    std::ofstream csv(outDir/"refinement.csv");
    csv<<"case,dt_s,true_E_pa,true_nu,fixed_adjacent_fraction,fit_E_pa,fit_nu,fit_objective_m,"
          "fit_rms_m,heldout_rms_m,evidence_rms_m,refit_target_adjacent_fraction,"
          "refit_target_error_vs_finest_fixed,fit_E_error_pa,fit_nu_error\n";

    for(const auto&c:cases){
        const auto trainRef=sim(c.E,c.nu,train,kReferenceDt);
        const auto evidenceRef=sim(c.E,c.nu,evidence,kReferenceDt);
        const auto targetRef=obs(sim(c.E,c.nu,target,kReferenceDt));
        const auto fitObs=obs(trainRef,0,2);
        const auto heldObs=obs(trainRef,2,5);
        const auto evidenceObs=obs(evidenceRef);
        const auto init=initialObs(target);

        std::vector<double> prevFixed,prevRefit;
        for(double dt:kDt){
            const auto fixed=obs(sim(c.E,c.nu,target,dt));
            const double fixedEffect=rms(fixed,init);
            const double fixedFrac=prevFixed.empty()?std::numeric_limits<double>::quiet_NaN():
                rms(prevFixed,fixed)/std::max(fixedEffect,1e-15);

            const auto fit=fitGrid(dt,train,evidence,fitObs,heldObs,evidenceObs);
            const auto refit=obs(sim(fit.E,fit.nu,target,dt));
            const double refitEffect=rms(refit,init);
            const double refitFrac=prevRefit.empty()?std::numeric_limits<double>::quiet_NaN():
                rms(prevRefit,refit)/std::max(refitEffect,1e-15);
            const double targetErr=rms(refit,targetRef)/std::max(rms(targetRef,init),1e-15);

            csv<<c.name<<','<<std::setprecision(17)<<dt<<','<<c.E<<','<<c.nu<<','<<fixedFrac<<','
               <<fit.E<<','<<fit.nu<<','<<fit.obj<<','<<fit.fit<<','<<fit.heldout<<','<<fit.evidence<<','
               <<refitFrac<<','<<targetErr<<','<<(fit.E-c.E)<<','<<(fit.nu-c.nu)<<"\n";
            std::cout<<"REFINEMENT "<<c.name<<" dt="<<dt
                     <<" fixed_frac="<<fixedFrac
                     <<" fit=("<<fit.E<<","<<fit.nu<<")"
                     <<" refit_frac="<<refitFrac
                     <<" target_err="<<targetErr<<"\n";
            prevFixed=fixed; prevRefit=refit;
        }
    }
    csv.close();
    std::ofstream meta(outDir/"metadata.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.refinement_forensics\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic-label-free-numerical-diagnostic\",\n"
        <<"  \"reference_finest_dt_s\": "<<std::setprecision(17)<<kReferenceDt<<",\n"
        <<"  \"dt_ladder_s\": [0.0004,0.0002,0.0001,0.00005,0.000025,0.0000125],\n"
        <<"  \"fit_grid_E_pa\": [12000,13500,15000,16500,18000],\n"
        <<"  \"fit_grid_nu\": [0.20,0.25,0.30,0.35,0.40],\n"
        <<"  \"cases\": [\"on_grid\",\"off_grid\"],\n"
        <<"  \"warning\": \"No safety labels or validation outcomes are used. The finest evaluated trajectory is a numerical reference, not exact continuum truth.\"\n"
        <<"}\n";
    return 0;
}
