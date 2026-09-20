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
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using vulkax::gaussian::GaussianCloud;
using vulkax::gaussian::GaussianSplat;
using vulkax::math::Vec3;
using vulkax::research::PrescribedParticleTarget;
using vulkax::solvers::MpmConstitutiveModel;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmMaterial;
using vulkax::solvers::MpmParticle;
using vulkax::solvers::MpmTransferScheme;

struct Marker {
    std::string id;
    Vec3 p{};
};
struct DriverSample {
    std::size_t frame{};
    double time{};
    Vec3 d{};
};
struct Geometry {
    int longAxis{};
    std::array<double,3> lo{};
    std::array<double,3> hi{};
    std::array<double,3> prismLo{};
    std::array<double,3> prismHi{};
    double longSpan{};
    double volume{};
    double crossA{};
    double crossB{};
};
struct RunEvidence {
    double minimumJ{std::numeric_limits<double>::infinity()};
    double maximumMomentumAccountingError{};
    double maximumPositionCorrection{};
    double accumulatedConstraintImpulseMagnitude{};
    double requestedDt{};
    double maximumActualDt{};
    std::size_t totalSubsteps{};
    std::size_t particles{};
    std::size_t minimumPrescribedParticles{std::numeric_limits<std::size_t>::max()};
    std::size_t maximumPrescribedParticles{};
    std::array<std::size_t,3> gridDims{};
    double gridCellSize{};
};

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while(std::getline(ss,item,',')) out.push_back(item);
    return out;
}

double number(const std::string& s) {
    std::size_t used=0;
    const double x=std::stod(s,&used);
    if(used!=s.size() || !std::isfinite(x)) throw std::runtime_error("invalid numeric CSV field");
    return x;
}

std::vector<Marker> loadMarkers(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in) throw std::runtime_error("cannot open marker CSV");
    std::string line;
    if(!std::getline(in,line)) throw std::runtime_error("missing marker CSV header");
    if(!line.empty() && line.back()=='\r') line.pop_back();
    if(line!="marker_id,x_m,y_m,z_m")
        throw std::runtime_error("unexpected marker CSV header");
    std::vector<Marker> out;
    while(std::getline(in,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line.empty()) continue;
        const auto f=split(line);
        if(f.size()!=4) throw std::runtime_error("malformed marker CSV row");
        out.push_back({f[0],{number(f[1]),number(f[2]),number(f[3])}});
    }
    if(out.size()<8) throw std::runtime_error("forward probe requires at least eight measured markers");
    return out;
}

std::vector<DriverSample> loadDriver(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in) throw std::runtime_error("cannot open driver CSV");
    std::string line;
    if(!std::getline(in,line)) throw std::runtime_error("missing driver CSV header");
    if(!line.empty() && line.back()=='\r') line.pop_back();
    if(line!="frame,time_s,dx_m,dy_m,dz_m,magnitude_m")
        throw std::runtime_error("unexpected driver CSV header");
    std::vector<DriverSample> out;
    while(std::getline(in,line)) {
        if(line.empty()) continue;
        const auto f=split(line);
        if(f.size()!=6) throw std::runtime_error("malformed driver CSV row");
        const auto frame=static_cast<std::size_t>(std::stoull(f[0]));
        out.push_back({frame,number(f[1]),{number(f[2]),number(f[3]),number(f[4])}});
    }
    if(out.size()<2) throw std::runtime_error("forward probe requires a driver trajectory");
    if(vulkax::math::length(out.front().d)>1e-10)
        throw std::runtime_error("driver CSV must be relative to frame zero");
    for(std::size_t i=1;i<out.size();++i) {
        if(!(out[i].time>out[i-1].time) || out[i].frame!=out[i-1].frame+1U)
            throw std::runtime_error("driver trajectory must be strictly time-ordered and contiguous");
    }
    return out;
}

