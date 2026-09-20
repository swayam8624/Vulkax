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

std::vector<MpmParticle> makeBody() {
    std::vector<MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double spacing = 0.12;
    constexpr double restVolume = spacing * spacing * spacing;
    constexpr double density = 1000.0;
    for (int iz = 0; iz < 4; ++iz)
        for (int iy = 0; iy < 4; ++iy)
            for (int ix = 0; ix < 4; ++ix) {
                MpmParticle p;
                p.id = id++;
                p.restPosition = {
                    (static_cast<double>(ix) - 1.5) * spacing,
                    (static_cast<double>(iy) - 1.5) * spacing,
                    (static_cast<double>(iz) - 1.5) * spacing,
                };
                p.position = p.restPosition;
                p.restVolume = restVolume;
                p.mass = density * restVolume;
                particles.push_back(p);
            }
    return particles;
}

vulkax::gaussian::GaussianSplat splat(Vec3 p) {
    vulkax::gaussian::GaussianSplat s;
    s.position = p;
    s.logScale = {std::log(0.06),std::log(0.045),std::log(0.035)};
    s.rotation = {1.0,0.0,0.0,0.0};
    s.opacityLogit = 4.0;
    return s;
}

GaussianCloud makeWorld() {
    GaussianCloud w;
    w.splats.push_back(splat({-0.08, 0.04, 0.02}));
    w.splats.push_back(splat({ 0.09,-0.06, 0.03}));
    w.splats.push_back(splat({ 0.04, 0.10,-0.07}));
    w.splats.push_back(splat({-0.05,-0.08,-0.06}));
    w.splats.push_back(splat({ 0.76, 0.55,-0.30}));
    return w;
}

MpmGridSettings makeGrid() {
    MpmGridSettings g;
    g.origin = {-0.80,-0.80,-0.80};
    g.nx = 22; g.ny = 22; g.nz = 22;
    g.cellSize = 0.08;
    g.boundaryCells = 0;
    return g;
}

struct Trace {
    std::vector<std::vector<Vec3>> positions;
    double maxEnergyDrift{};
    double minJ{1.0};
};

Trace simulate(double young, double poisson, const Matrix3& deformation,
               double dt, std::size_t steps,
               MpmTransferScheme transfer = MpmTransferScheme::APIC) {
    NonlinearDeformableWorldSettings settings;
    settings.steps = steps;
    settings.dt = dt;
    settings.material = {1000.0, young, poisson};
    settings.initialDeformation = deformation;
    settings.couplingNeighborCount = 20;
    settings.transferScheme = transfer;

    Trace trace;
    trace.positions.resize(steps + 1U);
    const auto result = vulkax::research::runNonlinearDeformableWorld(
        makeWorld(), {0,1,2,3}, makeBody(), makeGrid(), settings, {},
        [&](const auto& frame, const GaussianCloud&, const std::vector<MpmParticle>& particles) {
            auto& dst = trace.positions.at(frame.step);
            dst.reserve(particles.size());
            for (const auto& p : particles) dst.push_back(p.position);
        });
    trace.maxEnergyDrift = result.maximumRelativeMechanicalEnergyDrift;
    trace.minJ = result.minimumDeformationDeterminant;
    return trace;
}

double rms(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    if (a.size()!=b.size() || a.empty()) throw std::runtime_error("invalid RMS states");
    double sum=0.0;
    for (std::size_t i=0;i<a.size();++i) {
        const Vec3 d=a[i]-b[i];
        sum += vulkax::math::dot(d,d);
    }
    return std::sqrt(sum/static_cast<double>(a.size()));
}

double trajectoryRms(const Trace& a, const Trace& b, const std::vector<std::size_t>& steps) {
    double sum=0.0;
    for (const auto step:steps) {
        const double e=rms(a.positions.at(step),b.positions.at(step));
        sum += e*e;
    }
    return std::sqrt(sum/static_cast<double>(steps.size()));
}

