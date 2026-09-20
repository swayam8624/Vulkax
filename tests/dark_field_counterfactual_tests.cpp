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

    return 0;
}
