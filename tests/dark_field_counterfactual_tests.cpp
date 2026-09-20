#include "vulkax/research/dark_field_counterfactual.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <vector>

int main() {
    using namespace vulkax::research::dcs;

    {
        std::vector<Response> subsets{
            {0.0}, {2.0}, {3.0}, {12.0}
        };
        const auto kappa = counterfactualCumulant(subsets, 2);
        assert(kappa.size() == 1);
        assert(std::abs(kappa[0] - 7.0) < 1.0e-12);
    }

    {
        // f(x,y)=4 + 2x - 3y + 5xy. The mixed stencil annihilates
        // constant and first-order response and exposes only the xy coupling.
        AnnihilatingStencil stencil;
        stencil.order = 2;
        stencil.points = {{{1.0, 1.0}}, {{1.0, 0.0}}, {{0.0, 1.0}}, {{0.0, 0.0}}};
        stencil.weights = {1.0, -1.0, -1.0, 1.0};
        const auto validation = validateAnnihilatingStencil(stencil);
        assert(validation.valid);
        const auto f = [](double x, double y) { return 4.0 + 2.0*x - 3.0*y + 5.0*x*y; };
        std::vector<Response> responses{
            {f(1.0,1.0)}, {f(1.0,0.0)}, {f(0.0,1.0)}, {f(0.0,0.0)}
        };
        const auto dark = applyAnnihilatingStencil(responses, stencil);
        assert(std::abs(dark[0] - 5.0) < 1.0e-12);
    }

    {
        // Third forward difference annihilates polynomial response up to order 2.
        AnnihilatingStencil stencil;
        stencil.order = 3;
        stencil.points = {{{0.0}}, {{1.0}}, {{2.0}}, {{3.0}}};
        stencil.weights = {-1.0, 3.0, -3.0, 1.0};
        const auto validation = validateAnnihilatingStencil(stencil);
        assert(validation.valid);
        std::vector<Response> responses{{0.0}, {1.0}, {8.0}, {27.0}};
        const auto dark = applyAnnihilatingStencil(responses, stencil);
        assert(std::abs(dark[0] - 6.0) < 1.0e-12);
    }

    {
        std::vector<Response> reference{{1.0,2.0},{0.5,0.25},{0.1,0.2}};
        std::vector<Response> candidate{{1.0,2.0},{0.51,0.25},{0.4,0.2}};
        const auto contact = mechanismOrderOfContact(reference, candidate, 0.05);
        assert(contact.separated);
        assert(contact.separatingOrder == 3);
    }

    {
        const auto deceptive = classifyDeceptiveRepair(
            0.20, 0.10,
            0.05, 0.12,
            1.0e-9);
        assert(deceptive.observationImproved);
        assert(deceptive.witnessWorsened);
        assert(deceptive.deceptive);
    }

    {
        const auto flow = analyzeWitnessScaleFlow(
            {0.4, 0.2, 0.1},
            {0.16, 0.04, 0.01});
        assert(flow.finite);
        assert(flow.adjacentLogSlopes.size() == 2);
        assert(std::abs(flow.adjacentLogSlopes.back() - 2.0) < 1.0e-12);
    }

    {
        const double scoreNear = darkFieldDiscriminationScore(
            {{0.0},{0.1}}, 0.01, 0.01, 1.0, 0.01);
        const double scoreFar = darkFieldDiscriminationScore(
            {{0.0},{1.0}}, 0.01, 0.01, 1.0, 0.01);
        assert(scoreFar > scoreNear);
    }

    {
        std::vector<InterventionPoint> points{
            {{1.0,1.0}},{{1.0,-1.0}},{{-1.0,1.0}},{{-1.0,-1.0}}
        };
        std::vector<std::vector<Response>> models(2);
        for (std::size_t m=0;m<models.size();++m) {
            const double coupling=m==0?1.0:-1.0;
            for (const auto& point:points) {
                const double x=point.coordinates[0],y=point.coordinates[1];
                models[m].push_back({4.0+2.0*x-3.0*y+coupling*x*y});
            }
        }
        const auto synthesized=synthesizeAnnihilatingStencil(points,2,models);
        assert(synthesized.momentValidation.valid);
        assert(synthesized.modelDisagreementEnergy > 0.0);
        assert(synthesized.independentNoiseGain > 0.0);
        const auto d0=applyAnnihilatingStencil(models[0],synthesized.stencil);
        const auto d1=applyAnnihilatingStencil(models[1],synthesized.stencil);
        assert(responseDistance(d0,d1)>1.0);

        UncertaintyBudget budget;
        budget.measurementVariance=0.01;
        budget.numericalVariance=0.01;
        budget.repeatVariance=0.01;
        const auto maximin=synthesizeMaximinAnnihilatingStencil(points,2,models,budget);
        assert(maximin.momentValidation.valid);
        assert(maximin.worstCaseStandardizedSeparation>0.0);
        const auto m0=applyAnnihilatingStencil(models[0],maximin.stencil);
        const auto m1=applyAnnihilatingStencil(models[1],maximin.stencil);
        const auto effective=propagateStencilUncertainty(maximin.stencil,budget);
        assert(worstCaseStandardizedSeparation({m0,m1},effective)>0.0);
        double l2sq=0.0,l1=0.0;
        for(const double w:maximin.stencil.weights){l2sq+=w*w;l1+=std::abs(w);}
        assert(std::abs(effective.measurementVariance-budget.measurementVariance*l2sq)<1.0e-12);
        assert(std::abs(effective.repeatVariance-budget.repeatVariance*l2sq)<1.0e-12);
        assert(std::abs(effective.numericalVariance-budget.numericalVariance*l1*l1)<1.0e-12);
    }

    {
        UncertaintyBudget budget;
        budget.measurementVariance=0.01;
        budget.numericalVariance=0.01;
        budget.repeatVariance=0.02;
        const double discrepancy=standardizedDarkFieldDiscrepancy({1.0,1.0},{0.0,0.0},budget);
        assert(std::abs(discrepancy-5.0)<1.0e-12);

        const auto resolution=mechanismResolution(
            {{0.1,0.1},{1.0,1.0},{0.2,0.2}},
            {budget,budget,budget},
            2.0);
        assert(resolution.anyObservableOrder);
        assert(resolution.maximumObservableOrder==2);
        assert(resolution.standardizedSignalByOrder.size()==3);

        const auto jet=estimateJetOrderOfContact(
            {
                {{0.0},{0.0}},
                {{0.0},{0.0}},
                {{0.0},{0.0}},
            },
            {
                {{0.02},{0.01}},
                {{0.03},{0.04}},
                {{1.0},{0.02}},
            },
            {budget,budget,budget},
            2.0);
        assert(jet.separated);
        assert(jet.separatingOrder==3);
        assert(jet.witnessCountByOrder[2]==2);
    }

    {
        AnnihilatingStencil stencil;
        stencil.order=1;
        stencil.points={{{0.0}},{{1.0}}};
        stencil.weights={-0.5,0.5};

        UncertaintyBudget shared;
        shared.measurementVariance=1.0;

        UncertaintyBudget n0;
        n0.numericalVariance=1.0;
        UncertaintyBudget n1;
        n1.numericalVariance=4.0;

        const double separation=worstCasePairAwareStandardizedSeparation(
            {{0.0},{1.0}},shared,{n0,n1},stencil);
        // shared measurement gain = 0.5, numerical L1 gain = 1 for each model.
        assert(std::abs(separation-1.0/std::sqrt(5.5))<1.0e-12);
    }

    {
        std::vector<InterventionPoint> points{
            {{1.0,1.0}},{{1.0,-1.0}},{{-1.0,1.0}},{{-1.0,-1.0}}
        };
        std::vector<std::vector<Response>> models(3);
        const std::array<double,3> couplings{1.0,0.6,-1.0};
        for(std::size_t m=0;m<models.size();++m){
            for(const auto& point:points){
                const double x=point.coordinates[0],y=point.coordinates[1];
                models[m].push_back({2.0+x-y+couplings[m]*x*y});
            }
        }
        UncertaintyBudget shared;
        shared.measurementVariance=1.0e-4;
        shared.repeatVariance=1.0e-4;
        std::vector<UncertaintyBudget> numerical(3);
        numerical[0].numericalVariance=1.0e-5;
        numerical[1].numericalVariance=2.0e-5;
        numerical[2].numericalVariance=5.0e-4;

        const auto synthesized=synthesizePairAwareMaximinStencil(
            points,2,models,shared,numerical);
        assert(synthesized.momentValidation.valid);
        assert(synthesized.worstCaseStandardizedSeparation>0.0);

        std::vector<Response> witnesses;
        for(const auto& model:models)
            witnesses.push_back(applyAnnihilatingStencil(model,synthesized.stencil));
        const double check=worstCasePairAwareStandardizedSeparation(
            witnesses,shared,numerical,synthesized.stencil);
        assert(std::abs(check-synthesized.worstCaseStandardizedSeparation)<1.0e-12);
    }

    {
        // Witness-space numerical uncertainty must be evaluated after the same
        // annihilation. Here refinement adds only degree<3 numerical error, so
        // an order-3 stencil cancels it while preserving a cubic mechanism split.
        std::vector<InterventionPoint> points;
        for (double x : {-1.0, 0.0, 1.0})
            for (double y : {-1.0, 0.0, 1.0})
                points.push_back({{x,y}});

        std::vector<std::vector<Response>> nominal(2), refined(2);
        for (std::size_t m=0;m<2;++m) {
            const double cubic = m==0 ? 1.0 : -1.0;
            for (const auto& p:points) {
                const double x=p.coordinates[0], y=p.coordinates[1];
                const double base =
                    4.0 + 2.0*x - 3.0*y + 0.4*x*x + 0.2*x*y + 0.3*y*y;
                const double mechanism = cubic*x*x*y;
                const double lowOrderNumerical =
                    0.7 + 0.1*x - 0.08*y + 0.05*x*x + 0.03*x*y;
                nominal[m].push_back({base+mechanism});
                refined[m].push_back({base+mechanism+lowOrderNumerical});
            }
        }
        UncertaintyBudget shared;
        shared.measurementVariance=1.0e-4;
        shared.repeatVariance=1.0e-4;
        shared.numericalVariance=0.0;

        const auto stencil=synthesizeWitnessSpaceMaximinStencil(
            points,3,nominal,refined,shared);
        assert(stencil.momentValidation.valid);
        assert(stencil.worstCaseStandardizedSeparation>0.0);

        std::vector<Response> nw,rw;
        for(std::size_t m=0;m<2;++m){
            nw.push_back(applyAnnihilatingStencil(nominal[m],stencil.stencil));
            rw.push_back(applyAnnihilatingStencil(refined[m],stencil.stencil));
            assert(responseDistance(nw.back(),rw.back())<1.0e-10);
        }
        const double sep=worstCaseWitnessSpaceStandardizedSeparation(
            nw,rw,shared,stencil.stencil);
        assert(sep>0.0);
    }

    {
        UncertaintyBudget u0; u0.measurementVariance=0.01;
        UncertaintyBudget u1; u1.measurementVariance=0.04;
        const auto spatial=localizeSpatialWitnessResidual(
            {0.0,0.0,0.0,0.0},
            {0.2,0.0,0.0,0.4},
            {0,0,1,1},
            {u0,u1},
            1.0);
        assert(spatial.regions.size()==2);
        assert(spatial.resolvedRegionCount==1);
        assert(spatial.regions[0].resolved);
        assert(!spatial.regions[1].resolved);
        assert(spatial.maximumStandardizedError>1.0);
    }

    return 0;
}
