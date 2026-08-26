#include "vulkax/world/verified_rewrite.hpp"

#include <cmath>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace vulkax::world {
namespace {

[[nodiscard]] std::string joinErrors(const std::vector<std::string>& errors) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < errors.size(); ++i) {
        if (i != 0U) stream << "; ";
        stream << errors[i];
    }
    return stream.str();
}

[[nodiscard]] std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) return value;
    std::string escaped;
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (const char character : value) {
        if (character == '"') escaped.push_back('"');
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

} // namespace

const char* toString(RewriteVerificationStatus status) noexcept {
    switch (status) {
        case RewriteVerificationStatus::Verified: return "verified";
        case RewriteVerificationStatus::Rejected: return "rejected";
    }
    return "rejected";
}

VerifiedRewriteResult executeVerifiedRewrite(WorldIR& world,
                                            const WorldCorrespondenceGraph& graph,
                                            const WorldTransaction& transaction,
                                            const RewriteVerificationPolicy& policy,
                                            const PhysicalRewriteVerifier& verifier) {
    if (!std::isfinite(policy.maxUnaffectedPositionDrift) || policy.maxUnaffectedPositionDrift < 0.0)
        throw std::invalid_argument("verified rewrite requires a finite non-negative locality tolerance");

    VerifiedRewriteResult result;
    result.validation = validateTransaction(world, graph, transaction);
    result.evidence.maximumAllowedUnaffectedPositionDrift = policy.maxUnaffectedPositionDrift;
    result.evidence.appearancePropagationRequired = result.validation.requiresAppearancePropagation;
    result.evidence.physicalRerunRequired = result.validation.requiresPhysicalRerun;
    result.evidence.independentOracleRequired = result.validation.requiresIndependentOracle;

    if (!result.validation.valid) {
        result.evidence.rejectionReason = joinErrors(result.validation.errors);
        return result;
    }
    result.evidence.preconditionsSatisfied = true;

    // A physical rewrite is never allowed to become verified merely from metadata
    // mutation. The caller must provide the rerun/oracle adapter for the active
    // physical representation before any mutation is attempted.
    if (result.validation.requiresPhysicalRerun && !verifier) {
        result.evidence.rejectionReason = "physical rewrite requires a physical rerun verifier";
        return result;
    }

    const gaussian::GaussianCloud appearanceBefore = world.appearance;
    result.receipt = applyTransaction(world, graph, transaction);
    result.evidence.transactionApplied = true;
    result.evidence.appearancePropagationChecked =
        !result.validation.requiresAppearancePropagation || !result.receipt.touchedGaussians.empty();
    result.evidence.unaffectedPositionDrift =
        unaffectedPositionDrift(appearanceBefore, world.appearance, result.receipt.touchedGaussians);

    if (result.validation.requiresPhysicalRerun) {
        try {
            const auto physical = verifier(world, graph, transaction, result.receipt);
            result.evidence.physicalRerunCompleted = physical.rerunCompleted;
            result.evidence.physicalRerunPassed = physical.rerunPassed;
            result.evidence.independentOracleCompleted = physical.independentOracleCompleted;
            result.evidence.independentOraclePassed = physical.independentOraclePassed;
            result.evidence.physicalObservableError = physical.observableError;
            result.evidence.physicalObservableTolerance = physical.observableTolerance;
            result.evidence.physicalArtifact = physical.artifact;
            result.evidence.verifierSummary = physical.summary;
        } catch (const std::exception& error) {
            result.evidence.rejectionReason = std::string("physical verifier failed: ") + error.what();
        } catch (...) {
            result.evidence.rejectionReason = "physical verifier failed with an unknown exception";
        }
    }

    const bool localityPassed =
        std::isfinite(result.evidence.unaffectedPositionDrift) &&
        result.evidence.unaffectedPositionDrift <= policy.maxUnaffectedPositionDrift;
    const bool appearancePassed = result.evidence.appearancePropagationChecked;

    bool physicalPassed = true;
    if (result.validation.requiresPhysicalRerun) {
        const bool scalarEvidenceValid =
            std::isfinite(result.evidence.physicalObservableError) &&
            std::isfinite(result.evidence.physicalObservableTolerance) &&
            result.evidence.physicalObservableTolerance >= 0.0 &&
            result.evidence.physicalObservableError <= result.evidence.physicalObservableTolerance;
        physicalPassed = result.evidence.physicalRerunCompleted &&
                         result.evidence.physicalRerunPassed &&
                         scalarEvidenceValid &&
                         !result.evidence.physicalArtifact.empty();
    }

    const bool oraclePassed =
        !result.validation.requiresIndependentOracle ||
        (result.evidence.independentOracleCompleted && result.evidence.independentOraclePassed);

    if (result.evidence.rejectionReason.empty() && localityPassed && appearancePassed && physicalPassed && oraclePassed) {
        result.status = RewriteVerificationStatus::Verified;
        result.evidence.worldCommitted = true;
        return result;
    }

    if (result.evidence.rejectionReason.empty()) {
        if (!localityPassed)
            result.evidence.rejectionReason = "unaffected-region drift exceeds verification policy";
        else if (!appearancePassed)
            result.evidence.rejectionReason = "required appearance propagation evidence is missing";
        else if (!physicalPassed)
            result.evidence.rejectionReason = "physical rerun evidence is incomplete, untraceable, or outside tolerance";
        else if (!oraclePassed)
            result.evidence.rejectionReason = "independent rewrite oracle did not pass";
        else
            result.evidence.rejectionReason = "rewrite verification failed";
    }

    rollbackTransaction(world, result.receipt);
    result.rollbackPerformed = true;
    result.evidence.worldCommitted = false;
    return result;
}

