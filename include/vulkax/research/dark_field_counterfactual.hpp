#pragma once

#include <cstddef>
#include <vector>

namespace vulkax::research::dcs {

using Response = std::vector<double>;

struct InterventionPoint {
    std::vector<double> coordinates;
};

struct AnnihilatingStencil {
    // A stencil of order k must annihilate every monomial with total degree < k.
    std::size_t order{2};
    std::vector<InterventionPoint> points;
    std::vector<double> weights;
};

struct MomentValidation {
    std::vector<double> maximumResidualByDegree;
    double maximumAbsoluteResidual{};
    bool valid{};
};

struct MechanismContactResult {
    // If separated is true, this is the first 1-indexed response order whose
    // normalized discrepancy exceeds the frozen tolerance.
    std::size_t separatingOrder{};
    bool separated{};
    std::vector<double> normalizedDistanceByOrder;
};

struct JetContactEstimate {
    std::size_t separatingOrder{};
    bool separated{};
    std::vector<double> maximumStandardizedDiscrepancyByOrder;
    std::vector<std::size_t> witnessCountByOrder;
};

struct ScaleFlowResult {
    std::vector<double> adjacentLogSlopes;
    double tailLogSlope{};
    bool finite{};
};

struct UncertaintyBudget {
    double measurementVariance{};
    double numericalVariance{};
    double repeatVariance{};

