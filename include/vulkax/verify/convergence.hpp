#pragma once

#include "vulkax/verify/result_certificate.hpp"

#include <string>
#include <vector>

namespace vulkax::verify {

struct ConvergenceSample {
    double characteristicSpacing{};
    double observable{};
};

struct ConvergenceEstimate {
    bool valid{false};
    double observedOrder{};
    double extrapolatedValue{};
    double absoluteUncertainty{};
    double relativeUncertainty{};
    std::string diagnostic;
};

[[nodiscard]] ConvergenceEstimate estimateRichardson(const std::vector<ConvergenceSample>& samples);
[[nodiscard]] ResultCertificate certificateFromConvergence(
    std::uint64_t problemHash, std::uint64_t solverHash, std::string backend, std::string device,
    const ConvergenceEstimate& convergence, double requestedRelativeTolerance,
    double residual, double residualTolerance, double conservationError, double conservationTolerance);

} // namespace vulkax::verify
