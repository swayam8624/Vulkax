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
#include <vector>

namespace {
using vulkax::gaussian::GaussianCloud;
using vulkax::math::Vec3;
using vulkax::research::NonlinearDeformableWorldSettings;
using vulkax::solvers::Matrix3;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmParticle;
using vulkax::solvers::MpmTransferScheme;
constexpr double kTruthDt=1.0e-4,kHorizon=0.008,kNoise=2.8e-5;
const std::array<double,5> kTimes{0.0015,0.0027,0.0043,0.0060,0.0077};
const std::array<std::size_t,2> kMarkers{9U,52U};

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
    NonlinearDeformableWorldSettings s;s.steps=steps;s.dt=dt;s.material={1000.,E,nu};s.initialDeformation=d;s.couplingNeighborCount=20;s.transferScheme=scheme;
    Trace t;t.dt=dt;t.p.resize(steps+1);const auto b=body();for(const auto&q:b)t.p[0].push_back(mul(d,q.restPosition));
    (void)vulkax::research::runNonlinearDeformableWorld(world(),{0,1,2,3},body(),grid(),s,{},
      [&](const auto&f,const GaussianCloud&,const std::vector<MpmParticle>&ps){for(const auto&q:ps)t.p.at(f.step).push_back(q.position);});
    return t;
}
std::vector<double> obs(const Trace&t,std::size_t a,std::size_t b){
    std::vector<double> y;for(std::size_t ti=a;ti<b;++ti){const auto step=static_cast<std::size_t>(std::llround(kTimes[ti]/t.dt));
      for(auto m:kMarkers){const auto&q=t.p.at(step).at(m);y.push_back(q.x);y.push_back(q.y);y.push_back(q.z);}}return y;
}
std::vector<double> noisy(std::vector<double> y,int salt){for(std::size_t i=0;i<y.size();++i){const double q=.917*(i+1)+1.419*(salt+1);y[i]+=kNoise*(.58*std::sin(q)+.42*std::cos(1.607*q));}return y;}
double rms(const std::vector<double>&a,const std::vector<double>&b){double s=0.;for(std::size_t i=0;i<a.size();++i){const double d=a[i]-b[i];s+=d*d;}return std::sqrt(s/a.size());}
std::vector<double> initialObs(const Matrix3&d){const auto b=body();std::vector<double> y;for(std::size_t ti=0;ti<5;++ti)for(auto m:kMarkers){const auto q=mul(d,b.at(m).restPosition);y.push_back(q.x);y.push_back(q.y);y.push_back(q.z);}return y;}
struct Spectrum{double smin{},cond{};};
Spectrum spectrum(double E,double nu,const Matrix3&d,double dt,MpmTransferScheme scheme){
    constexpr double re=.01,dn=.01;const auto ep=obs(sim(E*(1+re),nu,d,dt,scheme),0,5),em=obs(sim(E*(1-re),nu,d,dt,scheme),0,5);
    const auto np=obs(sim(E,nu+dn,d,dt,scheme),0,5),nm=obs(sim(E,nu-dn,d,dt,scheme),0,5);
    double aa=0,ab=0,bb=0;for(std::size_t i=0;i<ep.size();++i){const double x=(ep[i]-em[i])/(2*re),z=((np[i]-nm[i])/(2*dn))*.1;aa+=x*x;ab+=x*z;bb+=z*z;}
    const double tr=aa+bb,disc=std::sqrt(std::max(0.,(aa-bb)*(aa-bb)+4*ab*ab));const double lmax=.5*(tr+disc),lmin=std::max(0.,.5*(tr-disc));
    const double smax=std::sqrt(lmax),smin=std::sqrt(lmin);return {smin,smin>1e-18?smax/smin:1e30};
}
enum class V{Fine,Coarse,PIC,Constrained};
std::string vname(V v){switch(v){case V::Fine:return"fine_apic";case V::Coarse:return"coarse_apic";case V::PIC:return"pic";case V::Constrained:return"constrained_nu";}return"unknown";}
struct Fit{double E{},nu{},dt{},fit{},held{},ev{},obj{};MpmTransferScheme scheme{MpmTransferScheme::APIC};V variant{V::Fine};};
const std::array<double,5> Es{12000,13500,15000,16500,18000};
const std::array<double,5> Nus{.20,.25,.30,.35,.40};
Fit search(V v,const Matrix3&train,const std::vector<double>&fo,const std::vector<double>&ho,const Matrix3*ed=nullptr,const std::vector<double>*eo=nullptr){
    Fit best;best.variant=v;best.dt=v==V::Coarse?4e-4:kTruthDt;best.scheme=v==V::PIC?MpmTransferScheme::PIC:MpmTransferScheme::APIC;best.obj=1e300;
    for(double E:Es){auto eval=[&](double nu){const auto tt=sim(E,nu,train,best.dt,best.scheme);const double fl=rms(obs(tt,0,2),fo),hl=rms(obs(tt,2,5),ho);
        double ev=0,obj=fl;if(ed&&eo){ev=rms(obs(sim(E,nu,*ed,best.dt,best.scheme),0,5),*eo);obj=std::sqrt(.5*(fl*fl+ev*ev));}
        if(obj<best.obj)best={E,nu,best.dt,fl,hl,ev,obj,best.scheme,v};};
      if(v==V::Constrained)eval(.10);else for(double nu:Nus)eval(nu);}
    return best;
}
struct Intv{std::string name;Matrix3 d;};
double targetRel(const Fit&f,const Matrix3&t,double E,double nu){
    const auto truth=obs(sim(E,nu,t,kTruthDt,MpmTransferScheme::APIC),0,5),pred=obs(sim(f.E,f.nu,t,f.dt,f.scheme),0,5);
    return rms(pred,truth)/std::max(rms(truth,initialObs(t)),1e-15);
}
struct NumConv{double coarse{},fine{},ratio{};};
NumConv numConv(const Fit&f,const Matrix3&t){
    const auto a=obs(sim(f.E,f.nu,t,f.dt,f.scheme),0,5);
    const auto b=obs(sim(f.E,f.nu,t,.5*f.dt,f.scheme),0,5);
    const auto c=obs(sim(f.E,f.nu,t,.25*f.dt,f.scheme),0,5);
    const double effect=std::max(rms(a,initialObs(t)),1e-15);
    const double coarse=rms(a,b)/effect,fine=rms(b,c)/effect;
    return {coarse,fine,fine/std::max(coarse,1e-15)};
}
double schFrac(const Fit&f,const Matrix3&t){
    const auto a=obs(sim(f.E,f.nu,t,f.dt,f.scheme),0,5);const auto alt=f.scheme==MpmTransferScheme::PIC?MpmTransferScheme::APIC:MpmTransferScheme::PIC;
    const auto b=obs(sim(f.E,f.nu,t,f.dt,alt),0,5);return rms(a,b)/std::max(rms(a,initialObs(t)),1e-15);
}
}
int main(int argc,char**argv){
    const std::filesystem::path dir=argc>1?argv[1]:"build/refusal-validation4";std::filesystem::create_directories(dir);std::ofstream out(dir/"cases.csv");
    out<<"case_id,truth_id,variant,selected_evidence,truth_E_pa,truth_nu,initial_E_pa,initial_nu,repaired_E_pa,repaired_nu,initial_fit_rms_m,initial_heldout_rms_m,repaired_fit_rms_m,repaired_heldout_rms_m,evidence_rms_m,evidence_noise_ratio,heldout_noise_ratio,selected_smin,selected_condition,pre_target_relative_error,post_target_relative_error,numerical_fraction,numerical_fine_fraction,numerical_convergence_ratio,scheme_fraction,post_safe_10pct,provenance\n";
    const Matrix3 train{1.018,.019,.004,.012,.984,.009,.005,.015,.989};
    const std::array<Intv,4> evidence{{{"cross_shear4",{1.024,.054,.033,.041,.978,.018,.026,.047,1.006}},
      {"biax4",{1.061,.029,.014,.036,.951,.049,.019,.028,1.022}},
      {"skew4",{1.073,.016,.061,.028,.943,.038,.052,.013,1.017}},
      {"offaxis4",{1.038,.079,.021,.063,.966,.029,.017,.058,1.009}}}};
    const Matrix3 target{1.087,.104,.041,.068,.922,.052,.057,.036,1.031};
    const std::array<double,4> truthEs{12900,14400,15900,17400};const std::array<double,4> truthNus{.2375,.2875,.3375,.4125};
    const std::array<V,4> vars{V::Fine,V::Coarse,V::PIC,V::Constrained};int tid=0,cid=0;
    for(double E:truthEs)for(double nu:truthNus){++tid;const auto tr=sim(E,nu,train,kTruthDt,MpmTransferScheme::APIC);const auto fo=noisy(obs(tr,0,2),3701+tid),ho=noisy(obs(tr,2,5),3901+tid);
      for(V v:vars){const auto before=search(v,train,fo,ho);const Intv*sel=&evidence[0];Spectrum ss{};double best=-1;
        for(const auto&c:evidence){const auto sp=spectrum(before.E,before.nu,c.d,before.dt,before.scheme);if(sp.smin>best){best=sp.smin;sel=&c;ss=sp;}}
        const auto evObs=noisy(obs(sim(E,nu,sel->d,kTruthDt,MpmTransferScheme::APIC),0,5),4101+tid*29+static_cast<int>(v));
        const auto after=search(v,train,fo,ho,&sel->d,&evObs);const double pre=targetRel(before,target,E,nu),post=targetRel(after,target,E,nu);
        const auto nc=numConv(after,target);
        ++cid;out<<cid<<','<<tid<<','<<vname(v)<<','<<sel->name<<','<<std::setprecision(17)<<E<<','<<nu<<','<<before.E<<','<<before.nu<<','<<after.E<<','<<after.nu<<','<<before.fit<<','<<before.held<<','<<after.fit<<','<<after.held<<','<<after.ev<<','<<after.ev/kNoise<<','<<after.held/kNoise<<','<<ss.smin<<','<<ss.cond<<','<<pre<<','<<post<<','<<nc.coarse<<','<<nc.fine<<','<<nc.ratio<<','<<schFrac(after,target)<<','<<(post<=.10?1:0)<<",synthetic-validation4\n";
      }}
    out.close();std::ofstream meta(dir/"summary.json");meta<<"{\"schema\":\"vulkax.refusal_repair_validation4\",\"version\":1,\"provenance\":\"synthetic-validation3\",\"case_count\":"<<cid<<",\"measurement_noise_m\":"<<std::setprecision(17)<<kNoise<<",\"truth_grid\":\"fresh Validation-4 off-grid values\",\"interventions\":\"fresh train/evidence/target domain; no Validation-1/2/3 labels used\"}\n";std::cout<<"generated_validation4_cases="<<cid<<"\n";
}
