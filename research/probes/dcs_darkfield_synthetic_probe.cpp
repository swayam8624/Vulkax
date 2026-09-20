#include "vulkax/research/dark_field_counterfactual.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using vulkax::research::dcs::AnnihilatingStencil;
using vulkax::research::dcs::Response;

struct Model {
    double jx{};
    double jy{};
    double hxy{};
};

Response response(const Model& m, double a, double b, double noisePhase = 0.0) {
    const double coupled = m.hxy * a * b;
    const double n = 2.0e-4 * std::sin(noisePhase + 1.7*a - 2.3*b);
    return {
        0.75 + m.jx*a + 0.40*m.jy*b + coupled + n,
        -0.15 + 0.25*m.jx*a - 0.60*m.jy*b + 0.50*coupled - 0.5*n,
        0.30 - 0.10*m.jx*a + 0.20*m.jy*b - 0.75*coupled + 0.25*n,
    };
}

double datasetRmse(
    const Model& candidate,
    const Model& truth,
    double amplitude,
    double phase) {
    const std::vector<std::pair<double,double>> points{
        { amplitude, 0.0},
        {-amplitude, 0.0},
        {0.0, amplitude},
        {0.0,-amplitude},
        { amplitude, amplitude},
        {-amplitude, amplitude},
    };
    long double squared = 0.0L;
    std::size_t count = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto [a,b] = points[i];
        const auto y = response(truth,a,b,phase + static_cast<double>(i));
        const auto p = response(candidate,a,b,0.0);
        for (std::size_t c = 0; c < y.size(); ++c) {
            const long double d = static_cast<long double>(p[c]) - y[c];
            squared += d*d;
            ++count;
        }
    }
    return std::sqrt(static_cast<double>(squared / count));
}

Response mixedDarkField(const Model& model, double amplitude, double phase) {
    AnnihilatingStencil stencil;
    stencil.order = 2;
    stencil.points = {
        {{amplitude, amplitude}},
        {{amplitude, 0.0}},
        {{0.0, amplitude}},
        {{0.0, 0.0}},
    };
    stencil.weights = {1.0,-1.0,-1.0,1.0};
    std::vector<Response> responses;
    responses.reserve(stencil.points.size());
    for (std::size_t i = 0; i < stencil.points.size(); ++i) {
        const auto& p = stencil.points[i].coordinates;
        responses.push_back(response(model,p[0],p[1],phase + static_cast<double>(i)));
    }
    return vulkax::research::dcs::applyAnnihilatingStencil(responses,stencil,1.0e-12);
}
}

int main(int argc,char**argv) {
    const std::filesystem::path outDir =
        argc > 1 ? argv[1] : "build/dcs-darkfield-synthetic";
    std::filesystem::create_directories(outDir);
    std::ofstream out(outDir/"cases.csv");
    out << "case_id,amplitude,truth_jx,truth_jy,truth_hxy,"
           "baseline_loss,repair_loss,baseline_witness_error,repair_witness_error,"
           "observation_improved,witness_worsened,deceptive,provenance\n";

    std::size_t deceptiveCount = 0;
    constexpr std::size_t caseCount = 64;
    for (std::size_t i = 0; i < caseCount; ++i) {
        const double t = static_cast<double>(i);
        const double amplitude = 0.16 + 0.0015 * static_cast<double>(i % 9);
        const Model truth{
            1.00 + 0.08*std::sin(0.31*t),
            0.85 + 0.07*std::cos(0.23*t),
            0.55 + 0.10*std::sin(0.17*t + 0.4),
        };

        // Baseline: visibly biased first-order response, but nearly correct coupling.
        const Model baseline{
            truth.jx * (1.0 + 0.080),
            truth.jy * (1.0 - 0.075),
            truth.hxy * (1.0 + 0.050),
        };

        // "Repair": fits the large/easy first-order trajectory much better while
        // silently corrupting the irreducible mixed mechanism.
        const Model repair{
            truth.jx * (1.0 + 0.006),
            truth.jy * (1.0 - 0.006),
            truth.hxy * (1.0 - 0.45),
        };

        const double phase = 0.19*t;
        const double baselineLoss = datasetRmse(baseline,truth,amplitude,phase);
        const double repairLoss = datasetRmse(repair,truth,amplitude,phase);

        const auto truthWitness = mixedDarkField(truth,amplitude,phase);
        const auto baselineWitness = mixedDarkField(baseline,amplitude,0.0);
        const auto repairWitness = mixedDarkField(repair,amplitude,0.0);
        const double baselineWitnessError =
            vulkax::research::dcs::responseDistance(baselineWitness,truthWitness);
        const double repairWitnessError =
            vulkax::research::dcs::responseDistance(repairWitness,truthWitness);

        const auto assessment = vulkax::research::dcs::classifyDeceptiveRepair(
            baselineLoss,repairLoss,
            baselineWitnessError,repairWitnessError,
            1.0e-8);
        if (assessment.deceptive) ++deceptiveCount;

        out << i << ',' << std::setprecision(17)
            << amplitude << ',' << truth.jx << ',' << truth.jy << ',' << truth.hxy << ','
            << baselineLoss << ',' << repairLoss << ','
            << baselineWitnessError << ',' << repairWitnessError << ','
            << (assessment.observationImproved?1:0) << ','
            << (assessment.witnessWorsened?1:0) << ','
            << (assessment.deceptive?1:0)
            << ",synthetic-dcs-positive-control\n";
    }
    out.close();

    std::ofstream meta(outDir/"summary.json");
    meta << "{\n"
         << "  \"schema\": \"vulkax.dcs.synthetic_positive_control\",\n"
         << "  \"version\": 1,\n"
         << "  \"provenance\": \"synthetic-dcs-positive-control\",\n"
         << "  \"case_count\": " << caseCount << ",\n"
         << "  \"deceptive_detected\": " << deceptiveCount << ",\n"
         << "  \"warning\": \"Constructed positive control only; not publication evidence.\"\n"
         << "}\n";

    std::cout << "DCS_POSITIVE_CONTROL cases=" << caseCount
              << " deceptive_detected=" << deceptiveCount << "\n";
}
