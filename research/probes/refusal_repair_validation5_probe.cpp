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
#include <vector>

namespace {
using vulkax::gaussian::GaussianCloud;
using vulkax::math::Vec3;
using vulkax::research::NonlinearDeformableWorldSettings;
using vulkax::solvers::Matrix3;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmParticle;
using vulkax::solvers::MpmTransferScheme;

constexpr double kTruthDt=2.5e-5;
constexpr double kBaseFineDt=1.0e-4;
constexpr double kHorizon=0.0064;
constexpr double kNoise=4.0e-5;
constexpr double kNumericalBudget=0.05;
const std::array<double,5> kTimes{0.0009,0.0018,0.0031,0.0047,0.0062};
const std::array<std::size_t,2> kMarkers{9U,54U};

std::vector<MpmParticle> body(){
    std::vector<MpmParticle> ps;std::uint64_t id=1;constexpr double h=.12,v=h*h*h,rho=1000.;
    for(int z=0;z<4;++z)for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        MpmParticle p;p.id=id++;p.restPosition={(x-1.5)*h,(y-1.5)*h,(z-1.5)*h};
        p.position=p.restPosition;p.restVolume=v;p.mass=rho*v;ps.push_back(p);
    }return ps;
}
vulkax::gaussian::GaussianSplat splat(Vec3 p){
    vulkax::gaussian::GaussianSplat s;s.position=p;s.logScale={std::log(.06),std::log(.045),std::log(.035)};
    s.rotation={1.,0.,0.,0.};s.opacityLogit=4.;return s;
}
GaussianCloud world(){
    GaussianCloud w;w.splats.push_back(splat({-.08,.04,.02}));w.splats.push_back(splat({.09,-.06,.03}));
    w.splats.push_back(splat({.04,.10,-.07}));w.splats.push_back(splat({-.05,-.08,-.06}));
    w.splats.push_back(splat({.76,.55,-.30}));return w;
}
MpmGridSettings grid(){MpmGridSettings g;g.origin={-.8,-.8,-.8};g.nx=22;g.ny=22;g.nz=22;g.cellSize=.08;g.boundaryCells=0;return g;}
Vec3 mul(const Matrix3&m,Vec3 q){return {m[0]*q.x+m[1]*q.y+m[2]*q.z,m[3]*q.x+m[4]*q.y+m[5]*q.z,m[6]*q.x+m[7]*q.y+m[8]*q.z};}

