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
using vulkax::research::dcs::AnnihilatingStencil;
using vulkax::research::dcs::Response;
using vulkax::solvers::Matrix3;
using vulkax::solvers::MpmGridSettings;
using vulkax::solvers::MpmParticle;
using vulkax::solvers::MpmTransferScheme;

constexpr double kHorizon = 0.0032;
constexpr double kTruthDt = 2.5e-5;
constexpr double kCandidateDt = 1.0e-4;
constexpr double kCoarseDt = 4.0e-4;
constexpr double kCalibrationAmplitude = 0.025;
constexpr double kHeldoutAmplitude = 0.040;
constexpr double kWitnessAmplitude = 0.055;
constexpr double kTargetAmplitude = 0.080;
constexpr double kNoise = 2.0e-5;

std::vector<MpmParticle> body() {
    std::vector<MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double h = 0.12;
    constexpr double volume = h*h*h;
    constexpr double density = 1000.0;
    for (int z=0;z<4;++z) for (int y=0;y<4;++y) for (int x=0;x<4;++x) {
        MpmParticle p;
        p.id=id++;
        p.restPosition={(x-1.5)*h,(y-1.5)*h,(z-1.5)*h};
        p.position=p.restPosition;
        p.restVolume=volume;
        p.mass=density*volume;
        particles.push_back(p);
    }
    return particles;
}

vulkax::gaussian::GaussianSplat splat(Vec3 position) {
    vulkax::gaussian::GaussianSplat s;
    s.position=position;
    s.logScale={std::log(0.06),std::log(0.045),std::log(0.035)};
    s.rotation={1.0,0.0,0.0,0.0};
    s.opacityLogit=4.0;
    return s;
}

GaussianCloud world() {
    GaussianCloud w;
    w.splats.push_back(splat({-0.08,0.04,0.02}));
    w.splats.push_back(splat({0.09,-0.06,0.03}));
    w.splats.push_back(splat({0.04,0.10,-0.07}));
    w.splats.push_back(splat({-0.05,-0.08,-0.06}));
    return w;
}

MpmGridSettings grid() {
    MpmGridSettings g;
    g.origin={-0.8,-0.8,-0.8};
    g.nx=22; g.ny=22; g.nz=22;
    g.cellSize=0.08;
    g.boundaryCells=0;
    return g;
}

Matrix3 deformation(double shear,double axial) {
    return {
        1.0+axial, shear, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };
}

struct Variant {
    std::string name;
    MpmTransferScheme scheme{MpmTransferScheme::APIC};
    double dt{kCandidateDt};
    bool constrainNu{};
};

struct Fit {
    double E{};
    double nu{};
    double objective{std::numeric_limits<double>::infinity()};
};

Response simulate(
    double E,double nu,double shear,double axial,
    MpmTransferScheme scheme,double dt) {
    NonlinearDeformableWorldSettings settings;
    settings.steps=static_cast<std::size_t>(std::llround(kHorizon/dt));
    settings.dt=dt;
    settings.material={1000.0,E,nu};
    settings.initialDeformation=deformation(shear,axial);
    settings.couplingNeighborCount=20;
    settings.transferScheme=scheme;

    std::vector<MpmParticle> finalParticles;
    (void)vulkax::research::runNonlinearDeformableWorld(
        world(),{0,1,2,3},body(),grid(),settings,{},
        [&](const auto& frame,const GaussianCloud&,const std::vector<MpmParticle>& particles) {
            if (frame.step==settings.steps) finalParticles=particles;
        });
    if (finalParticles.size()!=64U)
        throw std::runtime_error("DCS solver-native probe did not capture final particle state");

    constexpr std::array<std::size_t,4> markers{5U,18U,45U,58U};
    Response out;
    out.reserve(markers.size()*3U);
    for (const auto marker:markers) {
        const auto& p=finalParticles.at(marker).position;
        out.push_back(p.x); out.push_back(p.y); out.push_back(p.z);
    }
    return out;
}

