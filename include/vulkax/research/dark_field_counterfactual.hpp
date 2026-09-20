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

struct ScaleFlowResult {
    std::vector<double> adjacentLogSlopes;
    double tailLogSlope{};
    bool finite{};
};

struct DeceptiveRepairAssessment {
    bool observationImproved{};
    bool witnessWorsened{};
    bool deceptive{};
    double observationLossChange{};
    double witnessErrorChange{};
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

} // namespace vulkax::research::dcs
