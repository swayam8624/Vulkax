#pragma once

#include <cstddef>
#include <string>
#include <utility>

namespace vulkax::viewer {

enum class GpuSortAuthority {
    Validating,
    GpuTrusted,
    CpuFallback,
};

class GpuSortSession {
public:
    explicit GpuSortSession(std::size_t requiredParityPasses = 3U)
        : requiredParityPasses_(requiredParityPasses == 0U ? 1U : requiredParityPasses) {}

    void reset(bool gpuAvailable, std::string unavailableReason = {}) {
        parityPasses_ = 0U;
        note_.clear();
        if (gpuAvailable) {
            authority_ = GpuSortAuthority::Validating;
        } else {
            authority_ = GpuSortAuthority::CpuFallback;
            note_ = unavailableReason.empty() ? "Metal GPU sorter unavailable" : std::move(unavailableReason);
        }
    }

    void recordParity(bool valid, std::string failureReason = {}) {
        if (authority_ != GpuSortAuthority::Validating) return;
        if (!valid) {
            authority_ = GpuSortAuthority::CpuFallback;
            note_ = failureReason.empty() ? "GPU sort parity validation failed" : std::move(failureReason);
            return;
        }
        ++parityPasses_;
        if (parityPasses_ >= requiredParityPasses_) {
            authority_ = GpuSortAuthority::GpuTrusted;
            note_.clear();
        }
    }

    void recordRuntimeFailure(std::string reason = {}) {
        authority_ = GpuSortAuthority::CpuFallback;
        note_ = reason.empty() ? "GPU sort runtime failure" : std::move(reason);
    }

    [[nodiscard]] GpuSortAuthority authority() const noexcept { return authority_; }
    [[nodiscard]] bool validating() const noexcept { return authority_ == GpuSortAuthority::Validating; }
    [[nodiscard]] bool gpuTrusted() const noexcept { return authority_ == GpuSortAuthority::GpuTrusted; }
    [[nodiscard]] bool cpuFallback() const noexcept { return authority_ == GpuSortAuthority::CpuFallback; }
    [[nodiscard]] std::size_t parityPasses() const noexcept { return parityPasses_; }
    [[nodiscard]] std::size_t requiredParityPasses() const noexcept { return requiredParityPasses_; }
    [[nodiscard]] const std::string& note() const noexcept { return note_; }

private:
    GpuSortAuthority authority_{GpuSortAuthority::CpuFallback};
    std::size_t parityPasses_{};
    std::size_t requiredParityPasses_{3U};
    std::string note_;
};

} // namespace vulkax::viewer
