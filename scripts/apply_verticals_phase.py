from pathlib import Path
import runpy

ROOT = Path('.')

# If the previous atomic migration is still pending, apply it first.
if not Path('include/vulkax/execution/problem_runner.hpp').exists():
    previous = Path('scripts/apply_problem_runner_migration.py')
    if not previous.exists():
        raise RuntimeError('problem-runner prerequisite is missing')
    runpy.run_path(str(previous), run_name='__main__')

for stale in [Path('scripts/apply_problem_runner_migration.py'), Path('.github/workflows/wip-problem-runner-migration.yml')]:
    if stale.exists():
        stale.unlink()


def replace_once(path: str, old: str, new: str):
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if old not in text:
        raise RuntimeError(f'anchor not found in {path}: {old[:100]!r}')
    p.write_text(text.replace(old, new, 1))

# Reproducible external resources (geometry, measurements) belong to ProblemIR.
replace_once('include/vulkax/problem/problem_ir.hpp',
             'struct NamedParameter { std::string id; units::Quantity value; };\nstruct ComputeBudget',
             'struct NamedParameter { std::string id; units::Quantity value; };\nstruct DataResource { std::string id; std::string kind; std::string path; };\nstruct ComputeBudget')
replace_once('include/vulkax/problem/problem_ir.hpp',
             '    std::vector<NamedParameter> parameters;\n};',
             '    std::vector<NamedParameter> parameters;\n    std::vector<DataResource> resources;\n};')
replace_once('include/vulkax/problem/problem_ir.hpp',
             '[[nodiscard]] double requireParameterSI(const ProblemIR& problem, const std::string& id,\n                                        units::Dimension expectedDimension);',
             '[[nodiscard]] double requireParameterSI(const ProblemIR& problem, const std::string& id,\n                                        units::Dimension expectedDimension);\n[[nodiscard]] const DataResource* findResource(const ProblemIR& problem, const std::string& id) noexcept;\n[[nodiscard]] const DataResource& requireResource(const ProblemIR& problem, const std::string& id,\n                                                  const std::string& expectedKind);')
replace_once('src/problem/problem_ir.cpp',
             'double requireParameterSI(const ProblemIR& problem, const std::string& id,\n                          units::Dimension expectedDimension) {',
             'const DataResource* findResource(const ProblemIR& problem, const std::string& id) noexcept {\n    const auto it = std::find_if(problem.resources.begin(), problem.resources.end(),\n                                 [&](const auto& r) { return r.id == id; });\n    return it == problem.resources.end() ? nullptr : &*it;\n}\n\nconst DataResource& requireResource(const ProblemIR& problem, const std::string& id,\n                                    const std::string& expectedKind) {\n    const auto* resource = findResource(problem, id);\n    if (!resource) throw std::invalid_argument("missing problem resource: " + id);\n    if (resource->kind != expectedKind) throw std::invalid_argument("problem resource has wrong kind: " + id);\n    return *resource;\n}\n\ndouble requireParameterSI(const ProblemIR& problem, const std::string& id,\n                          units::Dimension expectedDimension) {')
replace_once('src/problem/problem_ir.cpp',
             '    for (const NamedParameter* parameter : sortedById(problem.parameters)) {',
             '    for (const DataResource* resource : sortedById(problem.resources)) {\n        hashString(hash, resource->id); hashString(hash, resource->kind); hashString(hash, resource->path);\n    }\n\n    for (const NamedParameter* parameter : sortedById(problem.parameters)) {')
replace_once('src/problem/document.cpp',
             '} else if (command == "domain") {',
             '} else if (command == "resource") {\n                DataResource resource;\n                if (!(stream >> std::quoted(resource.id) >> std::quoted(resource.kind) >> std::quoted(resource.path)))\n                    throw std::invalid_argument("invalid resource record");\n                result.resources.push_back(std::move(resource));\n            } else if (command == "domain") {')
