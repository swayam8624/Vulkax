#include "vulkax/calibration/hyperelastic.hpp"
#include "vulkax/field/field.hpp"
#include "vulkax/physics/cfd.hpp"
#include "vulkax/physics/dem.hpp"
#include "vulkax/visualization/export.hpp"
#include "vulkax/visualization/scientific.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace vulkax;

struct Options {
    std::filesystem::path output{"vulkax-output"};
    std::size_t particles{700};
    std::size_t steps{500};
    double dt{2.0e-4};
};

void ensureOutput(const std::filesystem::path& path) {
    std::filesystem::create_directories(path);
    if (!std::filesystem::is_directory(path)) throw std::runtime_error("unable to create output directory");
}

field::ParticleSet makeMillParticles(std::size_t requested, double radius) {
    field::ParticleSet particles;
    particles.positions.reserve(requested); particles.velocities.reserve(requested);
    particles.radii.reserve(requested); particles.masses.reserve(requested);
    const double particleRadius = 0.035;
    const double spacing = particleRadius * 2.08;
    for (double z = -0.18; z <= 0.18 && particles.positions.size() < requested; z += spacing) {
        for (double y = -0.72; y <= 0.20 && particles.positions.size() < requested; y += spacing) {
            for (double x = -0.72; x <= 0.72 && particles.positions.size() < requested; x += spacing) {
                if (x*x + y*y > (radius - particleRadius * 1.5) * (radius - particleRadius * 1.5)) continue;
                const auto i = particles.positions.size();
                const double jitter = 0.002 * std::sin(static_cast<double>(i) * 1.61803398875);
                particles.positions.push_back({x + jitter, y, z});
                particles.velocities.push_back({});
                particles.radii.push_back(particleRadius * (0.92 + 0.12 * (0.5 + 0.5 * std::sin(static_cast<double>(i) * 0.73))));
                particles.masses.push_back(0.18);
            }
        }
    }
    return particles;
}

void runMill(const Options& options) {
    const auto out = options.output / "mill"; ensureOutput(out);
    physics::dem::Settings settings;
    settings.gravity = {0.0, -9.81, 0.0};
    settings.drum.radius = 0.9; settings.drum.halfHeight = 0.28; settings.drum.angularVelocity = 2.4;
    settings.particleMaterial = {1.6e5, 65.0, 25.0, 0.48};
    settings.drum.wallMaterial = {2.0e5, 80.0, 35.0, 0.52};
    physics::dem::Solver solver(makeMillParticles(options.particles, settings.drum.radius), settings);
    for (std::size_t step = 0; step < options.steps; ++step) solver.step(options.dt);
    const auto& p = solver.particles();
    std::vector<double> speeds(p.positions.size());
    for (std::size_t i = 0; i < speeds.size(); ++i) speeds[i] = field::length(p.velocities[i]);
    const auto image = visualization::renderParticlesOrthographic(p, 1200, 1200, settings.drum.radius * 1.05, speeds);
    visualization::writePpm(image, out / "velocity.ppm");
    std::ofstream csv(out / "particles.csv");
    csv << "x,y,z,vx,vy,vz,radius,mass,speed\n" << std::setprecision(10);
    for (std::size_t i = 0; i < p.positions.size(); ++i)
        csv << p.positions[i].x << ',' << p.positions[i].y << ',' << p.positions[i].z << ','
            << p.velocities[i].x << ',' << p.velocities[i].y << ',' << p.velocities[i].z << ','
            << p.radii[i] << ',' << p.masses[i] << ',' << speeds[i] << '\n';
    const auto& stats = solver.statistics();
    std::ofstream summary(out / "summary.txt");
    summary << "Vulkax granular reference experiment\n"
            << "particles=" << p.positions.size() << "\nsteps=" << options.steps << "\ndt=" << options.dt
            << "\nkinetic_energy=" << stats.kineticEnergy << "\nmax_speed=" << stats.maximumSpeed
            << "\nparticle_contacts_last_step=" << stats.particleContacts
            << "\nwall_contacts_last_step=" << stats.wallContacts << '\n';
}

