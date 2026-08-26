#pragma once

#include "vulkax/world/transaction.hpp"

#include <functional>
#include <iosfwd>
#include <string>

namespace vulkax::world {

enum class RewriteVerificationStatus {
    Verified,
    Rejected,
};

struct RewriteVerificationPolicy {
    double maxUnaffectedPositionDrift{1.0e-12};
};

struct PhysicalRewriteEvidence {
    bool rerunCompleted{};
    bool rerunPassed{};
    bool independentOracleCompleted{};
    bool independentOraclePassed{};
    double observableError{};
    double observableTolerance{};
    std::string artifact;
    std::string summary;
};

struct RewriteEvidence {
    bool preconditionsSatisfied{};
    bool transactionApplied{};
    bool worldCommitted{};
    bool appearancePropagationRequired{};
    bool appearancePropagationChecked{};
    double unaffectedPositionDrift{};
    double maximumAllowedUnaffectedPositionDrift{};
    bool physicalRerunRequired{};
    bool physicalRerunCompleted{};
    bool physicalRerunPassed{};
    bool independentOracleRequired{};
    bool independentOracleCompleted{};
    bool independentOraclePassed{};
    double physicalObservableError{};
    double physicalObservableTolerance{};
    std::string physicalArtifact;
    std::string verifierSummary;
    std::string rejectionReason;
};

using PhysicalRewriteVerifier = std::function<PhysicalRewriteEvidence(
    const WorldIR& committedWorld,
    const WorldCorrespondenceGraph& graph,
    const WorldTransaction& transaction,
    const TransactionReceipt& receipt)>;

struct VerifiedRewriteResult {
    RewriteVerificationStatus status{RewriteVerificationStatus::Rejected};
    TransactionValidation validation;
    TransactionReceipt receipt;
    RewriteEvidence evidence;
    bool rollbackPerformed{};
};

[[nodiscard]] VerifiedRewriteResult executeVerifiedRewrite(
    WorldIR& world,
    const WorldCorrespondenceGraph& graph,
    const WorldTransaction& transaction,
    const RewriteVerificationPolicy& policy = {},
    const PhysicalRewriteVerifier& verifier = {});

[[nodiscard]] const char* toString(RewriteVerificationStatus status) noexcept;
void writeVerifiedRewriteEvidenceCsv(std::ostream& output, const VerifiedRewriteResult& result);

} // namespace vulkax::world