struct Trace{double dt{};std::vector<std::vector<Vec3>> p;};
Trace sim(double E,double nu,const Matrix3&d,double dt,MpmTransferScheme scheme){
    const auto steps=static_cast<std::size_t>(std::llround(kHorizon/dt));
    NonlinearDeformableWorldSettings s;s.steps=steps;s.dt=dt;s.material={1000.,E,nu};
    s.initialDeformation=d;s.couplingNeighborCount=20;s.transferScheme=scheme;
    Trace t;t.dt=dt;t.p.resize(steps+1);const auto b=body();for(const auto&q:b)t.p[0].push_back(mul(d,q.restPosition));
    (void)vulkax::research::runNonlinearDeformableWorld(world(),{0,1,2,3},body(),grid(),s,{},
      [&](const auto&f,const GaussianCloud&,const std::vector<MpmParticle>&ps){for(const auto&q:ps)t.p.at(f.step).push_back(q.position);});
    return t;
}
std::vector<double> obs(const Trace&t,std::size_t a,std::size_t b){
    std::vector<double> y;for(std::size_t ti=a;ti<b;++ti){
        const auto step=static_cast<std::size_t>(std::llround(kTimes[ti]/t.dt));
        for(auto m:kMarkers){const auto&q=t.p.at(step).at(m);y.push_back(q.x);y.push_back(q.y);y.push_back(q.z);}
    }return y;
}
std::vector<double> noisy(std::vector<double> y,int salt){
    for(std::size_t i=0;i<y.size();++i){const double q=.947*(i+1)+1.611*(salt+1);
        y[i]+=kNoise*(.58*std::sin(q)+.42*std::cos(1.417*q));}return y;
}
double rms(const std::vector<double>&a,const std::vector<double>&b){
    double s=0.;for(std::size_t i=0;i<a.size();++i){const double d=a[i]-b[i];s+=d*d;}return std::sqrt(s/a.size());
}
std::vector<double> initialObs(const Matrix3&d){
    const auto b=body();std::vector<double> y;for(std::size_t ti=0;ti<5;++ti)for(auto m:kMarkers){
        const auto q=mul(d,b.at(m).restPosition);y.push_back(q.x);y.push_back(q.y);y.push_back(q.z);}return y;
}
struct Spectrum{double smin{},cond{};};
Spectrum spectrum(double E,double nu,const Matrix3&d,double dt,MpmTransferScheme scheme){
    constexpr double re=.01,dn=.01;
    const auto ep=obs(sim(E*(1+re),nu,d,dt,scheme),0,5),em=obs(sim(E*(1-re),nu,d,dt,scheme),0,5);
    const auto np=obs(sim(E,nu+dn,d,dt,scheme),0,5),nm=obs(sim(E,nu-dn,d,dt,scheme),0,5);
    double aa=0,ab=0,bb=0;for(std::size_t i=0;i<ep.size();++i){
        const double x=(ep[i]-em[i])/(2*re),z=((np[i]-nm[i])/(2*dn))*.1;aa+=x*x;ab+=x*z;bb+=z*z;}
    const double tr=aa+bb,disc=std::sqrt(std::max(0.,(aa-bb)*(aa-bb)+4*ab*ab));
    const double lmax=.5*(tr+disc),lmin=std::max(0.,.5*(tr-disc));
    const double smax=std::sqrt(lmax),smin=std::sqrt(lmin);return {smin,smin>1e-18?smax/smin:1e30};
}
enum class V{Fine,Coarse,PIC,Constrained};
std::string vname(V v){switch(v){case V::Fine:return"fine_apic";case V::Coarse:return"coarse_apic";case V::PIC:return"pic";case V::Constrained:return"constrained_nu";}return"unknown";}
struct Fit{double E{},nu{},dt{},fit{},held{},ev{},obj{};MpmTransferScheme scheme{MpmTransferScheme::APIC};V variant{V::Fine};};
const std::array<double,5> Es{12000,13500,15000,16500,18000};
const std::array<double,5> Nus{.20,.25,.30,.35,.40};
Fit search(V v,const Matrix3&train,const std::vector<double>&fo,const std::vector<double>&ho,
           const Matrix3*ed=nullptr,const std::vector<double>*eo=nullptr,double forcedDt=0.0){
    Fit best;best.variant=v;best.dt=forcedDt>0.0?forcedDt:(v==V::Coarse?4e-4:kBaseFineDt);
    best.scheme=v==V::PIC?MpmTransferScheme::PIC:MpmTransferScheme::APIC;best.obj=1e300;
    for(double E:Es){auto eval=[&](double nu){
        const auto tt=sim(E,nu,train,best.dt,best.scheme);const double fl=rms(obs(tt,0,2),fo),hl=rms(obs(tt,2,5),ho);
        double ev=0,obj=fl;if(ed&&eo){ev=rms(obs(sim(E,nu,*ed,best.dt,best.scheme),0,5),*eo);obj=std::sqrt(.5*(fl*fl+ev*ev));}
        if(obj<best.obj)best={E,nu,best.dt,fl,hl,ev,obj,best.scheme,v};};
      if(v==V::Constrained)eval(.10);else for(double nu:Nus)eval(nu);}
    return best;
}
struct Intv{std::string name;Matrix3 d;};

struct RefinedPrediction{
    std::vector<double> prediction;
    double finalDt{};
    double numericalFraction{};
    int refinements{};
    bool converged{};
};
RefinedPrediction refineTarget(const Fit&f,const Matrix3&t){
    double dt=f.dt;
    auto coarse=obs(sim(f.E,f.nu,t,dt,f.scheme),0,5);
    RefinedPrediction out;out.prediction=coarse;out.finalDt=dt;out.numericalFraction=1e30;out.refinements=0;out.converged=false;
    for(int level=1;level<=3;++level){
        const double fineDt=dt*.5;
        auto fine=obs(sim(f.E,f.nu,t,fineDt,f.scheme),0,5);
        const double effect=rms(fine,initialObs(t));
        const double frac=rms(coarse,fine)/std::max(effect,1e-15);
        out.prediction=fine;out.finalDt=fineDt;out.numericalFraction=frac;out.refinements=level;
        if(frac<=kNumericalBudget){out.converged=true;return out;}
        coarse=std::move(fine);dt=fineDt;
    }
    return out;
}
struct RefitPrediction{
    Fit fit;
    std::vector<double> prediction;
    double finalDt{};
    double numericalFraction{1e30};
    int refinements{};
    int consecutiveStable{};
    bool converged{};
};

