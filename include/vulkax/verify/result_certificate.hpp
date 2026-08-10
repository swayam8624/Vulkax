#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vulkax::verify {

enum class TrustState : std::uint8_t { Preview, Converging, Verified, Rejected };
enum class CriterionRelation : std::uint8_t { LessEqual, GreaterEqual };

struct VerificationCriterion {
    std::string name;
    double measured{};
    double threshold{};
    CriterionRelation relation{CriterionRelation::LessEqual};
    bool required{true};

    [[nodiscard]] bool passed() const noexcept;
};

struct ResultCertificate {
    std::uint64_t problemHash{};
    std::uint64_t solverHash{};
    TrustState state{TrustState::Preview};
    std::string backend;
    std::string device;
    double wallSeconds{};
    std::vector<VerificationCriterion> criteria;
    std::vector<std::string> notes;

    [[nodiscard]] bool requiredEvidencePasses() const noexcept;
    void updateTrustState(bool convergenceStudyComplete) noexcept;
};

} // namespace vulkax::verify
