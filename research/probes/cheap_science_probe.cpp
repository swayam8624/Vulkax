#include "vulkax/world/reality_loop.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace vulkax::world;

ParameterAddress global(std::string name) {
    return {ParameterSpace::Global, std::nullopt, std::move(name)};
}

double cubicSpringDisplacement(double force, double k, double cubic) {
    double x = force / std::max(k, 1.0e-12);
    for (int i = 0; i < 40; ++i) {
        const double f = k * x + cubic * x * x * x - force;
        const double df = k + 3.0 * cubic * x * x;
        x -= f / df;
    }
    return x;
}

struct OscState { double x; double v; };

OscState rk4Step(OscState s, double dt, double k, double c) {
    const auto deriv = [=](OscState q) { return OscState{q.v, -k*q.x - c*q.v}; };
    const OscState a = deriv(s);
    const OscState b = deriv({s.x + 0.5*dt*a.x, s.v + 0.5*dt*a.v});
    const OscState d = deriv({s.x + 0.5*dt*b.x, s.v + 0.5*dt*b.v});
    const OscState e = deriv({s.x + dt*d.x, s.v + dt*d.v});
    return {
        s.x + dt*(a.x + 2.0*b.x + 2.0*d.x + e.x)/6.0,
        s.v + dt*(a.v + 2.0*b.v + 2.0*d.v + e.v)/6.0
    };
}

OscState eulerStep(OscState s, double dt, double k, double c) {
    return {s.x + dt*s.v, s.v + dt*(-k*s.x - c*s.v)};
}

double simulateX(double t, double dt, double k, double c, bool rk4) {
    OscState s{1.0, 0.0};
    double now = 0.0;
    while (now + 0.5*dt < t) {
        s = rk4 ? rk4Step(s, dt, k, c) : eulerStep(s, dt, k, c);
        now += dt;
    }
    return s.x;
}