RefitPrediction refineRefitTarget(
    V v,const Matrix3&train,const std::vector<double>&fo,const std::vector<double>&ho,
    const Matrix3&ed,const std::vector<double>&eo,const Matrix3&t) {
    double dt=v==V::Coarse?4e-4:kBaseFineDt;
    Fit coarseFit=search(v,train,fo,ho,&ed,&eo,dt);
    auto coarse=obs(sim(coarseFit.E,coarseFit.nu,t,dt,coarseFit.scheme),0,5);
    RefitPrediction out;out.fit=coarseFit;out.prediction=coarse;out.finalDt=dt;
    int stable=0;
    for(int level=1;level<=3;++level){
        const double fineDt=dt*.5;
        Fit fineFit=search(v,train,fo,ho,&ed,&eo,fineDt);
        auto fine=obs(sim(fineFit.E,fineFit.nu,t,fineDt,fineFit.scheme),0,5);
        const double effect=rms(fine,initialObs(t));
        const double frac=rms(coarse,fine)/std::max(effect,1e-15);
        if(fineDt<=kBaseFineDt && frac<=kNumericalBudget) ++stable; else stable=0;
        out.fit=fineFit;out.prediction=fine;out.finalDt=fineDt;out.numericalFraction=frac;
        out.refinements=level;out.consecutiveStable=stable;
        if(stable>=2){out.converged=true;return out;}
        coarseFit=std::move(fineFit);coarse=std::move(fine);dt=fineDt;
    }
    return out;
}

