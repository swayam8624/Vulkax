#include "vulkax/verify/result_certificate.hpp"

#include <algorithm>

namespace vulkax::verify {

bool VerificationCriterion::passed() const noexcept {
    switch (relation) {
    case CriterionRelation::LessEqual:
        return measured <= threshold;
    case CriterionRelation::GreaterEqual:
        return measured >= threshold;
    }
    return false;
}

bool ResultCertificate::requiredEvidencePasses() const noexcept {
    return std::all_of(criteria.begin(), criteria.end(), [](const VerificationCriterion& criterion) {
        return !criterion.required || criterion.passed();
    });
}

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

} // namespace vulkax::verify