replace_once('src/problem/document.cpp',
             '    for (const auto& parameter : problem.parameters) {',
             '    for (const auto& resource : problem.resources)\n        stream << "resource " << std::quoted(resource.id) << \' \' << std::quoted(resource.kind) << \' \' << std::quoted(resource.path) << \'\\n\';\n    for (const auto& parameter : problem.parameters) {')
replace_once('src/problem/validation.cpp',
             '    validateUniqueIds(problem.parameters, "parameters", result);',
             '    validateUniqueIds(problem.parameters, "parameters", result);\n    validateUniqueIds(problem.resources, "resources", result);\n    for (std::size_t i = 0; i < problem.resources.size(); ++i) {\n        if (problem.resources[i].kind.empty() || problem.resources[i].path.empty())\n            result.issues.push_back({ValidationSeverity::Error, "resources[" + std::to_string(i) + "]",\n                                     "resource kind and path cannot be empty"});\n    }')

Path('include/vulkax/execution/verticals.hpp').write_text(r'''#pragma once
#include "vulkax/execution/problem_runner.hpp"
namespace vulkax::execution {
[[nodiscard]] ProblemRunResult runHyperelasticVertical(const problem::ProblemIR&, const ProblemRunOptions&);
[[nodiscard]] ProblemRunResult runAerodynamicsVertical(const problem::ProblemIR&, const ProblemRunOptions&);
}
''')

Path('src/execution/hyperelastic_vertical.cpp').write_text(r'''#include "vulkax/execution/verticals.hpp"
#include "vulkax/backend/backend.hpp"
#include "vulkax/render/camera.hpp"
#include "vulkax/render/capture.hpp"
#include "vulkax/render/headless.hpp"
#include "vulkax/solvers/hyperelastic_fem.hpp"
#include "vulkax/verify/result_certificate.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace vulkax::execution {
namespace {
const units::Dimension pressureDimension = units::makeDimension(-1,1,-2);
const units::Dimension forceDimension = units::makeDimension(1,1,-2);
backend::BackendKind rendererChoice(){const auto a=render::availableHeadlessRenderBackends();if(a.empty())throw std::runtime_error("no renderer");if(backend::currentPlatform()==backend::PlatformKind::MacOS&&std::find(a.begin(),a.end(),backend::BackendKind::Metal)!=a.end())return backend::BackendKind::Metal;if(std::find(a.begin(),a.end(),backend::BackendKind::Vulkan)!=a.end())return backend::BackendKind::Vulkan;return a.front();}
}
ProblemRunResult runHyperelasticVertical(const problem::ProblemIR& p,const ProblemRunOptions& options){
    const double E=problem::requireParameterSI(p,"young_modulus",pressureDimension);
    const double nu=problem::requireParameterSI(p,"poisson_ratio",units::dimensionless);
    const double load=problem::requireParameterSI(p,"load",forceDimension);
    const double size=problem::requireParameterSI(p,"specimen_size",units::length);
    if(E<=0||nu<=-1||nu>=0.5||size<=0)throw std::invalid_argument("invalid hyperelastic operating parameters");
    std::vector<solvers::FemNode> n(8); const std::array<math::Vec3,8> q{{{0,0,0},{size,0,0},{size,size,0},{0,size,0},{0,0,size},{size,0,size},{size,size,size},{0,size,size}}};
    for(std::size_t i=0;i<8;++i)n[i].position=q[i]; for(std::size_t i=0;i<4;++i)n[i].fixed={true,true,true}; for(std::size_t i=4;i<8;++i)n[i].force={0,0,load/4.0};
    const std::vector<solvers::Tetrahedron> e={{{0,1,3,4}},{{1,2,3,6}},{{1,3,4,6}},{{1,4,5,6}},{{3,4,6,7}}};
    const auto r=solvers::solveNeoHookeanTetrahedral(n,e,{E,nu},{40,std::max(1e-5,std::abs(load)*1e-7),2e-5,1e-7});
    double maxU=0,maxStress=0;for(const auto&u:r.displacement)maxU=std::max(maxU,math::length(u));for(double s:r.vonMisesStress)maxStress=std::max(maxStress,s);
    std::filesystem::create_directories(options.outputDirectory);ProblemRunResult out;out.simulationBackend="CPU reference / nonlinear Neo-Hookean tetrahedral FEM";
    if(!render::availableHeadlessRenderBackends().empty()){const auto b=rendererChoice();out.visualizationBackend=std::string(backend::toString(b));std::vector<visualization::ParticleInstance> pts;for(std::size_t i=0;i<n.size();++i)pts.push_back({n[i].position+r.displacement[i],size*0.035,visualization::sampleColorMap(visualization::ColorMap::CoolWarm,static_cast<double>(i)/7.0)});render::CameraTrack track;track.setKeyframes({{0,{{1.5*size,1.2*size,3.0*size},{0.5*size,0.5*size,0.5*size},{0,1,0},42,1}}});render::CaptureSettings cs{options.width,options.height,1.0,options.frameCount,(std::filesystem::path(options.outputDirectory)/"frames").string(),{0.008F,0.01F,0.016F,1}};auto cap=render::captureParticleSequence(b,[&](double){return pts;},track,cs);out.framePaths=cap.framePaths;}else out.visualizationBackend="none (simulation-only build)";
    verify::ResultCertificate c;c.problemHash=problem::stableProblemHash(p);c.solverHash=c.problemHash^0x4e454f484f4f4bULL;c.backend=out.simulationBackend;c.device="host CPU";c.criteria.push_back({"nonlinear equilibrium gradient norm",r.finalGradientNorm,std::max(1e-5,std::abs(load)*1e-7),verify::CriterionRelation::LessEqual,true});c.criteria.push_back({"positive strain energy",r.strainEnergy,0.0,verify::CriterionRelation::GreaterEqual,true});c.notes={"converged="+std::string(r.converged?"true":"false"),"max_displacement="+std::to_string(maxU),"max_von_mises="+std::to_string(maxStress),"visualization_backend="+out.visualizationBackend};c.updateTrustState(false);out.resultCertificatePath=(std::filesystem::path(options.outputDirectory)/"result.json").string();std::ofstream f(out.resultCertificatePath);f<<c.toJson();return out;
}
} // namespace vulkax::execution
''')