double schemeFraction(const Fit&f,const Matrix3&t,double dt,const std::vector<double>&candidate){
    const auto alt=f.scheme==MpmTransferScheme::PIC?MpmTransferScheme::APIC:MpmTransferScheme::PIC;
    const auto b=obs(sim(f.E,f.nu,t,dt,alt),0,5);
    return rms(candidate,b)/std::max(rms(candidate,initialObs(t)),1e-15);
}
}
int main(int argc,char**argv){
    const std::filesystem::path dir=argc>1?argv[1]:"build/refusal-validation5";
    std::filesystem::create_directories(dir);std::ofstream out(dir/"cases.csv");
    out<<"case_id,truth_id,variant,selected_evidence,truth_E_pa,truth_nu,initial_E_pa,initial_nu,base_repaired_E_pa,base_repaired_nu,refit_E_pa,refit_nu,"
          "initial_fit_rms_m,initial_heldout_rms_m,base_fit_rms_m,base_heldout_rms_m,base_evidence_rms_m,"
          "refit_fit_rms_m,refit_heldout_rms_m,refit_evidence_rms_m,refit_evidence_noise_ratio,refit_heldout_noise_ratio,"
          "selected_smin,selected_condition,pre_target_relative_error,target_only_relative_error,refit_relative_error,"
          "base_dt_s,target_only_final_dt_s,target_only_refinements,target_only_fraction,target_only_converged,"
          "refit_final_dt_s,refit_refinements,refit_consecutive_stable,refit_fraction,refit_converged,scheme_fraction_final,"
          "target_only_accept,refit_accept,target_only_safe_10pct,refit_safe_10pct,provenance\n";
    const Matrix3 train{1.019,.024,.004,.014,.981,.015,.006,.012,.994};
    const std::array<Intv,4> evidence{{{"shear_d",{1.034,.076,.031,.029,.965,.024,.025,.047,1.009}},
      {"mixed_d",{1.049,.039,.069,.032,.949,.043,.039,.018,1.021}},
      {"triax_d",{1.067,.052,.019,.058,.941,.037,.021,.057,1.031}},
      {"skew_d",{1.089,.071,.044,.036,.918,.072,.048,.027,1.038}}}};
    const Matrix3 target{1.111,.142,.057,.076,.889,.087,.091,.049,1.054};
    const std::array<double,4> truthEs{12600,14100,15900,17400};
    const std::array<double,4> truthNus{.23,.28,.32,.37};
    const std::array<V,4> vars{V::Fine,V::Coarse,V::PIC,V::Constrained};
    int tid=0,cid=0;
    for(double E:truthEs)for(double nu:truthNus){
        ++tid;const auto tr=sim(E,nu,train,kTruthDt,MpmTransferScheme::APIC);
        const auto fo=noisy(obs(tr,0,2),5107+tid),ho=noisy(obs(tr,2,5),5311+tid);
        for(V v:vars){
            const auto before=search(v,train,fo,ho);const Intv*sel=&evidence[0];Spectrum ss{};double best=-1;
            for(const auto&c:evidence){const auto sp=spectrum(before.E,before.nu,c.d,before.dt,before.scheme);
                if(sp.smin>best){best=sp.smin;sel=&c;ss=sp;}}
            const auto evObs=noisy(obs(sim(E,nu,sel->d,kTruthDt,MpmTransferScheme::APIC),0,5),5521+tid*31+static_cast<int>(v));
            const auto after=search(v,train,fo,ho,&sel->d,&evObs);
            const auto truthTarget=obs(sim(E,nu,target,kTruthDt,MpmTransferScheme::APIC),0,5);
            const auto initial=initialObs(target);
            const auto prePred=obs(sim(before.E,before.nu,target,before.dt,before.scheme),0,5);
            const double denom=std::max(rms(truthTarget,initial),1e-15);
            const double pre=rms(prePred,truthTarget)/denom;

            const auto targetOnly=refineTarget(after,target);
            const double targetOnlyError=rms(targetOnly.prediction,truthTarget)/denom;
            const double baseEnr=after.ev/kNoise,baseHnr=after.held/kNoise;
            const bool targetOnlyAccept=baseEnr<=2.0 && baseHnr<=2.0 &&
                targetOnly.converged && targetOnly.numericalFraction<=kNumericalBudget;

            const auto refit=refineRefitTarget(v,train,fo,ho,sel->d,evObs,target);
            const double refitError=rms(refit.prediction,truthTarget)/denom;
            const double enr=refit.fit.ev/kNoise,hnr=refit.fit.held/kNoise;
            const double sch=schemeFraction(refit.fit,target,refit.finalDt,refit.prediction);
            const bool refitAccept=enr<=2.0 && hnr<=2.0 && refit.converged &&
                refit.consecutiveStable>=2 && refit.numericalFraction<=kNumericalBudget;
            ++cid;
            out<<cid<<','<<tid<<','<<vname(v)<<','<<sel->name<<','<<std::setprecision(17)
               <<E<<','<<nu<<','<<before.E<<','<<before.nu<<','<<after.E<<','<<after.nu<<','<<refit.fit.E<<','<<refit.fit.nu<<','
               <<before.fit<<','<<before.held<<','<<after.fit<<','<<after.held<<','<<after.ev<<','
               <<refit.fit.fit<<','<<refit.fit.held<<','<<refit.fit.ev<<','<<enr<<','<<hnr<<','
               <<ss.smin<<','<<ss.cond<<','<<pre<<','<<targetOnlyError<<','<<refitError<<','<<after.dt<<','
               <<targetOnly.finalDt<<','<<targetOnly.refinements<<','<<targetOnly.numericalFraction<<','<<(targetOnly.converged?1:0)<<','
               <<refit.finalDt<<','<<refit.refinements<<','<<refit.consecutiveStable<<','<<refit.numericalFraction<<','<<(refit.converged?1:0)<<','<<sch<<','
               <<(targetOnlyAccept?1:0)<<','<<(refitAccept?1:0)<<','<<(targetOnlyError<=.10?1:0)<<','<<(refitError<=.10?1:0)
               <<",synthetic-validation5\n";
        }
    }
    out.close();
    std::ofstream meta(dir/"summary.json");
    meta<<"{\n  \"schema\":\"vulkax.refusal_repair_validation5\",\n  \"version\":1,\n"
        <<"  \"provenance\":\"synthetic-validation5\",\n  \"case_count\":"<<cid<<",\n"
        <<"  \"truth_dt_s\":"<<kTruthDt<<",\n  \"base_fine_dt_s\":"<<kBaseFineDt<<",\n"
        <<"  \"measurement_noise_m\":"<<kNoise<<",\n  \"numerical_budget_fraction\":"<<kNumericalBudget<<",\n"
        <<"  \"policy\":\"same physical evidence; refit inverse parameters after each dt/2 refinement; require evidence<=2x noise, heldout<=2x noise, and two consecutive end-to-end target changes <=5% after reaching dt<=1e-4\",\n"
        <<"  \"warning\":\"Fresh domain with truth integrated at 2.5e-5 s. Validation-4 labels were not used to set any threshold or choose the domain.\"\n}\n";
    std::cout<<"generated_validation5_cases="<<cid<<"\n";
}
