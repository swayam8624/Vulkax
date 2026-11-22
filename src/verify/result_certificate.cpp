// TEACHING MAP: Convert numeric measurements into *configured* trust
// states. Every required criterion has an explicit direction and threshold;
// NaN/inf comparisons fail rather than silently passing. A Verified status
// requires required evidence and an indicated complete convergence study,
// not just a visually plausible rendering.
//
// Criterion passing is local to those measured quantities and tolerances.
// It cannot prove model identifiability, out-of-sample prediction, or
// numerical convergence in an untested regime. The separate world
// verified-rewrite transaction may require an independent physical oracle
// beyond this generic certificate's numeric gates.
//
// Preserve freeze-tag scientific decisions and carry validation failures
// into results rather than rebranding them as passes.
#include "vulkax/verify/result_certificate.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace vulkax::verify {

namespace {

std::string escapeJson(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += ch; break;
        }
    }
    return result;
}

} // namespace

const char* toString(TrustState state) noexcept {
    switch (state) {
    case TrustState::Preview: return "preview";
    case TrustState::Converging: return "converging";
    case TrustState::Verified: return "verified";
    case TrustState::Rejected: return "rejected";
    }
    return "unknown";
}

// Fail closed on NaN/inf; the configured threshold relation sets the comparison direction.
// Finite measurements only: threshold comparison direction must be
// declared by the criterion. A NaN never certifies a bound.
bool VerificationCriterion::passed() const noexcept {
    if (!std::isfinite(measured) || !std::isfinite(threshold)) return false;
    switch (relation) {
    case CriterionRelation::LessEqual: return measured <= threshold;
    case CriterionRelation::GreaterEqual: return measured >= threshold;
    }
    return false;
}

bool ResultCertificate::requiredEvidencePasses() const noexcept {
    return std::all_of(criteria.begin(), criteria.end(), [](const VerificationCriterion& criterion) {
        return !criterion.required || criterion.passed();
    });
}

// No required criteria -> Preview, missing/failed required evidence ->
// Rejected or Converging, all required checks plus convergence-complete ->
// Verified. A caller's 'complete' flag must itself be evidenced in tests.
void ResultCertificate::updateTrustState(bool convergenceStudyComplete) noexcept {
    const bool hasRequired = std::any_of(criteria.begin(), criteria.end(), [](const auto& criterion) {
        return criterion.required;
    });
    if (hasRequired && !requiredEvidencePasses()) {
        state = TrustState::Rejected;
        return;
    }
    if (hasRequired && convergenceStudyComplete) {
        state = TrustState::Verified;
        return;
    }
    state = hasRequired ? TrustState::Converging : TrustState::Preview;
}

std::string ResultCertificate::toJson() const {
    std::ostringstream stream;
    stream << std::setprecision(17);
    stream << "{\n";
    stream << "  \"schema\": 1,\n";
    stream << "  \"problem_hash\": \"" << std::hex << problemHash << std::dec << "\",\n";
    stream << "  \"solver_hash\": \"" << std::hex << solverHash << std::dec << "\",\n";
    stream << "  \"trust_state\": \"" << toString(state) << "\",\n";
    stream << "  \"backend\": \"" << escapeJson(backend) << "\",\n";
    stream << "  \"device\": \"" << escapeJson(device) << "\",\n";
    stream << "  \"wall_seconds\": " << wallSeconds << ",\n";
    stream << "  \"observed_order\": ";
    if (observedOrder) stream << *observedOrder; else stream << "null";
    stream << ",\n  \"discretization_relative_uncertainty\": ";
    if (discretizationRelativeUncertainty) stream << *discretizationRelativeUncertainty; else stream << "null";
    stream << ",\n  \"criteria\": [";
    for (std::size_t index = 0; index < criteria.size(); ++index) {
        const auto& criterion = criteria[index];
        if (index > 0) stream << ',';
        stream << "\n    {\"name\":\"" << escapeJson(criterion.name) << "\",\"measured\":"
               << criterion.measured << ",\"threshold\":" << criterion.threshold
               << ",\"relation\":\""
               << (criterion.relation == CriterionRelation::LessEqual ? "<=\"" : ">=\"")
               << ",\"required\":" << (criterion.required ? "true" : "false")
               << ",\"passed\":" << (criterion.passed() ? "true" : "false") << '}';
    }
    if (!criteria.empty()) stream << '\n';
    stream << "  ],\n  \"notes\": [";
    for (std::size_t index = 0; index < notes.size(); ++index) {
        if (index > 0) stream << ',';
        stream << "\"" << escapeJson(notes[index]) << "\"";
    }
    stream << "]\n}";
    return stream.str();
}

} // namespace vulkax::verify
