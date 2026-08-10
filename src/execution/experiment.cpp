#include "vulkax/execution/experiment.hpp"

#include "vulkax/problem/validation.hpp"
#include "vulkax/solvers/dem_broadphase.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace vulkax::execution {

namespace {

void hashByte(std::uint64_t& hash, std::uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ull;
}

void hashU64(std::uint64_t& hash, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) hashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

void hashString(std::uint64_t& hash, std::string_view value) {
    for (unsigned char ch : value) hashByte(hash, ch);
    hashByte(hash, 0xffu);
}

double requestedTolerance(const problem::ProblemIR& problem, double fallback) {
    double tolerance = fallback;
    for (const auto& target : problem.accuracyTargets) {
        if (target.relativeTolerance > 0.0) tolerance = std::min(tolerance, target.relativeTolerance);
    }
    return tolerance;
}

backend::BackendSelection chooseBackend(const ExperimentRequest& request) {
    backend::WorkloadRequirements requirements;
    requirements.requiredFeatures = {backend::Feature::StorageBuffers};
    return backend::selectBackend(request.backendCandidates, requirements, backend::currentPlatform());
}

const backend::BackendCapabilities& selectedCapability(
    const std::vector<backend::BackendCapabilities>& candidates, backend::BackendKind kind) {
    const auto iterator = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return candidate.available && candidate.kind == kind;
    });
    if (iterator == candidates.end()) throw std::runtime_error("selected backend capability disappeared");
    return *iterator;
}

} // namespace

std::uint64_t stableSolverPlanHash(const planning::SolverPlan& plan) {
    std::uint64_t hash = 1469598103934665603ull;
    hashU64(hash, static_cast<std::uint64_t>(plan.fidelity));
    hashU64(hash, static_cast<std::uint64_t>(plan.discretization));
    hashU64(hash, static_cast<std::uint64_t>(plan.solver));
    hashU64(hash, static_cast<std::uint64_t>(plan.characteristicResolution));
    hashU64(hash, std::bit_cast<std::uint64_t>(plan.relativeTolerance));
    for (const auto& reason : plan.reasons) hashString(hash, reason);
    return hash;
}

ExperimentResult executeExperiment(ExperimentRequest request) {
    const auto validation = problem::validateProblem(request.problem);
    if (!validation.ok()) throw std::invalid_argument("cannot execute an invalid ProblemIR");
    const auto backendSelection = chooseBackend(request);
    if (!backendSelection.kind) throw std::runtime_error("no backend satisfies experiment requirements");
    const auto& capability = selectedCapability(request.backendCandidates, *backendSelection.kind);
    const auto ladder = planning::makeFidelityLadder(request.problem, capability);
    const auto plan = planning::selectBestPlan(ladder, request.problem.computeBudget);

    verify::ResultCertificate certificate;
    certificate.problemHash = problem::stableProblemHash(request.problem);
    certificate.solverHash = stableSolverPlanHash(plan);
    certificate.backend = std::string(backend::toString(*backendSelection.kind));
    certificate.device = capability.deviceName;
    certificate.notes.insert(certificate.notes.end(), plan.reasons.begin(), plan.reasons.end());
    certificate.notes.insert(certificate.notes.end(), backendSelection.reasons.begin(), backendSelection.reasons.end());

    std::vector<Metric> metrics;
    const auto started = std::chrono::steady_clock::now();

    if (auto* diffusion = std::get_if<DiffusionExperiment>(&request.payload)) {
        if (plan.solver != planning::SolverKind::ExplicitField) throw std::invalid_argument("payload/solver mismatch: diffusion");
        const double before = numerics::integral(diffusion->state);
        solvers::advanceDiffusion(diffusion->state, diffusion->config);
        const double after = numerics::integral(diffusion->state);
        const double conservation = std::abs(after - before) / std::max(1.0e-30, std::abs(before));
        metrics.push_back({"integral_before", before});
        metrics.push_back({"integral_after", after});
        metrics.push_back({"relative_integral_change", conservation});
        if (diffusion->config.boundary == numerics::BoundaryMode::Periodic) {
            certificate.criteria.push_back({"periodic integral conservation", conservation,
                                            requestedTolerance(request.problem, 1.0e-8),
                                            verify::CriterionRelation::LessEqual, true});
        }
    } else if (auto* dem = std::get_if<DemExperiment>(&request.payload)) {
        if (plan.solver != planning::SolverKind::DEM) throw std::invalid_argument("payload/solver mismatch: DEM");
        const auto stats = solvers::advanceDemSpatialHash(dem->particles, dem->box, dem->config, dem->steps);
        const auto diagnostics = solvers::measureDem(dem->particles, dem->box, dem->config);
        double minimumRadius = std::numeric_limits<double>::infinity();
        for (const auto& particle : dem->particles) minimumRadius = std::min(minimumRadius, particle.radius);
        const double overlapRatio = minimumRadius > 0.0 ? diagnostics.maximumOverlap / minimumRadius : 0.0;
        metrics.push_back({"candidate_pairs", static_cast<double>(stats.candidatePairs)});
        metrics.push_back({"contacts", static_cast<double>(stats.contacts)});
        metrics.push_back({"kinetic_energy", diagnostics.kineticEnergy});
        metrics.push_back({"maximum_overlap_ratio", overlapRatio});
        certificate.criteria.push_back({"maximum particle overlap / minimum radius", overlapRatio, 0.5,
                                        verify::CriterionRelation::LessEqual, true});
    } else if (auto* fluid = std::get_if<FluidExperiment>(&request.payload)) {
        if (plan.solver != planning::SolverKind::ProjectionFluid) throw std::invalid_argument("payload/solver mismatch: fluid");
        const auto diagnostics = solvers::projectIncompressible(fluid->state, fluid->config);
        const double ratio = diagnostics.divergenceL2Before > 0.0
                                 ? diagnostics.divergenceL2After / diagnostics.divergenceL2Before
                                 : 0.0;
        metrics.push_back({"divergence_l2_before", diagnostics.divergenceL2Before});
        metrics.push_back({"divergence_l2_after", diagnostics.divergenceL2After});
        metrics.push_back({"divergence_reduction_ratio", ratio});
        metrics.push_back({"max_speed", diagnostics.maxSpeed});
        certificate.criteria.push_back({"projection divergence reduction ratio", ratio, 0.75,
                                        verify::CriterionRelation::LessEqual, true});
    } else if (auto* fem = std::get_if<FemExperiment>(&request.payload)) {
        if (plan.solver != planning::SolverKind::LinearFEM) throw std::invalid_argument("payload/solver mismatch: FEM");
        const auto result = solvers::solveLinearTetrahedralElasticity(fem->nodes, fem->elements, fem->material);
        double maxDisplacement = 0.0;
        double maxStress = 0.0;
        for (const auto& displacement : result.displacement) maxDisplacement = std::max(maxDisplacement, math::length(displacement));
        for (double stress : result.vonMisesStress) maxStress = std::max(maxStress, stress);
        metrics.push_back({"maximum_displacement", maxDisplacement});
        metrics.push_back({"maximum_von_mises", maxStress});
        metrics.push_back({"strain_energy", result.strainEnergy});
        certificate.criteria.push_back({"nonnegative strain energy", result.strainEnergy, 0.0,
                                        verify::CriterionRelation::GreaterEqual, true});
    }

    const auto stopped = std::chrono::steady_clock::now();
    certificate.wallSeconds = std::chrono::duration<double>(stopped - started).count();
    certificate.updateTrustState(request.convergenceStudyComplete);
    return {plan, certificate, std::move(metrics), std::move(request.payload)};
}

} // namespace vulkax::execution