Geometry inferGeometry(const std::vector<Marker>& markers,double mass,double density,const std::string& geometryMode) {
    if(!(mass>0.0) || !(density>0.0)) throw std::runtime_error("mass and density must be positive");
    Geometry g;
    for(int a=0;a<3;++a) {
        g.lo[a]=std::numeric_limits<double>::infinity();
        g.hi[a]=-std::numeric_limits<double>::infinity();
    }
    for(const auto&m:markers) {
        const std::array<double,3> p{m.p.x,m.p.y,m.p.z};
        for(int a=0;a<3;++a){g.lo[a]=std::min(g.lo[a],p[a]);g.hi[a]=std::max(g.hi[a],p[a]);}
    }
    std::array<double,3> span{};
    for(int a=0;a<3;++a) span[a]=g.hi[a]-g.lo[a];
    g.longAxis=static_cast<int>(std::distance(span.begin(),std::max_element(span.begin(),span.end())));
    std::array<int,2> cross{};
    int c=0;
    for(int a=0;a<3;++a) if(a!=g.longAxis) cross[static_cast<std::size_t>(c++)]=a;
    g.volume=mass/density;

    if(geometryMode=="released_asset_aspect") {
        // GAUGE's released foam.obj bbox is 0.05 x 0.05 x 0.20 (1:1:4).
        // Use only that unit-free aspect here; absolute scale is independently
        // recovered from measured mass/density volume. The measured marker bbox
        // supplies orientation and a center proxy, but no trajectory error enters.
        const double crossSpan=std::cbrt(g.volume/4.0);
        g.crossA=crossSpan;
        g.crossB=crossSpan;
        g.longSpan=4.0*crossSpan;
        for(int a=0;a<3;++a) {
            const double center=0.5*(g.lo[a]+g.hi[a]);
            const double physicalSpan=(a==g.longAxis)?g.longSpan:crossSpan;
            g.prismLo[a]=center-0.5*physicalSpan;
            g.prismHi[a]=center+0.5*physicalSpan;
        }
        return g;
    }

    g.longSpan=span[static_cast<std::size_t>(g.longAxis)];
    if(!(g.longSpan>0.05)) throw std::runtime_error("marker long axis is unexpectedly short");
    const double s0=std::max(span[static_cast<std::size_t>(cross[0])],1e-6);
    const double s1=std::max(span[static_cast<std::size_t>(cross[1])],1e-6);
    const bool squareCrossSection=geometryMode=="square_cross";
    const double aspect=squareCrossSection?1.0:(s0/s1);
    const double area=g.volume/g.longSpan;
    g.crossA=std::sqrt(area*aspect);
    g.crossB=std::sqrt(area/aspect);

    for(int a=0;a<3;++a){g.prismLo[a]=g.lo[a];g.prismHi[a]=g.hi[a];}
    const double center0=0.5*(g.lo[static_cast<std::size_t>(cross[0])]+g.hi[static_cast<std::size_t>(cross[0])]);
    const double center1=0.5*(g.lo[static_cast<std::size_t>(cross[1])]+g.hi[static_cast<std::size_t>(cross[1])]);
    g.prismLo[static_cast<std::size_t>(cross[0])]=center0-0.5*g.crossA;
    g.prismHi[static_cast<std::size_t>(cross[0])]=center0+0.5*g.crossA;
    g.prismLo[static_cast<std::size_t>(cross[1])]=center1-0.5*g.crossB;
    g.prismHi[static_cast<std::size_t>(cross[1])]=center1+0.5*g.crossB;
    return g;
}