void runCfd(const Options& options) {
    const auto out = options.output / "cfd"; ensureOutput(out);
    physics::cfd::MacGrid2D grid(128, 72, 1.0 / 128.0, 1.0 / 128.0);
    constexpr double pi = std::numbers::pi_v<double>;
    for (std::uint32_t y = 0; y < grid.ny; ++y) {
        for (std::uint32_t x = 1; x < grid.nx; ++x) {
            const double fx = static_cast<double>(x) / grid.nx;
            const double fy = (static_cast<double>(y) + 0.5) / grid.ny;
            grid.u[grid.uFace(x,y)] = 0.75 * std::sin(2*pi*fx) * std::sin(pi*fy);
        }
    }
    for (std::uint32_t y = 1; y < grid.ny; ++y) {
        for (std::uint32_t x = 0; x < grid.nx; ++x) {
            const double fx = (static_cast<double>(x) + 0.5) / grid.nx;
            const double fy = static_cast<double>(y) / grid.ny;
            grid.v[grid.vFace(x,y)] = -0.45 * std::sin(pi*fx) * std::sin(2*pi*fy);
        }
    }
    const auto projection = physics::cfd::projectIncompressible(grid, 0.01, 700);
    for (std::uint32_t y = 0; y < grid.ny; ++y) for (std::uint32_t x = 0; x < grid.nx; ++x) {
        const double px = (static_cast<double>(x) + 0.5) / grid.nx;
        const double py = (static_cast<double>(y) + 0.5) / grid.ny;
        const double dx = px - 0.32, dy = py - 0.50;
        grid.scalar[grid.cell(x,y)] = std::exp(-(dx*dx + dy*dy) / 0.008);
    }
    for (int i = 0; i < 100; ++i) physics::cfd::advectScalarSemiLagrangian(grid, 0.002);
    field::GridShape shape{grid.nx, grid.ny, 1, {0,0,0}, {grid.dx,grid.dy,1}};
    field::ScalarField dye{shape, grid.scalar};
    auto image = visualization::renderScalarSliceZ(dye,0,0,1,visualization::ColorMap::Viridis,5);
    visualization::writePpm(image,out/"advected_scalar.ppm");
    field::ScalarField pressure{shape,grid.pressure};
    auto mm = std::minmax_element(pressure.values.begin(),pressure.values.end());
    double lo=*mm.first,hi=*mm.second;if(!(lo<hi))hi=lo+1;
    visualization::writePpm(visualization::renderScalarSliceZ(pressure,0,lo,hi,visualization::ColorMap::Diverging,5),out/"pressure.ppm");
    std::ofstream summary(out/"summary.txt");summary<<"Vulkax incompressible MAC reference experiment\n"
        <<"grid="<<grid.nx<<"x"<<grid.ny<<"\ndivergence_before="<<projection.divergenceBefore
        <<"\ndivergence_after="<<projection.divergenceAfter<<"\npressure_iterations="<<projection.pressureIterations<<'\n';
}

