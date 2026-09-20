#include "vulkax/research/dark_field_counterfactual.hpp"

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

    return 0;
}