Response addNoise(Response response,int salt) {
    for (std::size_t i=0;i<response.size();++i) {
        const double phase=0.731*static_cast<double>(i+1)+1.117*static_cast<double>(salt+1);
        response[i]+=kNoise*(0.61*std::sin(phase)+0.39*std::cos(1.37*phase));
    }
    return response;
}

double rms(const std::vector<Response>& lhs,const std::vector<Response>& rhs) {
    if (lhs.size()!=rhs.size() || lhs.empty())
        throw std::invalid_argument("DCS dataset response count mismatch");
    long double sum=0.0L;
    std::size_t count=0;
    for (std::size_t i=0;i<lhs.size();++i) {
        if (lhs[i].size()!=rhs[i].size())
            throw std::invalid_argument("DCS dataset response width mismatch");
        for (std::size_t j=0;j<lhs[i].size();++j) {
            const long double d=static_cast<long double>(lhs[i][j])-rhs[i][j];
            sum+=d*d;
            ++count;
        }
    }
    return std::sqrt(static_cast<double>(sum/static_cast<long double>(count)));
}

std::vector<std::pair<double,double>> calibrationInterventions() {
    return {
        { kCalibrationAmplitude,0.0},
        {-kCalibrationAmplitude,0.0},
        {0.0, kCalibrationAmplitude},
        {0.0,-kCalibrationAmplitude}
    };
}

std::vector<std::pair<double,double>> heldoutInterventions() {
    return {
        { kHeldoutAmplitude, kHeldoutAmplitude},
        {-kHeldoutAmplitude, kHeldoutAmplitude},
        { kHeldoutAmplitude,-kHeldoutAmplitude}
    };
}

std::vector<Response> simulateDataset(
    double E,double nu,MpmTransferScheme scheme,double dt,
    const std::vector<std::pair<double,double>>& interventions) {
    std::vector<Response> out;
    out.reserve(interventions.size());
    for (const auto [s,a]:interventions)
        out.push_back(simulate(E,nu,s,a,scheme,dt));
    return out;
}

Fit fitVariant(
    const Variant& variant,
    const std::vector<Response>& calibrationTruth) {
    constexpr std::array<double,3> Es{12000.0,15000.0,18000.0};
    constexpr std::array<double,3> Nus{0.20,0.30,0.40};
    Fit best;
    for (const double E:Es) {
        if (variant.constrainNu) {
            const double nu=0.10;
            const auto prediction=simulateDataset(
                E,nu,variant.scheme,variant.dt,calibrationInterventions());
            const double objective=rms(prediction,calibrationTruth);
            if (objective<best.objective) best={E,nu,objective};
        } else {
            for (const double nu:Nus) {
                const auto prediction=simulateDataset(
                    E,nu,variant.scheme,variant.dt,calibrationInterventions());
                const double objective=rms(prediction,calibrationTruth);
                if (objective<best.objective) best={E,nu,objective};
            }
        }
    }
    return best;
}

Response witness(
    double E,double nu,MpmTransferScheme scheme,double dt) {
    const double a=kWitnessAmplitude;
    AnnihilatingStencil stencil;
    stencil.order=2;
    stencil.points={{{ a, a}},{{ a,-a}},{{-a, a}},{{-a,-a}}};
    stencil.weights={0.25,-0.25,-0.25,0.25};
    std::vector<Response> responses;
    responses.reserve(4);
    for (const auto& point:stencil.points)
        responses.push_back(simulate(
            E,nu,point.coordinates[0],point.coordinates[1],scheme,dt));
    return vulkax::research::dcs::applyAnnihilatingStencil(responses,stencil,1.0e-12);
}

Response target(double E,double nu,MpmTransferScheme scheme,double dt) {
    return simulate(E,nu,kTargetAmplitude,kTargetAmplitude,scheme,dt);
}

} // namespace

