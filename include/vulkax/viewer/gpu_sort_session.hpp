#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace vulkax::viewer {

enum class GpuSortAuthority {
    Validating,
    GpuTrusted,
    CpuFallback,
};

class GpuSortSession {
public:
    explicit GpuSortSession(std::size_t requiredParityPasses = 3U,
                            double maximumMedianGpuToCpuRatio = 0.95)
        : requiredParityPasses_(requiredParityPasses == 0U ? 1U : requiredParityPasses),
          maximumMedianGpuToCpuRatio_(
              std::isfinite(maximumMedianGpuToCpuRatio) && maximumMedianGpuToCpuRatio > 0.0
                  ? maximumMedianGpuToCpuRatio
                  : 0.95) {}

    void reset(bool gpuAvailable, std::string unavailableReason = {}) {
        parityPasses_ = 0U;
        performanceRatios_.clear();
        note_.clear();
        if (gpuAvailable) {
            authority_ = GpuSortAuthority::Validating;
        } else {
            authority_ = GpuSortAuthority::CpuFallback;
            note_ = unavailableReason.empty() ? "Metal GPU sorter unavailable" : std::move(unavailableReason);
        }
    }

    // Legacy correctness-only promotion used by focused unit tests/tools. The
    // interactive viewer uses recordValidation(), which additionally protects
    // against a correct-but-slower GPU path.
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

    // Performance-aware promotion. gpuPathMilliseconds and cpuPathMilliseconds
    // must measure comparable end-to-end ordering paths. GPU authority is granted
    // only after all required correctness passes and a median GPU/CPU ratio at or
    // below maximumMedianGpuToCpuRatio_.
    void recordValidation(bool valid,
                          double gpuPathMilliseconds,
                          double cpuPathMilliseconds,
                          std::string failureReason = {}) {
        if (authority_ != GpuSortAuthority::Validating) return;
        if (!valid) {
            authority_ = GpuSortAuthority::CpuFallback;
            note_ = failureReason.empty() ? "GPU sort parity validation failed" : std::move(failureReason);
            return;
        }
        if (!std::isfinite(gpuPathMilliseconds) ||
            !std::isfinite(cpuPathMilliseconds) ||
            gpuPathMilliseconds < 0.0 ||
            cpuPathMilliseconds <= 0.0) {
            authority_ = GpuSortAuthority::CpuFallback;
            note_ = "GPU sort validation produced invalid performance timing";
            return;
        }

        ++parityPasses_;
        performanceRatios_.push_back(gpuPathMilliseconds / cpuPathMilliseconds);
        if (parityPasses_ < requiredParityPasses_) return;

        const double ratio = medianPerformanceRatio();
        if (ratio <= maximumMedianGpuToCpuRatio_) {
            authority_ = GpuSortAuthority::GpuTrusted;
            note_.clear();
        } else {
            authority_ = GpuSortAuthority::CpuFallback;
            note_ = "GPU sort parity passed but median GPU/CPU ratio " +
                    std::to_string(ratio) +
                    " exceeded promotion threshold " +
                    std::to_string(maximumMedianGpuToCpuRatio_) +
                    "; using CPU ordering";
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
    [[nodiscard]] std::size_t performanceSamples() const noexcept { return performanceRatios_.size(); }
    [[nodiscard]] double maximumMedianGpuToCpuRatio() const noexcept {
        return maximumMedianGpuToCpuRatio_;
    }
    [[nodiscard]] double medianPerformanceRatio() const {
        if (performanceRatios_.empty()) return 0.0;
        auto values = performanceRatios_;
        std::sort(values.begin(), values.end());
        const std::size_t middle = values.size() / 2U;
        if ((values.size() & 1U) != 0U) return values[middle];
        return 0.5 * (values[middle - 1U] + values[middle]);
    }
    [[nodiscard]] const std::string& note() const noexcept { return note_; }

private:
    GpuSortAuthority authority_{GpuSortAuthority::CpuFallback};
    std::size_t parityPasses_{};
    std::size_t requiredParityPasses_{3U};
    double maximumMedianGpuToCpuRatio_{0.95};
    std::vector<double> performanceRatios_;
    std::string note_;
};

} // namespace vulkax::viewer
