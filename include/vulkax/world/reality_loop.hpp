#pragma once

#include "vulkax/world/world_ir.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace vulkax::world {

struct ObservationPrediction {
    std::string observationId;
    std::vector<double> valuesSI;
};

using ForwardModel = std::function<std::vector<ObservationPrediction>(const WorldIR&)>;

struct RealityLoopSettings {
    std::size_t maxIterations{16};
    std::size_t maxBacktrackingSteps{8};
    double relativeFiniteDifferenceStep{1.0e-4};
    double absoluteFiniteDifferenceStep{1.0e-8};
    double initialDamping{1.0e-3};
    double dampingGrowth{10.0};
    double dampingShrink{0.3};
    double objectiveTolerance{1.0e-12};
    double relativeImprovementTolerance{1.0e-8};
    double parameterStepTolerance{1.0e-10};
};

struct ResidualSummary {
    std::size_t scalarCount{};
    double weightedRms{};
    double weightedMaximum{};
};

struct RealityLoopIteration {
    std::size_t iteration{};
    double objective{};
    double damping{};
    bool accepted{};
    std::vector<double> parameterValues;
};

struct RealityLoopResult {
    WorldIR world;
    double initialObjective{};
    double finalObjective{};
    bool converged{};
    ResidualSummary residual;
    std::vector<RealityLoopIteration> trace;
};

struct ParameterSensitivity {
    ParameterAddress parameter;
    double objectiveDerivative{};
    double weightedPredictionL2Derivative{};
};

// Fits an executable WorldIR against its observation records. The supplied forward
// model may dispatch to MPM/FEM/rendering/reconstruction code, but the inverse loop
// itself stays solver-agnostic. Derivatives are finite-difference numerical oracles,
// intentionally preserving an independent reference path beside future adjoints.
[[nodiscard]] RealityLoopResult fitWorldHypothesis(
    WorldIR initialWorld,
    const std::vector<ParameterAddress>& parameters,
    const ForwardModel& forwardModel,
    const RealityLoopSettings& settings = {});

// Local numerical sensitivity, not a claim of formal causal identification. Values
// describe how the current forward model's weighted predictions and objective change
// around the supplied WorldIR state.
[[nodiscard]] std::vector<ParameterSensitivity> rankParameterSensitivity(
    const WorldIR& world,
    const std::vector<ParameterAddress>& parameters,
    const ForwardModel& forwardModel,
    double relativeStep = 1.0e-4,
    double absoluteStep = 1.0e-8);

} // namespace vulkax::world