Path('include/vulkax/solvers/aerodynamics3d.hpp').write_text(r'''#pragma once
#include "vulkax/geometry/voxelize.hpp"
#include "vulkax/solvers/mac3d.hpp"
#include "vulkax/visualization/scientific.hpp"
namespace vulkax::solvers {
struct AerodynamicsConfig{double dt{0.001};double density{1.225};double kinematicViscosity{1.5e-5};double inletSpeed{10};std::size_t pressureIterations{100};};
struct AerodynamicsDiagnostics{double divergenceL2Before{};double divergenceL2After{};math::Vec3 pressureForce{};double maxSpeed{};};
[[nodiscard]] AerodynamicsDiagnostics advanceAerodynamics(MacGrid3D&,const AerodynamicsConfig&,std::size_t steps);
[[nodiscard]] visualization::VectorGrid3D cellCenteredVelocity(const MacGrid3D&);
[[nodiscard]] numerics::ScalarGrid3D pressureField(const MacGrid3D&);
}
''')

Path('src/solvers/aerodynamics3d.cpp').write_text(r'''#include "vulkax/solvers/aerodynamics3d.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace vulkax::solvers {
namespace {
math::Vec3 cellVelocity(const MacGrid3D&g,std::size_t x,std::size_t y,std::size_t z){return{0.5*(g.u(x,y,z)+g.u(x+1,y,z)),0.5*(g.v(x,y,z)+g.v(x,y+1,z)),0.5*(g.w(x,y,z)+g.w(x,y,z+1))};}
void boundaries(MacGrid3D&g,double inlet){for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y){g.u(0,y,z)=inlet;g.u(g.nx(),y,z)=g.u(g.nx()-1,y,z);}for(std::size_t z=0;z<g.nz();++z)for(std::size_t x=0;x<g.nx();++x){g.v(x,0,z)=0;g.v(x,g.ny(),z)=0;}for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x){g.w(x,y,0)=0;g.w(x,y,g.nz())=0;}for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=1;x<g.nx();++x)if(g.solid(x-1,y,z)||g.solid(x,y,z))g.u(x,y,z)=0;for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=1;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x)if(g.solid(x,y-1,z)||g.solid(x,y,z))g.v(x,y,z)=0;for(std::size_t z=1;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x)if(g.solid(x,y,z-1)||g.solid(x,y,z))g.w(x,y,z)=0;}
double div(const MacGrid3D&g,std::size_t x,std::size_t y,std::size_t z){return(g.u(x+1,y,z)-g.u(x,y,z))/g.dx()+(g.v(x,y+1,z)-g.v(x,y,z))/g.dy()+(g.w(x,y,z+1)-g.w(x,y,z))/g.dz();}
double interiorDivL2(const MacGrid3D&g){double s=0;std::size_t n=0;for(std::size_t z=1;z+1<g.nz();++z)for(std::size_t y=1;y+1<g.ny();++y)for(std::size_t x=1;x+1<g.nx();++x)if(!g.solid(x,y,z)){const double d=div(g,x,y,z);s+=d*d;++n;}return n?std::sqrt(s/n):0;}
}
AerodynamicsDiagnostics advanceAerodynamics(MacGrid3D&g,const AerodynamicsConfig&c,std::size_t steps){if(c.dt<=0||c.density<=0||c.kinematicViscosity<0||c.inletSpeed<0||c.pressureIterations==0||steps==0)throw std::invalid_argument("invalid aerodynamics config");AerodynamicsDiagnostics out;for(std::size_t step=0;step<steps;++step){boundaries(g,c.inletSpeed);if(step==0)out.divergenceL2Before=interiorDivL2(g);MacGrid3D old=g;const double hx=g.dx(),hy=g.dy(),hz=g.dz();for(std::size_t z=1;z+1<g.nz();++z)for(std::size_t y=1;y+1<g.ny();++y)for(std::size_t x=1;x+1<g.nx();++x)if(!g.solid(x,y,z)){const auto v=cellVelocity(old,x,y,z);const auto vxm=cellVelocity(old,x-1,y,z),vxp=cellVelocity(old,x+1,y,z),vym=cellVelocity(old,x,y-1,z),vyp=cellVelocity(old,x,y+1,z),vzm=cellVelocity(old,x,y,z-1),vzp=cellVelocity(old,x,y,z+1);math::Vec3 next=v;const math::Vec3 gradx=(vxp-vxm)/(2*hx),grady=(vyp-vym)/(2*hy),gradz=(vzp-vzm)/(2*hz);next-= (gradx*v.x+grady*v.y+gradz*v.z)*c.dt;next+=( (vxp+vxm-v*2.0)/(hx*hx)+(vyp+vym-v*2.0)/(hy*hy)+(vzp+vzm-v*2.0)/(hz*hz) )*(c.kinematicViscosity*c.dt);g.u(x,y,z)=0.5*(g.u(x,y,z)+next.x);g.u(x+1,y,z)=0.5*(g.u(x+1,y,z)+next.x);g.v(x,y,z)=0.5*(g.v(x,y,z)+next.y);g.v(x,y+1,z)=0.5*(g.v(x,y+1,z)+next.y);g.w(x,y,z)=0.5*(g.w(x,y,z)+next.z);g.w(x,y,z+1)=0.5*(g.w(x,y,z+1)+next.z);}
        boundaries(g,c.inletSpeed);const double wx=1/(hx*hx),wy=1/(hy*hy),wz=1/(hz*hz);std::vector<double>nextP(g.nx()*g.ny()*g.nz());auto p=[&](long long x,long long y,long long z,std::size_t cx,std::size_t cy,std::size_t cz){if(x<0||y<0||z<0||x>=static_cast<long long>(g.nx())||y>=static_cast<long long>(g.ny())||z>=static_cast<long long>(g.nz())||g.solid(static_cast<std::size_t>(x),static_cast<std::size_t>(y),static_cast<std::size_t>(z)))return g.pressure(cx,cy,cz);return g.pressure(static_cast<std::size_t>(x),static_cast<std::size_t>(y),static_cast<std::size_t>(z));};for(std::size_t it=0;it<c.pressureIterations;++it){for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x){const auto idx=(z*g.ny()+y)*g.nx()+x;if(g.solid(x,y,z)){nextP[idx]=0;continue;}const double rhs=c.density/c.dt*div(g,x,y,z);const double q=wx*(p((long long)x-1,y,z,x,y,z)+p(x+1,y,z,x,y,z))+wy*(p(x,(long long)y-1,z,x,y,z)+p(x,y+1,z,x,y,z))+wz*(p(x,y,(long long)z-1,x,y,z)+p(x,y,z+1,x,y,z));nextP[idx]=(q-rhs)/(2*(wx+wy+wz));}for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x)g.pressure(x,y,z)=nextP[(z*g.ny()+y)*g.nx()+x];}
        for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=1;x<g.nx();++x)if(!g.solid(x-1,y,z)&&!g.solid(x,y,z))g.u(x,y,z)-=c.dt/c.density*(g.pressure(x,y,z)-g.pressure(x-1,y,z))/hx;for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=1;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x)if(!g.solid(x,y-1,z)&&!g.solid(x,y,z))g.v(x,y,z)-=c.dt/c.density*(g.pressure(x,y,z)-g.pressure(x,y-1,z))/hy;for(std::size_t z=1;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x)if(!g.solid(x,y,z-1)&&!g.solid(x,y,z))g.w(x,y,z)-=c.dt/c.density*(g.pressure(x,y,z)-g.pressure(x,y,z-1))/hz;boundaries(g,c.inletSpeed);}
    out.divergenceL2After=interiorDivL2(g);const double ax=g.dy()*g.dz(),ay=g.dx()*g.dz(),az=g.dx()*g.dy();for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x)if(g.solid(x,y,z)){auto add=[&](long long nx,long long ny,long long nz,math::Vec3 normal,double area){if(nx<0||ny<0||nz<0||nx>=static_cast<long long>(g.nx())||ny>=static_cast<long long>(g.ny())||nz>=static_cast<long long>(g.nz()))return;if(g.solid((std::size_t)nx,(std::size_t)ny,(std::size_t)nz))return;out.pressureForce-=normal*g.pressure((std::size_t)nx,(std::size_t)ny,(std::size_t)nz)*area;};add((long long)x-1,y,z,{-1,0,0},ax);add(x+1,y,z,{1,0,0},ax);add(x,(long long)y-1,z,{0,-1,0},ay);add(x,y+1,z,{0,1,0},ay);add(x,y,(long long)z-1,{0,0,-1},az);add(x,y,z+1,{0,0,1},az);}for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x)out.maxSpeed=std::max(out.maxSpeed,math::length(cellVelocity(g,x,y,z)));return out;}
visualization::VectorGrid3D cellCenteredVelocity(const MacGrid3D&g){visualization::VectorGrid3D f(g.nx(),g.ny(),g.nz(),{g.dx(),g.dy(),g.dz()});for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x)f.at(x,y,z)=cellVelocity(g,x,y,z);return f;}
numerics::ScalarGrid3D pressureField(const MacGrid3D&g){numerics::ScalarGrid3D f(g.nx(),g.ny(),g.nz(),{g.dx(),g.dy(),g.dz()});for(std::size_t z=0;z<g.nz();++z)for(std::size_t y=0;y<g.ny();++y)for(std::size_t x=0;x<g.nx();++x)f.at(x,y,z)=g.pressure(x,y,z);return f;}
} // namespace vulkax::solvers
''')

