#include "vulkax/compiler/expression.hpp"
#include "vulkax/core/units.hpp"
#include "vulkax/experiment/design.hpp"
#include "vulkax/inverse/hyperelastic.hpp"
#include "vulkax/optimize/optimizer.hpp"

#include <cassert>
#include <cmath>
#include <unordered_map>
#include <vector>

int main() {
    using namespace vulkax;

    const auto expression = compiler::compileExpression("0.5*m*v^2");
    const double energy = compiler::evaluate(expression, {{"m", 2.0}, {"v", 3.0}});
    assert(std::abs(energy - 9.0) < 1.0e-12);
    const auto dimension = compiler::inferDimension(
        expression, {{"m", units::mass}, {"v", units::velocity}});
    assert(dimension == units::makeDimension(2, 1, -2));
    assert(!compiler::canonicalExpression(expression).empty());

    bool caughtDimensionError = false;
    try {
        const auto bad = compiler::compileExpression("m + v");
        (void)compiler::inferDimension(bad, {{"m", units::mass}, {"v", units::velocity}});
    } catch (const std::invalid_argument&) {
        caughtDimensionError = true;
    }
    assert(caughtDimensionError);

    std::vector<inverse::UniaxialSample> samples;
    const std::vector<double> trueParameters = {0.65, 0.12};
    for (double stretch : {1.05, 1.15, 1.3, 1.5, 1.8, 2.1}) {
        samples.push_back({stretch,
                           inverse::predictUniaxialNominal(
                               inverse::HyperelasticModel::MooneyRivlin, trueParameters, stretch),
                           1.0});
    }
    const auto ranked = inverse::rankHyperelasticModels(samples);
    assert(!ranked.empty());
    assert(ranked.front().model == inverse::HyperelasticModel::MooneyRivlin);
    assert(ranked.front().weightedRmse < 1.0e-7);
    assert(std::abs(ranked.front().parameters[0] - trueParameters[0]) < 1.0e-6);
    assert(std::abs(ranked.front().parameters[1] - trueParameters[1]) < 1.0e-6);

    const auto choice = experiment::selectNextUniaxialExperiment(
        std::vector<inverse::UniaxialSample>(samples.begin(), samples.begin() + 4),
        {1.1, 1.4, 1.7, 2.0, 2.3}, 0.01);
    assert(choice.stretch >= 1.1 && choice.stretch <= 2.3);
    assert(choice.combinedScore > 0.0);

    const optimize::ObjectiveFunction objective = [](const std::vector<double>& x) {
        const double a = x[0] - 1.25;
        const double b = x[1] + 0.75;
        return a * a + 2.0 * b * b;
    };
    const auto optimum = optimize::boundedCoordinateSearch(
        objective, {{"a", -3.0, 3.0}, {"b", -3.0, 3.0}}, {0.0, 0.0});
    assert(optimum.converged);
    assert(std::abs(optimum.parameters[0] - 1.25) < 1.0e-5);
    assert(std::abs(optimum.parameters[1] + 0.75) < 1.0e-5);
    return 0;
}