void csv(std::ofstream& out, const std::string& probe, const std::string& hypothesis,
         const std::string& scenario, const std::string& metric, double value,
         const std::string& provenance = "synthetic") {
    out << probe << ',' << hypothesis << ',' << scenario << ',' << metric << ','
        << std::setprecision(17) << value << ',' << provenance << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path outDir = argc > 1 ? argv[1] : "build/research-cheap-probes";
    std::filesystem::create_directories(outDir);
    std::ofstream out(outDir / "cheap_probes.csv");
    out << "probe,hypothesis,scenario,metric,value,provenance\n";

    // H3: controlled positive/negative identifiability check using the live WorldIR API.
    {
        const auto scale = global("pixels_per_metre");
        const auto gravity = global("gravity");
        WorldIR world;
        world.id = "cheap-h3-scale-gravity";
        world.globalParameters = {{"pixels_per_metre",100.0},{"gravity",9.8}};
        world.parameterBeliefs.push_back({scale,50.0,150.0,std::nullopt,EvidenceClass::ModelProxy,"synthetic"});
        world.parameterBeliefs.push_back({gravity,5.0,15.0,std::nullopt,EvidenceClass::ModelProxy,"synthetic"});
        for (int i=1;i<=4;++i) {
            ObservationRecord obs;
            obs.id = "px" + std::to_string(i);
            obs.observableId = obs.id;
            obs.values = {100.0*9.8*static_cast<double>(i*i)};
            obs.standardDeviation = {1.0};
            obs.evidence = EvidenceClass::Synthetic;
            obs.source = "analytic-scale-gravity";
            obs.space = ObservationSpace::ImagePixels;
            world.observations.push_back(obs);
        }
        const ForwardModel unanchored = [=](const WorldIR& w) {
            std::vector<ObservationPrediction> p;
            const double s=*w.parameterValue(scale), g=*w.parameterValue(gravity);
            for (int i=1;i<=4;++i) p.push_back({"px"+std::to_string(i),{s*g*static_cast<double>(i*i)},ObservationSpace::ImagePixels});
            return p;
        };
        const auto r0 = analyzeLocalIdentifiability(world,{scale,gravity},unanchored);
        csv(out,"identifiability","H003","unanchored","rank",static_cast<double>(r0.numericalRank));
        csv(out,"identifiability","H003","unanchored","condition_number",r0.conditionNumber);

        ObservationRecord anchor;
        anchor.id="scale_anchor"; anchor.observableId="scale_anchor"; anchor.values={100.0};
        anchor.standardDeviation={0.25}; anchor.evidence=EvidenceClass::Synthetic;
        anchor.source="synthetic-metric-anchor"; anchor.space=ObservationSpace::Dimensionless;
        world.observations.push_back(anchor);
        const ForwardModel anchored = [=](const WorldIR& w) {
            std::vector<ObservationPrediction> p;
            const double s=*w.parameterValue(scale), g=*w.parameterValue(gravity);
            for (int i=1;i<=4;++i) p.push_back({"px"+std::to_string(i),{s*g*static_cast<double>(i*i)},ObservationSpace::ImagePixels});
            p.push_back({"scale_anchor",{s},ObservationSpace::Dimensionless});
            return p;
        };
        const auto r1 = analyzeLocalIdentifiability(world,{scale,gravity},anchored);
        csv(out,"identifiability","H003","anchored","rank",static_cast<double>(r1.numericalRank));
        csv(out,"identifiability","H003","anchored","condition_number",r1.conditionNumber);
        for (std::size_t i=0;i<r1.singularValuesDescending.size();++i)
            csv(out,"identifiability","H003","anchored","singular_"+std::to_string(i),r1.singularValuesDescending[i]);
    }

    // H2: same-experiment held-out replay can look good while an unseen intervention fails.
    {
        constexpr double kTruth=10.0, cubicTruth=20.0;
        const std::vector<double> fitLoads{0.25,0.5,0.75};
        double kFitSum=0.0;
        for (double f:fitLoads) kFitSum += f / cubicSpringDisplacement(f,kTruth,cubicTruth);
        const double kFit=kFitSum/static_cast<double>(fitLoads.size());
        const double heldLoad=0.6;
        const double counterLoad=4.0;
        const double heldTruth=cubicSpringDisplacement(heldLoad,kTruth,cubicTruth);
        const double heldPred=heldLoad/kFit;
        const double cfTruth=cubicSpringDisplacement(counterLoad,kTruth,cubicTruth);
        const double cfPred=counterLoad/kFit;
        const double heldRel=std::abs(heldPred-heldTruth)/std::max(std::abs(heldTruth),1.0e-12);
        const double cfRel=std::abs(cfPred-cfTruth)/std::max(std::abs(cfTruth),1.0e-12);
        csv(out,"heldout_vs_counterfactual","H002","linear-fit-to-cubic-truth","fitted_k",kFit);
        csv(out,"heldout_vs_counterfactual","H002","same-regime-heldout","relative_error",heldRel);
        csv(out,"heldout_vs_counterfactual","H002","unseen-large-force","relative_error",cfRel);
        csv(out,"heldout_vs_counterfactual","H002","comparison","counterfactual_to_heldout_error_ratio",cfRel/std::max(heldRel,1.0e-15));
    }

    // H6: estimate a local counterfactual trust radius around a nonlinear spring state.
    {
        constexpr double k=10.0, cubic=20.0, f0=1.0;
        const double x0=cubicSpringDisplacement(f0,k,cubic);
        const double dxdf=1.0/(k+3.0*cubic*x0*x0);
        double firstFail=std::numeric_limits<double>::quiet_NaN();
        for (int i=1;i<=200;++i) {
            const double df=0.01*static_cast<double>(i);
            const double truth=cubicSpringDisplacement(f0+df,k,cubic)-x0;
            const double linear=dxdf*df;
            const double rel=std::abs(truth-linear)/std::max(std::abs(truth),1.0e-12);
            csv(out,"trust_region","H006","delta_force","relative_linearization_error",rel);
            if (!std::isfinite(firstFail) && rel>0.05) firstFail=df;
        }
        csv(out,"trust_region","H006","threshold_5pct","first_failed_delta_force",firstFail);
    }

    // H9: fit stiffness with a coarse numerical solver to truth generated at high fidelity.
    {
        constexpr double kTruth=25.0, cTruth=1.5;
        const std::vector<double> sampleTimes{0.2,0.4,0.6,0.8,1.0};
        const std::vector<double> dts{0.001,0.005,0.01,0.02,0.04};
        for (double dt:dts) {
            double bestK=0.0, bestLoss=std::numeric_limits<double>::infinity();
            for (int q=0;q<=400;++q) {
                const double k=15.0+0.05*static_cast<double>(q);
                double loss=0.0;
                for (double t:sampleTimes) {
                    const double truth=simulateX(t,1.0e-4,kTruth,cTruth,true);
                    const double pred=simulateX(t,dt,k,cTruth,false);
                    const double e=pred-truth;
                    loss+=e*e;
                }
                if (loss<bestLoss) {bestLoss=loss; bestK=k;}
            }
            csv(out,"discretization_bias","H009","forward_euler_dt="+std::to_string(dt),"fitted_k",bestK);
            csv(out,"discretization_bias","H009","forward_euler_dt="+std::to_string(dt),"relative_k_error",std::abs(bestK-kTruth)/kTruth);
            csv(out,"discretization_bias","H009","forward_euler_dt="+std::to_string(dt),"fit_sse",bestLoss);
        }
    }

    // H12: positive-control low-rank influence spectrum through the live identifiability implementation.
    {
        constexpr int n=12;
        WorldIR w;
        std::vector<ParameterAddress> params;
        for (int j=0;j<n;++j) {
            auto p=global("p"+std::to_string(j));
            params.push_back(p);
            w.globalParameters[p.name]=0.1*static_cast<double>(j+1);
            w.parameterBeliefs.push_back({p,-2.0,2.0,std::nullopt,EvidenceClass::Synthetic,"constructed-low-rank-control"});
        }
        for (int i=0;i<n;++i) {
            ObservationRecord o;
            o.id="y"+std::to_string(i); o.observableId=o.id; o.values={0.0};
            o.standardDeviation={1.0}; o.evidence=EvidenceClass::Synthetic;
            o.source="constructed-low-rank-control"; w.observations.push_back(o);
        }
        const ForwardModel f=[=](const WorldIR& c) {
            std::vector<ObservationPrediction> outp;
            for (int i=0;i<n;++i) {
                double y=0.0;
                for (int j=0;j<n;++j) {
                    const double u1=std::sin(0.31*(i+1)), v1=std::cos(0.17*(j+1));
                    const double u2=std::cos(0.23*(i+1)), v2=std::sin(0.29*(j+1));
                    const double u3=std::sin(0.11*(i+1)*(i+1)), v3=std::cos(0.13*(j+1)*(j+1));
                    const double a=3.0*u1*v1 + 1.5*u2*v2 + 0.6*u3*v3 + 1.0e-5*(i==j ? 1.0 : 0.0);
                    y += a * *c.parameterValue(params[j]);
                }
                outp.push_back({"y"+std::to_string(i),{y},ObservationSpace::Dimensionless});
            }
            return outp;
        };
        const auto r=analyzeLocalIdentifiability(w,params,f);
        double total=0.0, top3=0.0;
        for (std::size_t i=0;i<r.singularValuesDescending.size();++i) {
            const double s=r.singularValuesDescending[i];
            total+=s*s; if (i<3) top3+=s*s;
            csv(out,"influence_spectrum","H012","constructed-positive-control","singular_"+std::to_string(i),s);
        }
        csv(out,"influence_spectrum","H012","constructed-positive-control","top3_energy_fraction",top3/std::max(total,1.0e-30));
    }

    // H16/H17: calibrate a simple certificate against actual nonlinear counterfactual correctness.
    {
        constexpr double k=10.0, cubic=20.0, f0=1.0;
        const double x0=cubicSpringDisplacement(f0,k,cubic);
        const double dxdf=1.0/(k+3.0*cubic*x0*x0);
        const double h=0.05;
        const double xp=cubicSpringDisplacement(f0+h,k,cubic);
        const double xm=cubicSpringDisplacement(f0-h,k,cubic);
        const double curvature=(xp-2.0*x0+xm)/(h*h);
        for (double threshold : {0.02,0.05,0.10,0.20}) {
            int tp=0,fp=0,tn=0,fn=0;
            for (int i=1;i<=400;++i) {
                const double df=0.005*static_cast<double>(i);
                const double truth=cubicSpringDisplacement(f0+df,k,cubic)-x0;
                const double linear=dxdf*df;
                const double actualRel=std::abs(truth-linear)/std::max(std::abs(truth),1.0e-12);
                const double estimatedSecondOrder=0.5*std::abs(curvature)*df*df;
                const double certificateRel=estimatedSecondOrder/std::max(std::abs(linear),1.0e-12);
                const bool accept=certificateRel<=threshold;
                const bool actuallyCorrect=actualRel<=0.05;
                if (accept && actuallyCorrect) ++tp;
                else if (accept && !actuallyCorrect) ++fp;
                else if (!accept && actuallyCorrect) ++fn;
                else ++tn;
            }
            const double far=static_cast<double>(fp)/std::max(fp+tn,1);
            const double frr=static_cast<double>(fn)/std::max(fn+tp,1);
            csv(out,"certificate_calibration","H016","threshold="+std::to_string(threshold),"false_accept_rate",far);
            csv(out,"certificate_calibration","H017","threshold="+std::to_string(threshold),"false_reject_rate",frr);
        }
    }

    out.close();

    std::ofstream summary(outDir / "summary.json");
    summary << "{\n"
            << "  \"schema\": \"vulkax.cheap_science_probe\",\n"
            << "  \"version\": 1,\n"
            << "  \"provenance\": \"synthetic\",\n"
            << "  \"warning\": \"Tooling/falsification scaffold only; not publication evidence\",\n"
            << "  \"probes\": [\"H003_identifiability\",\"H002_heldout_vs_counterfactual\",\"H006_trust_region\",\"H009_discretization_bias\",\"H012_influence_spectrum\",\"H016_H017_certificate_calibration\"]\n"
            << "}\n";
    return 0;
}