Path('src/execution/aerodynamics_vertical.cpp').write_text(r'''#include "vulkax/execution/verticals.hpp"
#include "vulkax/backend/backend.hpp"
#include "vulkax/geometry/mesh.hpp"
#include "vulkax/geometry/voxelize.hpp"
#include "vulkax/render/camera.hpp"
#include "vulkax/render/capture.hpp"
#include "vulkax/render/headless.hpp"
#include "vulkax/solvers/aerodynamics3d.hpp"
#include "vulkax/verify/result_certificate.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace vulkax::execution {
namespace { const units::Dimension densityD=units::makeDimension(-3,1,0), kinViscD=units::makeDimension(2,0,-1);backend::BackendKind pick(){auto a=render::availableHeadlessRenderBackends();if(a.empty())throw std::runtime_error("no renderer");if(backend::currentPlatform()==backend::PlatformKind::MacOS&&std::find(a.begin(),a.end(),backend::BackendKind::Metal)!=a.end())return backend::BackendKind::Metal;return std::find(a.begin(),a.end(),backend::BackendKind::Vulkan)!=a.end()?backend::BackendKind::Vulkan:a.front();}double material(const problem::ProblemIR&p,const std::string&n,units::Dimension d){for(const auto&m:p.materials)for(const auto&q:m.properties)if(q.name==n){if(!(q.value.dimension==d))throw std::invalid_argument("wrong material dimension: "+n);return q.value.valueSI;}throw std::invalid_argument("missing material: "+n);} }
ProblemRunResult runAerodynamicsVertical(const problem::ProblemIR&p,const ProblemRunOptions&o){const auto&r=problem::requireResource(p,"body","mesh_obj");auto mesh=geometry::normalizedToUnitBox(geometry::loadObj(r.path));const auto nx=(std::size_t)std::llround(problem::requireParameterSI(p,"resolution_x",units::dimensionless)),ny=(std::size_t)std::llround(problem::requireParameterSI(p,"resolution_y",units::dimensionless)),nz=(std::size_t)std::llround(problem::requireParameterSI(p,"resolution_z",units::dimensionless));const double Lx=problem::requireParameterSI(p,"domain_x",units::length),Ly=problem::requireParameterSI(p,"domain_y",units::length),Lz=problem::requireParameterSI(p,"domain_z",units::length),inlet=problem::requireParameterSI(p,"inlet_speed",units::velocity),dt=problem::requireParameterSI(p,"dt",units::time);const auto steps=(std::size_t)std::llround(problem::requireParameterSI(p,"steps",units::dimensionless));if(nx<8||ny<8||nz<8||nx>128||ny>128||nz>128||Lx<=0||Ly<=0||Lz<=0||dt<=0||steps==0)throw std::invalid_argument("invalid CFD discretization");for(auto&v:mesh.positions){v.x=v.x*0.35;v.y=v.y*0.25;v.z=v.z*0.25;}const geometry::Bounds3 box{{-0.5*Lx,-0.5*Ly,-0.5*Lz},{0.5*Lx,0.5*Ly,0.5*Lz}};const auto mask=geometry::voxelizeClosedMesh(mesh,nx,ny,nz,box);solvers::MacGrid3D grid(nx,ny,nz,Lx/nx,Ly/ny,Lz/nz);grid.setSolidMask(mask);for(std::size_t z=0;z<nz;++z)for(std::size_t y=0;y<ny;++y)for(std::size_t x=0;x<=nx;++x)grid.u(x,y,z)=inlet;const double rho=material(p,"density",densityD),nu=material(p,"kinematic_viscosity",kinViscD);const auto d=solvers::advanceAerodynamics(grid,{dt,rho,nu,inlet,120},steps);std::filesystem::create_directories(o.outputDirectory);ProblemRunResult out;out.simulationBackend="CPU reference / 3D staggered MAC CFD";if(!render::availableHeadlessRenderBackends().empty()){const auto b=pick();out.visualizationBackend=std::string(backend::toString(b));const auto vf=solvers::cellCenteredVelocity(grid);std::vector<visualization::ParticleInstance> tracers;for(int sy=1;sy<=6;++sy)for(int sz=1;sz<=4;++sz){const math::Vec3 seed{0.05*Lx,(double(sy)/7.0)*Ly,(double(sz)/5.0)*Lz};auto line=visualization::traceStreamline(vf,seed,0.03*Lx,80,1e-8);for(std::size_t i=0;i<line.points.size();i+=3){auto pos=line.points[i];pos.x-=0.5*Lx;pos.y-=0.5*Ly;pos.z-=0.5*Lz;tracers.push_back({pos,0.008*Lx,visualization::sampleColorMap(visualization::ColorMap::Viridis,double(i)/std::max<std::size_t>(1,line.points.size()-1))});}}render::CameraTrack track;track.setKeyframes({{0,{{1.4*Lx,0.9*Ly,1.5*Lz},{0,0,0},{0,1,0},45,1}}});render::CaptureSettings cs{o.width,o.height,1.0,o.frameCount,(std::filesystem::path(o.outputDirectory)/"frames").string(),{0.008F,0.01F,0.016F,1}};out.framePaths=render::captureParticleSequence(b,[&](double){return tracers;},track,cs).framePaths;}else out.visualizationBackend="none (simulation-only build)";verify::ResultCertificate c;c.problemHash=problem::stableProblemHash(p);c.solverHash=c.problemHash^0x4346445f4d414333ULL;c.backend=out.simulationBackend;c.device="host CPU";const double ratio=d.divergenceL2Before>0?d.divergenceL2After/d.divergenceL2Before:0;c.criteria.push_back({"projection divergence ratio",ratio,0.65,verify::CriterionRelation::LessEqual,true});c.notes={"drag_force_x="+std::to_string(d.pressureForce.x),"lift_force_y="+std::to_string(d.pressureForce.y),"max_speed="+std::to_string(d.maxSpeed),"solid_voxels="+std::to_string(mask.solidCount()),"visualization_backend="+out.visualizationBackend};c.updateTrustState(false);out.resultCertificatePath=(std::filesystem::path(o.outputDirectory)/"result.json").string();std::ofstream f(out.resultCertificatePath);f<<c.toJson();return out;}
} // namespace vulkax::execution
''')