void write(std::ofstream& out, const std::string& experiment, const std::string& scenario,
           const std::string& metric, double value) {
    out << experiment << ',' << scenario << ',' << metric << ','
        << std::setprecision(17) << value << ",synthetic\n";
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path outDir =
        argc > 1 ? std::filesystem::path(argv[1]) : "build/solver-counterfactual";
    std::filesystem::create_directories(outDir);
    std::ofstream out(outDir/"solver_counterfactual.csv");
    if(!out) throw std::runtime_error("cannot create solver counterfactual output");
    out << "experiment,scenario,metric,value,provenance\n";

    constexpr double truthE=15000.0;
    constexpr double truthNu=0.35;
    constexpr double wrongNu=0.08;
    constexpr double dt=2.0e-4;
    constexpr std::size_t steps=40;

    const Matrix3 trainDef{
        1.035, 0.010, 0.000,
        0.000, 0.985, 0.004,
        0.000, 0.000, 1.005
    };
    const Matrix3 interventionDef{
        1.085, 0.085, 0.010,
        0.020, 0.935, 0.070,
        0.000, 0.025, 1.025
    };

    const Trace truthTrain=simulate(truthE,truthNu,trainDef,dt,steps);
    const std::vector<std::size_t> fitSteps{5,10,15};
    const std::vector<std::size_t> heldSteps{25,30,40};

    double bestE=0.0;
    double bestFit=std::numeric_limits<double>::infinity();
    Trace bestTrace;
    for(int q=0;q<=40;++q) {
        const double candidateE=8000.0+500.0*static_cast<double>(q);
        Trace candidate=simulate(candidateE,wrongNu,trainDef,dt,steps);
        const double fit=trajectoryRms(candidate,truthTrain,fitSteps);
        write(out,"model_mismatch_fit","E="+std::to_string(static_cast<int>(candidateE)),"fit_rms_m",fit);
        if(fit<bestFit) {
            bestFit=fit;
            bestE=candidateE;
            bestTrace=std::move(candidate);
        }
    }

    const double held=trajectoryRms(bestTrace,truthTrain,heldSteps);
    const Trace truthCf=simulate(truthE,truthNu,interventionDef,dt,steps);
    const Trace predCf=simulate(bestE,wrongNu,interventionDef,dt,steps);
    const double cf=trajectoryRms(predCf,truthCf,{10,20,30,40});

    write(out,"model_mismatch_summary","best","truth_E_pa",truthE);
    write(out,"model_mismatch_summary","best","truth_nu",truthNu);
    write(out,"model_mismatch_summary","best","assumed_wrong_nu",wrongNu);
    write(out,"model_mismatch_summary","best","fitted_E_pa",bestE);
    write(out,"model_mismatch_summary","best","fit_rms_m",bestFit);
    write(out,"model_mismatch_summary","best","heldout_same_intervention_rms_m",held);
    write(out,"model_mismatch_summary","best","counterfactual_new_deformation_rms_m",cf);
    write(out,"model_mismatch_summary","best","counterfactual_to_heldout_ratio",cf/std::max(held,1.0e-15));
    write(out,"model_mismatch_summary","truth","max_energy_drift",truthTrain.maxEnergyDrift);
    write(out,"model_mismatch_summary","truth","minimum_J",truthTrain.minJ);

    // Parameter–numerics confounding on the actual nonlinear Vulkax solver:
    // fine-dt APIC is synthetic truth; each coarser dt re-fits E from final state.
    constexpr double horizon=0.004;
    const Trace numericalTruth=simulate(truthE,truthNu,trainDef,1.0e-4,40);
    const auto& truthFinal=numericalTruth.positions.back();
    const std::array<double,4> dts{1.0e-4,2.0e-4,4.0e-4,5.0e-4};
    for(double coarseDt:dts) {
        const auto coarseSteps=static_cast<std::size_t>(std::llround(horizon/coarseDt));
        double localBestE=0.0;
        double localBest=std::numeric_limits<double>::infinity();
        for(int q=0;q<=40;++q) {
            const double candidateE=10000.0+250.0*static_cast<double>(q);
            Trace candidate=simulate(candidateE,truthNu,trainDef,coarseDt,coarseSteps);
            const double loss=rms(candidate.positions.back(),truthFinal);
            if(loss<localBest) { localBest=loss; localBestE=candidateE; }
        }
        const std::string scenario="dt="+std::to_string(coarseDt);
        write(out,"numerics_confounding",scenario,"fitted_E_pa",localBestE);
        write(out,"numerics_confounding",scenario,"relative_E_error",std::abs(localBestE-truthE)/truthE);
        write(out,"numerics_confounding",scenario,"final_state_rms_m",localBest);
    }

    out.close();
    std::ofstream summary(outDir/"summary.json");
    summary << "{\n"
            << "  \"schema\": \"vulkax.solver_counterfactual_probe\",\n"
            << "  \"version\": 1,\n"
            << "  \"provenance\": \"synthetic\",\n"
            << "  \"solver\": \"Vulkax nonlinear APIC/MPM + compressible Neo-Hookean\",\n"
            << "  \"warning\": \"Exploratory synthetic evidence; no novelty or real-world material claim\"\n"
            << "}\n";
    std::cout << std::setprecision(10)
              << "fitted_E_pa=" << bestE << '\n'
              << "fit_rms_m=" << bestFit << '\n'
              << "heldout_same_intervention_rms_m=" << held << '\n'
              << "counterfactual_new_deformation_rms_m=" << cf << '\n'
              << "counterfactual_to_heldout_ratio=" << cf/std::max(held,1.0e-15) << '\n';
    return 0;
}