int main(int argc,char**argv) {
    const std::filesystem::path outDir =
        argc>1?argv[1]:"build/dcs-solver-native";
    std::filesystem::create_directories(outDir);
    std::ofstream out(outDir/"cases.csv");
    out<<"truth_id,variant,truth_E_pa,truth_nu,fit_E_pa,fit_nu,fit_objective_m,"
          "heldout_rmse_m,witness_error_m,target_error_m,scheme,dt_s,provenance\n";

    const std::array<double,2> truthEs{13750.0,16250.0};
    const std::array<double,2> truthNus{0.24,0.36};
    const std::array<Variant,4> variants{{
        {"apic",MpmTransferScheme::APIC,kCandidateDt,false},
        {"pic",MpmTransferScheme::PIC,kCandidateDt,false},
        {"constrained_nu",MpmTransferScheme::APIC,kCandidateDt,true},
        {"coarse_apic",MpmTransferScheme::APIC,kCoarseDt,false},
    }};

    int truthId=0;
    int rowCount=0;
    for (const double truthE:truthEs) for (const double truthNu:truthNus) {
        ++truthId;
        auto calibrationTruth=simulateDataset(
            truthE,truthNu,MpmTransferScheme::APIC,kTruthDt,calibrationInterventions());
        for (std::size_t i=0;i<calibrationTruth.size();++i)
            calibrationTruth[i]=addNoise(std::move(calibrationTruth[i]),100*truthId+static_cast<int>(i));

        const auto heldoutTruth=simulateDataset(
            truthE,truthNu,MpmTransferScheme::APIC,kTruthDt,heldoutInterventions());
        const auto truthWitness=witness(
            truthE,truthNu,MpmTransferScheme::APIC,kTruthDt);
        const auto truthTarget=target(
            truthE,truthNu,MpmTransferScheme::APIC,kTruthDt);

        for (const auto& variant:variants) {
            const auto fit=fitVariant(variant,calibrationTruth);
            const auto heldoutPrediction=simulateDataset(
                fit.E,fit.nu,variant.scheme,variant.dt,heldoutInterventions());
            const double heldoutError=rms(heldoutPrediction,heldoutTruth);
            const auto candidateWitness=witness(
                fit.E,fit.nu,variant.scheme,variant.dt);
            const double witnessError=
                vulkax::research::dcs::responseDistance(candidateWitness,truthWitness)/
                std::sqrt(static_cast<double>(truthWitness.size()));
            const auto candidateTarget=target(
                fit.E,fit.nu,variant.scheme,variant.dt);
            const double targetError=
                vulkax::research::dcs::responseDistance(candidateTarget,truthTarget)/
                std::sqrt(static_cast<double>(truthTarget.size()));

            out<<truthId<<','<<variant.name<<','<<std::setprecision(17)
               <<truthE<<','<<truthNu<<','<<fit.E<<','<<fit.nu<<','<<fit.objective<<','
               <<heldoutError<<','<<witnessError<<','<<targetError<<','
               <<(variant.scheme==MpmTransferScheme::APIC?"APIC":"PIC")<<','
               <<variant.dt<<",synthetic-dcs-solver-native-discovery\n";
            ++rowCount;
            std::cout<<"DCS_SOLVER truth="<<truthId<<" variant="<<variant.name
                     <<" fit="<<fit.objective<<" held="<<heldoutError
                     <<" witness="<<witnessError<<" target="<<targetError<<"\n";
        }
    }
    out.close();
    std::ofstream meta(outDir/"summary.json");
    meta<<"{\n"
        <<"  \"schema\": \"vulkax.dcs.solver_native_discovery\",\n"
        <<"  \"version\": 1,\n"
        <<"  \"provenance\": \"synthetic-dcs-solver-native-discovery\",\n"
        <<"  \"truth_count\": "<<truthId<<",\n"
        <<"  \"candidate_count\": "<<rowCount<<",\n"
        <<"  \"selection_labels_used\": false,\n"
        <<"  \"warning\": \"Discovery probe. Ranking observations here must be frozen before fresh validation.\"\n"
        <<"}\n";
}