    [[nodiscard]] double totalVariance() const noexcept {
        return measurementVariance + numericalVariance + repeatVariance;
    }
};

struct MechanismResolutionResult {
    std::vector<double> standardizedSignalByOrder;
    std::size_t maximumObservableOrder{};
    bool anyObservableOrder{};
};

struct DeceptiveRepairAssessment {
    bool observationImproved{};
    bool witnessWorsened{};
    bool deceptive{};
    double observationLossChange{};
    double witnessErrorChange{};
};

struct SynthesizedStencil {
    AnnihilatingStencil stencil;
    MomentValidation momentValidation;
    double modelDisagreementEnergy{};
    double worstCaseStandardizedSeparation{};
    double independentNoiseGain{};
    std::size_t powerIterations{};
    bool converged{};
};

[[nodiscard]] double responseNorm(const Response& response);
[[nodiscard]] double responseDistance(const Response& lhs, const Response& rhs);

// Full-set Möbius inversion over a Boolean intervention lattice. Responses are
// indexed by subset bitmask [0, 2^interventionCount). The returned response is
// the irreducible interaction that cannot be explained by any proper subset.
[[nodiscard]] Response counterfactualCumulant(
    const std::vector<Response>& subsetResponses,
    std::size_t interventionCount);

// Verify that a signed physical intervention ensemble annihilates every
// polynomial response term below stencil.order.
[[nodiscard]] MomentValidation validateAnnihilatingStencil(
    const AnnihilatingStencil& stencil,
    double tolerance = 1.0e-10);

// Apply a validated dark-field stencil to vector-valued physical observations.
[[nodiscard]] Response applyAnnihilatingStencil(
    const std::vector<Response>& responses,
    const AnnihilatingStencil& stencil,
    double tolerance = 1.0e-10);

// First derivative/interaction order at which two worlds become distinguishable.
// Entries are ordered [first-order response, second-order witness, ...].
[[nodiscard]] MechanismContactResult mechanismOrderOfContact(
    const std::vector<Response>& referenceByOrder,
    const std::vector<Response>& candidateByOrder,
    double relativeTolerance = 0.05,
    double absoluteFloor = 1.0e-12);

// Basis-aware operational estimate of response-jet contact. Each order may
// contain several independent order-selective witnesses. Worlds remain in
// contact through an order only if every tested witness stays within the frozen
// standardized-discrepancy threshold.
[[nodiscard]] JetContactEstimate estimateJetOrderOfContact(
    const std::vector<std::vector<Response>>& referenceWitnessesByOrder,
    const std::vector<std::vector<Response>>& candidateWitnessesByOrder,
    const std::vector<UncertaintyBudget>& uncertaintyByOrder,
    double separatingStandardizedDiscrepancy);

// Log-log flow of a witness norm over numerical resolution h. This is a
// diagnostic guard, not a claim that every witness follows an RG power law.
[[nodiscard]] ScaleFlowResult analyzeWitnessScaleFlow(
    const std::vector<double>& scales,
    const std::vector<double>& witnessNorms);

// Classify the measured pattern that motivated DCS: a candidate repair improves
// ordinary held-out loss while moving farther from an independent dark-field
// mechanism witness.
[[nodiscard]] DeceptiveRepairAssessment classifyDeceptiveRepair(
    double observationLossBefore,
    double observationLossAfter,
    double witnessErrorBefore,
    double witnessErrorAfter,
    double minimumMeaningfulChange = 0.0);

// Candidate experiment score: between-model dark-field response dispersion
// divided by measurement + numerical uncertainty + acquisition penalty.
// Generic Fisher information remains a required baseline; this is deliberately
// target/mechanism-disagreement based.
[[nodiscard]] double darkFieldDiscriminationScore(
    const std::vector<Response>& modelWitnesses,
    double measurementVariance,
    double numericalVariance,
    double acquisitionCost,
    double costWeight = 1.0);

// Standardized mismatch in units of total witness uncertainty.
[[nodiscard]] double standardizedDarkFieldDiscrepancy(
    const Response& measured,
    const Response& predicted,
    const UncertaintyBudget& uncertainty);

// Propagate per-intervention uncertainty through a signed stencil.
[[nodiscard]] UncertaintyBudget propagateStencilUncertainty(
    const AnnihilatingStencil& stencil,
    const UncertaintyBudget& perInterventionUncertainty);

// Smallest pairwise standardized separation among surviving candidate worlds.
[[nodiscard]] double worstCaseStandardizedSeparation(
    const std::vector<Response>& modelWitnesses,
    const UncertaintyBudget& uncertainty);

// Pair-aware standardized separation. sharedObservationUncertainty contains
// measurement/repeat uncertainty common to the physical acquisition. Each entry
// in modelNumericalUncertainty supplies that candidate world's numerical
// uncertainty before stencil propagation. The pair denominator contains the
// shared observation variance plus both candidate numerical variances.
[[nodiscard]] double worstCasePairAwareStandardizedSeparation(
    const std::vector<Response>& modelWitnesses,
    const UncertaintyBudget& sharedObservationUncertainty,
    const std::vector<UncertaintyBudget>& modelNumericalUncertainty,
    const AnnihilatingStencil& stencil);

// Highest response order whose measured dark-field signal remains observable
// above a frozen standardized-signal threshold.
[[nodiscard]] MechanismResolutionResult mechanismResolution(
    const std::vector<Response>& witnessByOrder,
    const std::vector<UncertaintyBudget>& uncertaintyByOrder,
    double minimumStandardizedSignal);

// Maximin DCS experiment synthesis. This searches projected pairwise
// disagreement directions and keeps the annihilating stencil maximizing the
// worst-case pairwise standardized separation. It is a deterministic heuristic,
// not a claim of global non-convex optimality.
[[nodiscard]] SynthesizedStencil synthesizeMaximinAnnihilatingStencil(
    const std::vector<InterventionPoint>& points,
    std::size_t order,
    const std::vector<std::vector<Response>>& modelResponses,
    const UncertaintyBudget& uncertainty,
    double momentTolerance = 1.0e-9,
    std::size_t maximumIterations = 256);

// Pair-aware variant used when candidate numerical uncertainty differs
// materially across model families. The synthesis objective is the minimum
// pairwise standardized separation after exact lower-order annihilation.
[[nodiscard]] SynthesizedStencil synthesizePairAwareMaximinStencil(
    const std::vector<InterventionPoint>& points,
    std::size_t order,
    const std::vector<std::vector<Response>>& modelResponses,
    const UncertaintyBudget& sharedObservationUncertainty,
    const std::vector<UncertaintyBudget>& modelNumericalUncertainty,
    double momentTolerance = 1.0e-9,
    std::size_t maximumIterations = 256);

// Automatically synthesize a signed intervention ensemble. The moment
// constraints define the lower-order response subspace to suppress. Competing
// model responses define a disagreement operator; the returned weights maximize
// its projected energy under unit L2 norm before a final L1 normalization.
// modelResponses[model][point][observable].
[[nodiscard]] SynthesizedStencil synthesizeAnnihilatingStencil(
    const std::vector<InterventionPoint>& points,
    std::size_t order,
    const std::vector<std::vector<Response>>& modelResponses,
    double momentTolerance = 1.0e-9,
    std::size_t maximumIterations = 256);

} // namespace vulkax::research::dcs
