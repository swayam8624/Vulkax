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
#include <string>
#include <vector>

namespace {

using vulkax::math::Vec3;
using vulkax::research::PrescribedParticleTarget;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmMaterial;
using vulkax::solvers::MpmParticle;

struct RunSummary {
    std::string label;
    double young{};
    double poisson{};
    double peakShear{};
    double accumulatedConstraintImpulse{};
    double maximumMomentumAccountingError{};
    double maximumPositionCorrection{};
    double minimumJ{1.0};
    double finalInteriorMeanX{};
    double finalInteriorRmsX{};
    double finalTopPositionError{};
};

std::vector<MpmParticle> makeSlab() {
    std::vector<MpmParticle> particles;
    constexpr int nx=5, ny=5, nz=5;
    constexpr double h=0.02;
    constexpr double volume=h*h*h;
    constexpr double density=1000.0;
    std::uint64_t id=1;
    for(int iy=0;iy<ny;++iy)
        for(int iz=0;iz<nz;++iz)
            for(int ix=0;ix<nx;++ix) {
                MpmParticle p;
                p.id=id++;
                p.restPosition={
                    (static_cast<double>(ix)-2.0)*h,
                    static_cast<double>(iy)*h,
                    (static_cast<double>(iz)-2.0)*h
                };
                p.position=p.restPosition;
                p.mass=density*volume;
                p.restVolume=volume;
                particles.push_back(p);
            }
    return particles;
}

MpmGridSettings makeGrid() {
    MpmGridSettings g;
    g.origin={-0.18,-0.08,-0.18};
    g.nx=24; g.ny=24; g.nz=24;
    g.cellSize=0.02;
    g.boundaryCells=0;
    return g;
}

RunSummary runCase(std::string label,double young,double poisson) {
    auto particles=makeSlab();
    const auto initial=particles;
    const auto grid=makeGrid();
    MpmMaterial material{1000.0,young,poisson};

    constexpr double dt=1.0e-4;
    constexpr std::size_t rampSteps=80;
    constexpr std::size_t holdSteps=40;
    constexpr double totalShear=0.006; // 6 mm over an 80 mm slab height.
    constexpr double topY=0.08;
    constexpr double bottomY=0.0;
    const double shearVelocity=totalShear/(static_cast<double>(rampSteps)*dt);

    RunSummary s;
    s.label=std::move(label);
    s.young=young;
    s.poisson=poisson;
    s.peakShear=totalShear/topY;
    s.minimumJ=std::numeric_limits<double>::infinity();

    auto findInitial=[&](std::uint64_t id)->const MpmParticle&{
        return initial.at(static_cast<std::size_t>(id-1));
    };

    for(std::size_t step=1;step<=rampSteps+holdSteps;++step) {
        const bool ramp=step<=rampSteps;
        const double displacement = ramp
            ? totalShear*static_cast<double>(step)/static_cast<double>(rampSteps)
            : totalShear;
        std::vector<PrescribedParticleTarget> targets;
        for(const auto& p:particles) {
            const auto& ref=findInitial(p.id);
            if(std::abs(ref.restPosition.y-bottomY)<1.0e-12) {
                targets.push_back({p.id,ref.restPosition,{0,0,0},true});
            } else if(std::abs(ref.restPosition.y-topY)<1.0e-12) {
                targets.push_back({
                    p.id,
                    ref.restPosition+Vec3{displacement,0,0},
                    ramp ? Vec3{shearVelocity,0,0} : Vec3{0,0,0},
                    true
                });
            }
        }

        const auto evidence=vulkax::research::stepMpmWithPrescribedParticles(
            particles,grid,material,dt,targets,{0,0,0});

        s.accumulatedConstraintImpulse += evidence.totalConstraintImpulseMagnitude;
        s.maximumMomentumAccountingError = std::max(
            s.maximumMomentumAccountingError,evidence.momentumAccountingError);
        s.maximumPositionCorrection = std::max(
            s.maximumPositionCorrection,evidence.maximumPositionCorrection);
        s.minimumJ = std::min(
            s.minimumJ,evidence.unconstrainedStep.minimumDeformationDeterminant);
    }

    double meanX=0.0, sq=0.0;
    std::size_t interior=0;
    double topError=0.0;
    for(const auto& p:particles) {
        const auto& ref=findInitial(p.id);
        if(ref.restPosition.y>bottomY+1.0e-12 && ref.restPosition.y<topY-1.0e-12) {
            const double dx=p.position.x-ref.restPosition.x;
            meanX+=dx; sq+=dx*dx; ++interior;
        }
        if(std::abs(ref.restPosition.y-topY)<1.0e-12) {
            const auto target=ref.restPosition+Vec3{totalShear,0,0};
            topError=std::max(topError,vulkax::math::length(p.position-target));
        }
    }
    if(interior==0) throw std::runtime_error("driven slab has no interior particles");
    s.finalInteriorMeanX=meanX/static_cast<double>(interior);
    s.finalInteriorRmsX=std::sqrt(sq/static_cast<double>(interior));
    s.finalTopPositionError=topError;
    return s;
}

void writeRow(std::ofstream& out,const RunSummary& s) {
    out<<s.label<<','<<std::setprecision(17)
       <<s.young<<','<<s.poisson<<','<<s.peakShear<<','
       <<s.accumulatedConstraintImpulse<<','
       <<s.maximumMomentumAccountingError<<','
       <<s.maximumPositionCorrection<<','
       <<s.minimumJ<<','
       <<s.finalInteriorMeanX<<','
       <<s.finalInteriorRmsX<<','
       <<s.finalTopPositionError<<",synthetic\n";
}

} // namespace

int main(int argc,char** argv) {
    const std::filesystem::path outDir =
        argc>1 ? std::filesystem::path(argv[1]) : "build/driven-slab";
    std::filesystem::create_directories(outDir);

    const auto soft=runCase("soft",2.0e4,0.30);
    const auto hard=runCase("hard",8.0e4,0.30);

    std::ofstream out(outDir/"driven_slab.csv");
    if(!out) throw std::runtime_error("failed to create driven slab output");
    out<<"case,young_pa,poisson,peak_shear,accumulated_constraint_impulse,"
          "max_momentum_accounting_error,max_position_correction,min_J,"
          "final_interior_mean_x,final_interior_rms_x,final_top_position_error,provenance\n";
    writeRow(out,soft); writeRow(out,hard);
    out.close();

    std::ofstream meta(outDir/"summary.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.driven_slab_probe\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic\",\n"
        <<"  \"boundary\": \"bottom fixed; top prescribed 6 mm x-shear; 8 mm? no, 80 mm slab height\",\n"
        <<"  \"warning\": \"Boundary-validation experiment only; constraint impulse is not a calibrated force measurement\"\n"
        <<"}\n";

    std::cout<<std::setprecision(10)
             <<"soft_impulse="<<soft.accumulatedConstraintImpulse<<"\n"
             <<"hard_impulse="<<hard.accumulatedConstraintImpulse<<"\n"
             <<"impulse_ratio="<<hard.accumulatedConstraintImpulse/std::max(soft.accumulatedConstraintImpulse,1e-30)<<"\n"
             <<"soft_interior_rms_x="<<soft.finalInteriorRmsX<<"\n"
             <<"hard_interior_rms_x="<<hard.finalInteriorRmsX<<"\n"
             <<"soft_min_J="<<soft.minimumJ<<" hard_min_J="<<hard.minimumJ<<"\n";
    return 0;
}
