#include "vulkax/operators/operator_graph.hpp"
#include "vulkax/world/reality_loop.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(double a, double b, double tolerance = 1.0e-6) {
    return std::abs(a - b) <= tolerance;
}

void testExecutableHypothesisValidation() {
    using namespace vulkax::world;
    WorldIR world;
    world.id = "hypothesis";
    world.entities.push_back({1, "body", std::nullopt, {{"stiffness", 2.0}}, {{"bias", 0.0}}});
    world.globalParameters["gravity_scale"] = 1.0;
    const ParameterAddress stiffness{ParameterSpace::Material, 1, "stiffness"};
    world.parameterBeliefs.push_back({stiffness, 0.1, 10.0, 0.25, EvidenceClass::ModelProxy, "test"});
    world.observations.push_back({"obs", "tip", 1, 0.1, {3.0}, {0.2}, EvidenceClass::Measured, "test"});

    const auto validation = world.validateHypothesis();
    check(validation.valid, "well-formed executable WorldIR must validate");
    check(world.parameterValue(stiffness) == std::optional<double>{2.0}, "parameter address must resolve value");
    check(world.setParameterValue(stiffness, 3.0), "existing addressed parameter must be mutable");
    check(near(world.clampToBelief(stiffness, 100.0), 10.0), "parameter belief must clamp trials to upper bound");

    world.observations[0].standardDeviationSI = {0.0};
    check(!world.validateHypothesis().valid, "non-positive measurement uncertainty must be rejected");
}

void testClosedLoopRecoversParameters() {
    using namespace vulkax::world;
    WorldIR world;
    world.id = "linear-inverse";
    world.entities.push_back({1, "body", std::nullopt, {{"stiffness", 1.0}}, {{"bias", 0.0}}});
    const ParameterAddress stiffness{ParameterSpace::Material, 1, "stiffness"};
    const ParameterAddress bias{ParameterSpace::Constraint, 1, "bias"};
    world.parameterBeliefs.push_back({stiffness, 0.0, 10.0, std::nullopt, EvidenceClass::ModelProxy, "synthetic"});
    world.parameterBeliefs.push_back({bias, -5.0, 5.0, std::nullopt, EvidenceClass::ModelProxy, "synthetic"});
    world.observations.push_back({"tip", "tip_position", 1, 0.0, {2.5}, {0.1}, EvidenceClass::Synthetic, "analytic"});
    world.observations.push_back({"recoil", "recoil", 1, 0.0, {5.0}, {0.2}, EvidenceClass::Synthetic, "analytic"});

    const ForwardModel forward = [=](const WorldIR& candidate) {
        const double k = *candidate.parameterValue(stiffness);
        const double b = *candidate.parameterValue(bias);
        return std::vector<ObservationPrediction>{{"tip", {0.5 * k + b}},
                                                  {"recoil", {1.5 * k - 2.0 * b}}};
    };

    RealityLoopSettings settings;
    settings.maxIterations = 12;
    settings.relativeImprovementTolerance = 1.0e-12;
    const auto result = fitWorldHypothesis(world, {stiffness, bias}, forward, settings);
    check(result.finalObjective < result.initialObjective * 1.0e-8,
          "reality loop must drastically reduce the weighted observation mismatch");
    check(near(*result.world.parameterValue(stiffness), 4.0, 1.0e-4),
          "reality loop must recover stiffness from multiple observations");
    check(near(*result.world.parameterValue(bias), 0.5, 1.0e-4),
          "reality loop must recover constraint bias from multiple observations");
    check(result.residual.weightedRms < 1.0e-4, "final weighted residual must be small");
}

void testBoundsRemainHardConstraints() {
    using namespace vulkax::world;
    WorldIR world;
    world.entities.push_back({1, "bounded", std::nullopt, {{"gain", 1.0}}, {}});
    const ParameterAddress gain{ParameterSpace::Material, 1, "gain"};
    world.parameterBeliefs.push_back({gain, 0.0, 5.0, std::nullopt, EvidenceClass::ModelProxy, "test"});
    world.observations.push_back({"target", "output", 1, 0.0, {100.0}, {}, EvidenceClass::Synthetic, "test"});
    const ForwardModel forward = [=](const WorldIR& candidate) {
        return std::vector<ObservationPrediction>{{"target", {*candidate.parameterValue(gain)}}};
    };
    const auto result = fitWorldHypothesis(world, {gain}, forward);
    check(*result.world.parameterValue(gain) <= 5.0 + 1.0e-12,
          "inverse fitting must never leave the declared parameter belief bounds");
    check(near(*result.world.parameterValue(gain), 5.0, 1.0e-6),
          "unreachable target must converge to the active upper bound");
}

void testSensitivityRanking() {
    using namespace vulkax::world;
    WorldIR world;
    world.entities.push_back({1, "body", std::nullopt, {{"active", 2.0}, {"inactive", 7.0}}, {}});
    const ParameterAddress active{ParameterSpace::Material, 1, "active"};
    const ParameterAddress inactive{ParameterSpace::Material, 1, "inactive"};
    world.observations.push_back({"obs", "signal", 1, 0.0, {10.0}, {}, EvidenceClass::Synthetic, "test"});
    const ForwardModel forward = [=](const WorldIR& candidate) {
        return std::vector<ObservationPrediction>{{"obs", {3.0 * *candidate.parameterValue(active)}}};
    };
    const auto ranked = rankParameterSensitivity(world, {inactive, active}, forward);
    check(ranked.size() == 2 && ranked[0].parameter == active,
          "parameter sensitivity must rank the locally influential parameter first");
    check(ranked[0].weightedPredictionL2Derivative > 2.9,
          "active parameter should expose the analytic local derivative");
    check(ranked[1].weightedPredictionL2Derivative < 1.0e-9,
          "unused parameter should have zero local prediction sensitivity");
}

void testStructuralOperatorBacktraceTerminatesCycles() {
    using namespace vulkax;
    problem::ProblemIR problem;
    problem.operators = {
        {"integrate", "integrate", "position", {"velocity"}, "", "kinematics"},
        {"accelerate", "accelerate", "velocity", {"force"}, "", "dynamics"},
        {"contact", "contact", "force", {"position"}, "", "contact"},
        {"thermal", "thermal", "temperature", {"heat"}, "", "thermal"},
    };
    operators::OperatorGraph graph(problem);
    const auto trace = graph.traceUpstream("position", 16);
    check(trace.operators.size() == 3, "cyclic upstream trace must visit each reachable operator once");
    check(std::none_of(trace.operators.begin(), trace.operators.end(), [](const auto& influence) {
              return influence.operatorId == "thermal";
          }),
          "structural backtrace must omit disconnected operators");
    check(trace.operators[0].operatorId == "integrate" && trace.operators[0].depth == 0,
          "direct writer of observable must be the depth-zero structural influence");
}

} // namespace

int main() {
    testExecutableHypothesisValidation();
    testClosedLoopRecoversParameters();
    testBoundsRemainHardConstraints();
    testSensitivityRanking();
    testStructuralOperatorBacktraceTerminatesCycles();
    if (failures != 0) {
        std::cerr << failures << " reality-loop test(s) failed\n";
        return 1;
    }
    std::cout << "All executable WorldIR reality-loop tests passed\n";
    return 0;
}
