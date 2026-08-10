#include "vulkax/verify/result_certificate.hpp"

#include <cassert>
#include <string>

int main() {
    using namespace vulkax::verify;
    ResultCertificate certificate;
    certificate.problemHash = 0x1234;
    certificate.solverHash = 0x5678;
    certificate.backend = "Vulkan";
    certificate.device = "Test GPU";
    certificate.observedOrder = 2.01;
    certificate.discretizationRelativeUncertainty = 0.009;
    certificate.criteria.push_back({"mass conservation", 1.0e-7, 1.0e-5,
                                    CriterionRelation::LessEqual, true});
    certificate.criteria.push_back({"convergence order", 2.01, 1.5,
                                    CriterionRelation::GreaterEqual, true});
    certificate.updateTrustState(true);
    assert(certificate.state == TrustState::Verified);
    const std::string json = certificate.toJson();
    assert(json.find("\"trust_state\": \"verified\"") != std::string::npos);
    assert(json.find("mass conservation") != std::string::npos);

    certificate.criteria[0].measured = 1.0;
    certificate.updateTrustState(true);
    assert(certificate.state == TrustState::Rejected);
    return 0;
}