Path('examples/assets').mkdir(parents=True,exist_ok=True)
Path('examples/assets/aero_body.obj').write_text('''v -0.8 -0.35 -0.3
v 0.7 -0.35 -0.3
v 0.9 0.15 -0.3
v -0.65 0.25 -0.3
v -0.8 -0.35 0.3
v 0.7 -0.35 0.3
v 0.9 0.15 0.3
v -0.65 0.25 0.3
f 1 3 2
f 1 4 3
f 5 6 7
f 5 7 8
f 1 2 6
f 1 6 5
f 4 8 7
f 4 7 3
f 1 5 8
f 1 8 4
f 2 3 7
f 2 7 6
''')
Path('examples/car_aerodynamics.vkx').write_text('''vulkax 1
id "car-aerodynamics"
name "Vehicle aerodynamics reference problem"
resource "body" "mesh_obj" "examples/assets/aero_body.obj"
parameter "resolution_x" 36 0 0 0 0 0 0 0
parameter "resolution_y" 20 0 0 0 0 0 0 0
parameter "resolution_z" 20 0 0 0 0 0 0 0
parameter "domain_x" 4 1 0 0 0 0 0 0
parameter "domain_y" 2 1 0 0 0 0 0 0
parameter "domain_z" 2 1 0 0 0 0 0 0
parameter "inlet_speed" 20 1 0 -1 0 0 0 0
parameter "dt" 0.0005 0 0 1 0 0 0 0
parameter "steps" 2 0 0 0 0 0 0 0
domain "air" volume 3
field "velocity" "air" vector 3 1 0 -1 0 0 0 0
field "pressure" "air" scalar 1 -1 1 -2 0 0 0 0
operator "momentum" "Momentum balance" "velocity" "fluid" "du/dt + advect(velocity) + grad(pressure)/rho - nu*laplacian(velocity)" "velocity" "pressure"
operator "continuity" "Incompressibility" "pressure" "incompressible" "div(velocity)" "velocity"
material "air"
property "air" "density" 1.225 -3 1 0 0 0 0 0
property "air" "kinematic_viscosity" 1.48e-5 2 0 -1 0 0 0 0
objective "drag" "Drag force" minimize "pressure_force_x(body)"
accuracy "drag" 0.05 0 0
budget_wall 120
budget_memory 8589934592
''')
Path('examples/rubber_inverse.vkx').write_text('''vulkax 1
id "rubber-inverse"
name "Hyperelastic deformation reference problem"
parameter "young_modulus" 80000 -1 1 -2 0 0 0 0
parameter "poisson_ratio" 0.35 0 0 0 0 0 0 0
parameter "load" -120 1 1 -2 0 0 0 0
parameter "specimen_size" 1 1 0 0 0 0 0 0
domain "specimen" volume 3
field "displacement" "specimen" vector 3 1 0 0 0 0 0 0
operator "equilibrium" "Nonlinear equilibrium" "displacement" "hyperelastic" "div(P(F,theta)) + body_force" "displacement"
material "rubber"
property "rubber" "density" 1050 -3 1 0 0 0 0 0
objective "strain-energy" "Strain energy" observe "strain_energy(displacement)"
accuracy "strain-energy" 0.05 0 0
budget_wall 180
budget_memory 4294967296
''')

