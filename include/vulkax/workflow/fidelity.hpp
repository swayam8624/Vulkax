#pragma once

#include "vulkax/verify/convergence.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vulkax::workflow {

struct FidelityLevel { std::string label; double spacing{}; double expectedCost{}; };
struct FidelityObservation { FidelityLevel level; double observable{}; double residual{}; double conservationError{}; double wallSeconds{}; };
struct FidelityPolicy { double relativeTolerance{0.02}; double residualTolerance{1e-6}; double conservationTolerance{1e-6}; };
struct FidelityRun { std::vector<FidelityObservation> observations; verify::ConvergenceEstimate convergence; verify::ResultCertificate certificate; };
using FidelityExecutor = std::function<FidelityObservation(const FidelityLevel&)>;

[[nodiscard]] FidelityRun runFidelityLadder(std::uint64_t problemHash, std::uint64_t solverHash,
                                            std::string backend, std::string device,
                                            const std::vector<FidelityLevel>& levels,
                                            const FidelityPolicy& policy,
                                            const FidelityExecutor& executor,
                                            bool stopWhenVerified = true);

} // namespace vulkax::workflow