std::vector<MpmParticle> makeParticles(const Geometry& g,double mass,double density,int nCross,int nLong) {
    if(nCross<3 || nLong<3) throw std::runtime_error("particle resolution must be at least 3 per axis");
    std::array<int,3> n{nCross,nCross,nCross};
    n[static_cast<std::size_t>(g.longAxis)]=nLong;
    const std::size_t count=static_cast<std::size_t>(n[0]*n[1]*n[2]);
    std::vector<MpmParticle> ps;
    ps.reserve(count);
    const double restVolume=(mass/density)/static_cast<double>(count);
    const double particleMass=mass/static_cast<double>(count);
    std::uint64_t id=1;
    for(int iz=0;iz<n[2];++iz) for(int iy=0;iy<n[1];++iy) for(int ix=0;ix<n[0];++ix) {
        const std::array<int,3> q{ix,iy,iz};
        std::array<double,3> p{};
        for(int a=0;a<3;++a) {
            const int na=n[static_cast<std::size_t>(a)];
            const double u=na>1?static_cast<double>(q[static_cast<std::size_t>(a)])/static_cast<double>(na-1):0.5;
            p[static_cast<std::size_t>(a)]=g.prismLo[static_cast<std::size_t>(a)] +
                u*(g.prismHi[static_cast<std::size_t>(a)]-g.prismLo[static_cast<std::size_t>(a)]);
        }
        MpmParticle x;
        x.id=id++;
        x.restPosition={p[0],p[1],p[2]};
        x.position=x.restPosition;
        x.mass=particleMass;
        x.restVolume=restVolume;
        ps.push_back(x);
    }
    return ps;
}

GaussianCloud markerCloud(const std::vector<Marker>& markers) {
    GaussianCloud cloud;
    std::uint32_t local=1;
    for(const auto&m:markers) {
        GaussianSplat s;
        s.position=m.p;
        s.logScale={std::log(0.004),std::log(0.004),std::log(0.004)};
        s.rotation={1.0,0.0,0.0,0.0};
        s.opacityLogit=4.0;
        s.id={991U,local++};
        cloud.splats.push_back(s);
    }
    return cloud;
}

MpmGridSettings makeGrid(const Geometry& g,const std::vector<DriverSample>& driver,double cell) {
    std::array<double,3> dmin{0.0,0.0,0.0},dmax{0.0,0.0,0.0};
    for(const auto&s:driver) {
        const std::array<double,3> d{s.d.x,s.d.y,s.d.z};
        for(int a=0;a<3;++a){dmin[a]=std::min(dmin[a],d[a]);dmax[a]=std::max(dmax[a],d[a]);}
    }
    std::array<double,3> lo{},hi{};
    for(int a=0;a<3;++a) {
        lo[a]=g.prismLo[a]+std::min(0.0,dmin[a])-4.0*cell;
        hi[a]=g.prismHi[a]+std::max(0.0,dmax[a])+4.0*cell;
    }
    MpmGridSettings grid;
    grid.origin={lo[0],lo[1],lo[2]};
    grid.cellSize=cell;
    grid.boundaryCells=0;
    grid.nx=static_cast<std::size_t>(std::ceil((hi[0]-lo[0])/cell))+3U;
    grid.ny=static_cast<std::size_t>(std::ceil((hi[1]-lo[1])/cell))+3U;
    grid.nz=static_cast<std::size_t>(std::ceil((hi[2]-lo[2])/cell))+3U;
    if(grid.nx>80 || grid.ny>80 || grid.nz>80) throw std::runtime_error("forward grid unexpectedly large");
    return grid;
}