void runMaterial(const Options& options) {
    const auto out = options.output / "material"; ensureOutput(out);
    const std::vector<double> truth{2.2e5,4.5e4,1.8e4};
    std::vector<calibration::StressDatum> data;
    for (double stretch : {1.03,1.07,1.12,1.20,1.32,1.48,1.68,1.90}) {
        const double ideal = calibration::predictNominalStress(calibration::HyperelasticFamily::Yeoh3,truth,stretch);
        const double deterministicNoise = 0.003 * ideal * std::sin(stretch * 31.0);
        data.push_back({stretch, ideal + deterministicNoise, 1.0});
    }
    const auto selection = calibration::selectModel(data);
    const auto recommendation = calibration::recommendNextUniaxialStretch(selection,{1.1,1.3,1.5,1.8,2.1,2.4,2.7});
    std::ofstream csv(out/"model_fits.csv");csv<<"family,rms_error,aic,bic,parameters\n";
    const auto familyName=[](calibration::HyperelasticFamily f){switch(f){case calibration::HyperelasticFamily::NeoHookean:return "NeoHookean";case calibration::HyperelasticFamily::MooneyRivlin:return "MooneyRivlin";case calibration::HyperelasticFamily::Yeoh3:return "Yeoh3";}return "Unknown";};
    for(const auto&fit:selection.fits){csv<<familyName(fit.family)<<','<<fit.rmsError<<','<<fit.aic<<','<<fit.bic<<',';for(std::size_t i=0;i<fit.parameters.size();++i){if(i)csv<<';';csv<<fit.parameters[i];}csv<<'\n';}
    std::ofstream summary(out/"summary.txt");summary<<"Vulkax material-identification reference experiment\n";
    if(selection.valid)summary<<"selected_model="<<familyName(selection.fits[selection.bestIndex].family)<<"\nselected_bic="<<selection.fits[selection.bestIndex].bic<<'\n';
    if(recommendation.valid)summary<<"recommended_next_stretch="<<recommendation.stretch<<"\nmodel_disagreement="<<recommendation.disagreement<<'\n';
}

void runGeometry(const Options& options) {
    const auto out = options.output / "geometry"; ensureOutput(out);
    constexpr std::uint32_t n=42;constexpr double extent=std::numbers::pi_v<double>;
    field::GridShape grid{n,n,n,{-extent,-extent,-extent},{2*extent/(n-1),2*extent/(n-1),2*extent/(n-1)}};
    field::ScalarField gyroid{grid,std::vector<double>(grid.cellCount())};
    for(std::uint32_t z=0;z<n;++z)for(std::uint32_t y=0;y<n;++y)for(std::uint32_t x=0;x<n;++x){double px=grid.origin.x+grid.spacing.x*x,py=grid.origin.y+grid.spacing.y*y,pz=grid.origin.z+grid.spacing.z*z;gyroid.values[grid.index(x,y,z)]=std::sin(px)*std::cos(py)+std::sin(py)*std::cos(pz)+std::sin(pz)*std::cos(px);}
    auto surface=visualization::extractIsoSurface(gyroid,0.0);visualization::writeObj(surface.mesh,out/"gyroid.obj");
    std::ofstream summary(out/"summary.txt");summary<<"Vulkax field-to-geometry reference experiment\nactive_cells="<<surface.activeCells<<"\ntriangles="<<surface.mesh.triangles.size()<<'\n';
}

Options parseOptions(int argc,char**argv,int start){Options o;for(int i=start;i<argc;++i){std::string_view a(argv[i]);if(a=="--output"&&i+1<argc)o.output=argv[++i];else if(a=="--particles"&&i+1<argc)o.particles=static_cast<std::size_t>(std::stoull(argv[++i]));else if(a=="--steps"&&i+1<argc)o.steps=static_cast<std::size_t>(std::stoull(argv[++i]));else if(a=="--dt"&&i+1<argc)o.dt=std::stod(argv[++i]);else throw std::invalid_argument("unknown or incomplete option: "+std::string(a));}if(o.particles==0||o.steps==0||o.dt<=0)throw std::invalid_argument("particles, steps and dt must be positive");return o;}
void usage(){std::cout<<"vulkax-lab <suite|mill|cfd|material|geometry> [--output DIR] [--particles N] [--steps N] [--dt SECONDS]\n";}
}

int main(int argc,char**argv){try{if(argc<2){usage();return 2;}std::string command=argv[1];auto options=parseOptions(argc,argv,2);ensureOutput(options.output);if(command=="suite"){runMill(options);runCfd(options);runMaterial(options);runGeometry(options);}else if(command=="mill")runMill(options);else if(command=="cfd")runCfd(options);else if(command=="material")runMaterial(options);else if(command=="geometry")runGeometry(options);else{usage();return 2;}std::cout<<"Vulkax lab completed: "<<options.output.string()<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"vulkax-lab error: "<<e.what()<<'\n';return 1;}}