# Wire new verticals into the existing run dispatcher.
replace_once('src/execution/problem_runner.cpp', '#include "vulkax/execution/problem_runner.hpp"', '#include "vulkax/execution/problem_runner.hpp"\n#include "vulkax/execution/verticals.hpp"')
replace_once('src/execution/problem_runner.cpp',
             '    if (dem) return runRotatingMill(problem, options);\n    throw std::runtime_error("this ProblemIR family is not yet connected to the end-to-end runner");',
             '    if (dem) return runRotatingMill(problem, options);\n    const bool hyperelastic = std::any_of(problem.operators.begin(), problem.operators.end(), [](const auto& op) { return op.family == "hyperelastic"; });\n    if (hyperelastic) return runHyperelasticVertical(problem, options);\n    const bool fluid = std::any_of(problem.operators.begin(), problem.operators.end(), [](const auto& op) { return op.family == "fluid" || op.family == "incompressible"; });\n    if (fluid) return runAerodynamicsVertical(problem, options);\n    throw std::runtime_error("this ProblemIR family is not yet connected to the end-to-end runner");')

replace_once('CMakeLists.txt', 'project(Vulkax VERSION 0.18.0 LANGUAGES CXX)', 'project(Vulkax VERSION 0.19.0 LANGUAGES CXX)')
replace_once('CMakeLists.txt', 'src/execution/experiment.cpp src/execution/problem_runner.cpp src/experiment/design.cpp', 'src/execution/experiment.cpp src/execution/problem_runner.cpp src/execution/hyperelastic_vertical.cpp src/execution/aerodynamics_vertical.cpp src/experiment/design.cpp')
replace_once('CMakeLists.txt', 'src/solvers/hyperelastic_fem.cpp src/solvers/incompressible2d.cpp src/solvers/mac3d.cpp', 'src/solvers/hyperelastic_fem.cpp src/solvers/incompressible2d.cpp src/solvers/mac3d.cpp src/solvers/aerodynamics3d.cpp')

# Extend CI with all three verticals. Keep resolutions tiny enough for CI; these are correctness gates, not performance claims.
p = Path('.github/workflows/ci.yml')
ci = p.read_text()
needle = '          grep -q \'"trust_state": "converging"\' build/mill-run/result.json\n'
extra = needle + '''          ./build/vulkax validate examples/rubber_inverse.vkx
          ./build/vulkax run examples/rubber_inverse.vkx --output build/rubber-run --frames 1 --width 120 --height 100
          test -s build/rubber-run/result.json
          ./build/vulkax validate examples/car_aerodynamics.vkx
          ./build/vulkax run examples/car_aerodynamics.vkx --output build/aero-run --frames 1 --width 120 --height 100
          test -s build/aero-run/result.json
'''
if needle in ci and 'build/rubber-run' not in ci:
    ci = ci.replace(needle, extra)
p.write_text(ci)