std::vector<PrescribedParticleTarget> targetsFor(
    const std::vector<MpmParticle>& particles,const Geometry& g,Vec3 driver,Vec3 velocity,
    int boundaryLayers,int nLong,double boundaryThicknessM) {
    std::vector<PrescribedParticleTarget> out;
    if(boundaryLayers<1 || boundaryLayers*2>=nLong)
        throw std::runtime_error("invalid prescribed boundary thickness");
    const auto axis=static_cast<std::size_t>(g.longAxis);
    const double lo=g.prismLo[axis],hi=g.prismHi[axis];
    const double spacing=(hi-lo)/static_cast<double>(nLong-1);
    double extent=(static_cast<double>(boundaryLayers)-0.5)*spacing;

    // Research-only metric support mode. A non-negative thickness represents a
    // physically interpretable support region measured from each end of the full
    // body. It is intentionally separate from the historical particle-layer mode
    // so held-out fixture-support experiments do not silently reinterpret old runs.
    if(boundaryThicknessM>=0.0) {
        if(!(boundaryThicknessM>0.0) || !(2.0*boundaryThicknessM < hi-lo))
            throw std::runtime_error("invalid metric boundary thickness");
        extent=boundaryThicknessM;
    }

    std::size_t lowerCount=0U,upperCount=0U;
    for(const auto&p:particles) {
        const std::array<double,3> r{p.restPosition.x,p.restPosition.y,p.restPosition.z};
        if(r[axis]<=lo+extent+1.0e-12) {
            out.push_back({p.id,p.restPosition,{0,0,0},true});
            ++lowerCount;
        } else if(r[axis]>=hi-extent-1.0e-12) {
            out.push_back({p.id,p.restPosition+driver,velocity,true});
            ++upperCount;
        }
    }
    if(lowerCount==0U || upperCount==0U)
        throw std::runtime_error("forward probe boundary support did not constrain both ends");
    return out;
}

MpmTransferScheme parseTransfer(const std::string& name) {
    if(name=="APIC") return MpmTransferScheme::APIC;
    if(name=="PIC") return MpmTransferScheme::PIC;
    if(name=="FLIP") return MpmTransferScheme::FLIP;
    throw std::runtime_error("unsupported GAUGE transfer scheme");
}

MpmConstitutiveModel parseConstitutive(const std::string& name) {
    if(name=="neo_hookean_log_j") return MpmConstitutiveModel::NeoHookeanLogJ;
    if(name=="neo_hookean_quadratic_j") return MpmConstitutiveModel::NeoHookeanQuadraticJ;
    if(name=="st_venant_kirchhoff") return MpmConstitutiveModel::StVenantKirchhoff;
    throw std::runtime_error("unsupported GAUGE constitutive model");
}

Vec3 parseGravity(const std::string& name) {
    if(name=="zero") return {0,0,0};
    if(name=="+x") return {9.81,0,0};
    if(name=="-x") return {-9.81,0,0};
    if(name=="+y") return {0,9.81,0};
    if(name=="-y") return {0,-9.81,0};
    if(name=="+z") return {0,0,9.81};
    if(name=="-z") return {0,0,-9.81};
    throw std::runtime_error("unsupported GAUGE gravity frame");
}

void writeFrame(std::ofstream& out,std::size_t frame,double time,
                const std::vector<Marker>& markers,const GaussianCloud& cloud) {
    if(markers.size()!=cloud.size()) throw std::runtime_error("marker/cloud count mismatch");
    for(std::size_t i=0;i<markers.size();++i) {
        const auto&p=cloud.splats[i].position;
        out<<frame<<','<<std::setprecision(17)<<time<<','<<markers[i].id<<','
           <<p.x<<','<<p.y<<','<<p.z<<"\n";
    }
}

} // namespace

