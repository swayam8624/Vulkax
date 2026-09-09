#pragma once

#include "vulkax/world/world_ir.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace vulkax::world {

struct ObservationPrediction {
    std::string observationId;
    std::vector<double> values;
    ObservationSpace space{ObservationSpace::Dimensionless};
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
    std::vector<ObservationRole> fittingRoles{ObservationRole::Fit};
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
    double validationObjective{};
    ResidualSummary validationResidual;
    std::vector<RealityLoopIteration> trace;
};

struct ParameterSensitivity {
    ParameterAddress parameter;
    double objectiveDerivative{};
    double weightedPredictionL2Derivative{};
};

struct IdentifiabilitySettings {
    double relativeFiniteDifferenceStep{1.0e-4};
    double absoluteFiniteDifferenceStep{1.0e-8};
    double relativeRankTolerance{1.0e-8};
    double absoluteRankTolerance{1.0e-12};
    std::vector<ObservationRole> roles{ObservationRole::Fit};
};

// Coefficients live in normalized parameter coordinates. If a parameter has two
// finite belief bounds, one normalized unit is the bound span; otherwise Vulkax
// uses max(abs(current value), 1). This prevents metres, pascals and focal pixels
// from making the rank test meaningless purely because of unit scale.
struct WeakParameterCombination {
    double singularValue{};
    std::vector<double> normalizedCoefficients;
};

struct LocalIdentifiabilityReport {
    std::vector<ParameterAddress> parameterOrder;
    std::vector<double> parameterScales;
    std::size_t scalarObservationCount{};
    std::size_t numericalRank{};
    bool locallyIdentifiable{};
    double largestSingularValue{};
    double smallestResolvedSingularValue{};
    double conditionNumber{};
    std::vector<double> singularValuesDescending;
    std::vector<WeakParameterCombination> weakCombinations;
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

// Diagnoses local parameter identifiability from the uncertainty-weighted residual
// Jacobian. This is deliberately separate from optimizer convergence: a low loss can
// coexist with infinitely many parameter explanations. The singular spectrum and
// weak combinations expose local gauge freedoms such as monocular image scale versus
// metric acceleration. This is a local differential diagnostic, not a proof of
// global uniqueness.
[[nodiscard]] LocalIdentifiabilityReport analyzeLocalIdentifiability(
    const WorldIR& world,
    const std::vector<ParameterAddress>& parameters,
    const ForwardModel& forwardModel,
    const IdentifiabilitySettings& settings = {});

} // namespace vulkax::world