void writeVerifiedRewriteEvidenceCsv(std::ostream& output, const VerifiedRewriteResult& result) {
    output << "status,preconditions_satisfied,transaction_applied,world_committed,rollback_performed,"
              "appearance_propagation_required,appearance_propagation_checked,unaffected_position_drift,"
              "maximum_allowed_unaffected_position_drift,physical_rerun_required,physical_rerun_completed,"
              "physical_rerun_passed,independent_oracle_required,independent_oracle_completed,"
              "independent_oracle_passed,physical_observable_error,physical_observable_tolerance,"
              "physical_artifact,verifier_summary,rejection_reason\n";
    output << toString(result.status) << ','
           << (result.evidence.preconditionsSatisfied ? 1 : 0) << ','
           << (result.evidence.transactionApplied ? 1 : 0) << ','
           << (result.evidence.worldCommitted ? 1 : 0) << ','
           << (result.rollbackPerformed ? 1 : 0) << ','
           << (result.evidence.appearancePropagationRequired ? 1 : 0) << ','
           << (result.evidence.appearancePropagationChecked ? 1 : 0) << ','
           << result.evidence.unaffectedPositionDrift << ','
           << result.evidence.maximumAllowedUnaffectedPositionDrift << ','
           << (result.evidence.physicalRerunRequired ? 1 : 0) << ','
           << (result.evidence.physicalRerunCompleted ? 1 : 0) << ','
           << (result.evidence.physicalRerunPassed ? 1 : 0) << ','
           << (result.evidence.independentOracleRequired ? 1 : 0) << ','
           << (result.evidence.independentOracleCompleted ? 1 : 0) << ','
           << (result.evidence.independentOraclePassed ? 1 : 0) << ','
           << result.evidence.physicalObservableError << ','
           << result.evidence.physicalObservableTolerance << ','
           << csvEscape(result.evidence.physicalArtifact) << ','
           << csvEscape(result.evidence.verifierSummary) << ','
           << csvEscape(result.evidence.rejectionReason) << '\n';
}

} // namespace vulkax::world