int main(int argc,char**argv) {
    if(argc!=10 && argc!=16 && argc!=17 && argc!=18) {
        std::cerr<<"usage: vulkax_gauge_shearing_forward_probe markers.csv driver.csv output.csv "
                    "young_pa poisson density_kg_m3 mass_kg requested_dt label "
                    "[n_cross n_long boundary_layers transfer geometry_mode gravity [constitutive [boundary_thickness_m]]]\n";
        return 2;
    }
    const std::filesystem::path markerPath=argv[1],driverPath=argv[2],outPath=argv[3];
    const double young=number(argv[4]),poisson=number(argv[5]),density=number(argv[6]),mass=number(argv[7]);
    const double requestedDt=number(argv[8]);
    const std::string label=argv[9];
    const int nCross=argc>=16?std::stoi(argv[10]):5;
    const int nLong=argc>=16?std::stoi(argv[11]):13;
    const int boundaryLayers=argc>=16?std::stoi(argv[12]):1;
    const std::string transferName=argc>=16?argv[13]:"APIC";
    const std::string geometryMode=argc>=16?argv[14]:"measured_aspect";
    const std::string gravityName=argc>=16?argv[15]:"zero";
    const std::string constitutiveName=argc>=17?argv[16]:"neo_hookean_log_j";
    const double boundaryThicknessM=argc==18?number(argv[17]):-1.0;
    if(!(poisson>-1.0 && poisson<0.5) || !(requestedDt>0.0))
        throw std::runtime_error("invalid material/timestep argument");
    if(geometryMode!="measured_aspect" && geometryMode!="square_cross" && geometryMode!="released_asset_aspect")
        throw std::runtime_error("unsupported geometry mode");
    const auto transfer=parseTransfer(transferName);
    const auto gravity=parseGravity(gravityName);
    const auto constitutive=parseConstitutive(constitutiveName);

    const auto markers=loadMarkers(markerPath);
    const auto driver=loadDriver(driverPath);
    const auto geom=inferGeometry(markers,mass,density,geometryMode);
    auto particles=makeParticles(geom,mass,density,nCross,nLong);
    auto cloud=markerCloud(markers);
    const auto binding=vulkax::coupling::bindGaussianCloudToMpm(cloud,particles,24);

    const std::array<double,3> side{
        geom.prismHi[0]-geom.prismLo[0],
        geom.prismHi[1]-geom.prismLo[1],
        geom.prismHi[2]-geom.prismLo[2]};
    std::array<int,3> particleDims{nCross,nCross,nCross};
    particleDims[static_cast<std::size_t>(geom.longAxis)]=nLong;
    const double cell=std::min({
        side[0]/static_cast<double>(particleDims[0]-1),
        side[1]/static_cast<double>(particleDims[1]-1),
        side[2]/static_cast<double>(particleDims[2]-1)});
    const auto grid=makeGrid(geom,driver,cell);
    const MpmMaterial material{density,young,poisson,constitutive};

    std::filesystem::create_directories(outPath.parent_path());
    std::ofstream out(outPath);
    if(!out) throw std::runtime_error("cannot create forward marker output");
    out<<"frame,time_s,marker_id,x_m,y_m,z_m\n";
    writeFrame(out,0,driver.front().time,markers,cloud);

    RunEvidence evidence;
    evidence.requestedDt=requestedDt;
    evidence.particles=particles.size();
    evidence.gridDims={grid.nx,grid.ny,grid.nz};
    evidence.gridCellSize=cell;

    for(std::size_t f=0;f+1<driver.size();++f) {
        const double frameDt=driver[f+1].time-driver[f].time;
        const std::size_t substeps=static_cast<std::size_t>(std::ceil(frameDt/requestedDt));
        const double dt=frameDt/static_cast<double>(substeps);
        evidence.maximumActualDt=std::max(evidence.maximumActualDt,dt);
        evidence.totalSubsteps+=substeps;
        const Vec3 v=(driver[f+1].d-driver[f].d)/frameDt;
        for(std::size_t s=1;s<=substeps;++s) {
            const double alpha=static_cast<double>(s)/static_cast<double>(substeps);
            const Vec3 d=driver[f].d+(driver[f+1].d-driver[f].d)*alpha;
            const auto targets=targetsFor(particles,geom,d,v,boundaryLayers,nLong,boundaryThicknessM);
            evidence.minimumPrescribedParticles=std::min(evidence.minimumPrescribedParticles,targets.size());
            evidence.maximumPrescribedParticles=std::max(evidence.maximumPrescribedParticles,targets.size());
            const auto ev=vulkax::research::stepMpmWithPrescribedParticles(
                particles,grid,material,dt,targets,gravity,transfer,0.0);
            evidence.minimumJ=std::min(evidence.minimumJ,ev.unconstrainedStep.minimumDeformationDeterminant);
            evidence.maximumMomentumAccountingError=std::max(evidence.maximumMomentumAccountingError,ev.momentumAccountingError);
            evidence.maximumPositionCorrection=std::max(evidence.maximumPositionCorrection,ev.maximumPositionCorrection);
            evidence.accumulatedConstraintImpulseMagnitude+=ev.totalConstraintImpulseMagnitude;
            if(!(ev.unconstrainedStep.minimumDeformationDeterminant>0.0))
                throw std::runtime_error("GAUGE no-fit forward model inverted");
        }
        vulkax::coupling::updateGaussianCloudFromMpm(binding,particles,cloud);
        writeFrame(out,f+1,driver[f+1].time,markers,cloud);
    }
    out.close();

    const auto summaryPath=outPath.parent_path()/(outPath.stem().string()+"_summary.json");
    std::ofstream summary(summaryPath);
    summary<<"{\n"
           <<"  \"schema\": \"vulkax.gauge_shearing_forward\",\n"
           <<"  \"version\": 1,\n"
           <<"  \"provenance\": \"measured-input+model-prediction\",\n"
           <<"  \"label\": \""<<label<<"\",\n"
           <<"  \"young_pa\": "<<std::setprecision(17)<<young<<",\n"
           <<"  \"poisson\": "<<poisson<<",\n"
           <<"  \"density_kg_m3\": "<<density<<",\n"
           <<"  \"mass_kg\": "<<mass<<",\n"
           <<"  \"geometry_proxy\": \"mass/density volume + measured marker long span + "<<geometryMode<<"\",\n"
           <<"  \"gravity_mode\": \""<<gravityName<<"\",\n"
           <<"  \"gravity\": ["<<gravity.x<<','<<gravity.y<<','<<gravity.z<<"],\n"
           <<"  \"transfer\": \""<<transferName<<"\",\n"
           <<"  \"constitutive_model\": \""<<constitutiveName<<"\",\n"
           <<"  \"n_cross\": "<<nCross<<",\n"
           <<"  \"n_long\": "<<nLong<<",\n"
           <<"  \"boundary_layers\": "<<boundaryLayers<<",\n"
           <<"  \"boundary_mode\": \""<<(boundaryThicknessM>=0.0?"metric_thickness":"particle_layers")<<"\",\n"
           <<"  \"boundary_thickness_m\": "<<boundaryThicknessM<<",\n"
           <<"  \"minimum_prescribed_particles\": "<<evidence.minimumPrescribedParticles<<",\n"
           <<"  \"maximum_prescribed_particles\": "<<evidence.maximumPrescribedParticles<<",\n"
           <<"  \"particles\": "<<evidence.particles<<",\n"
           <<"  \"grid\": ["<<evidence.gridDims[0]<<','<<evidence.gridDims[1]<<','<<evidence.gridDims[2]<<"],\n"
           <<"  \"grid_cell_m\": "<<evidence.gridCellSize<<",\n"
           <<"  \"requested_dt_s\": "<<evidence.requestedDt<<",\n"
           <<"  \"maximum_actual_dt_s\": "<<evidence.maximumActualDt<<",\n"
           <<"  \"substeps\": "<<evidence.totalSubsteps<<",\n"
           <<"  \"minimum_J\": "<<evidence.minimumJ<<",\n"
           <<"  \"max_momentum_accounting_error\": "<<evidence.maximumMomentumAccountingError<<",\n"
           <<"  \"max_position_correction_m\": "<<evidence.maximumPositionCorrection<<",\n"
           <<"  \"accumulated_constraint_impulse_magnitude\": "<<evidence.accumulatedConstraintImpulseMagnitude<<",\n"
           <<"  \"warning\": \"No parameters were fit. Geometry is a declared proxy; constraint impulse is not a calibrated actuator force.\"\n"
           <<"}\n";

    std::cout<<std::setprecision(10)
             <<"GAUGE_FORWARD "<<label
             <<" particles="<<evidence.particles
             <<" grid="<<grid.nx<<"x"<<grid.ny<<"x"<<grid.nz
             <<" minJ="<<evidence.minimumJ
             <<" max_dt="<<evidence.maximumActualDt
             <<" substeps="<<evidence.totalSubsteps
             <<"\n";
    return 0;
}
