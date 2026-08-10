#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace vulkax::optimize {

struct ParameterBound {
    std::string name;
    double minimum{};
    double maximum{};
};

struct OptimizationResult {
    std::vector<double> parameters;
    double objective{};
    std::size_t evaluations{};
    bool converged{false};
};

using ObjectiveFunction = std::function<double(const std::vector<double>&)>;

[[nodiscard]] std::vector<double> finiteDifferenceGradient(const ObjectiveFunction& objective,
                                                           const std::vector<double>& point,
                                                           double relativeStep = 1.0e-6);
[[nodiscard]] OptimizationResult boundedCoordinateSearch(const ObjectiveFunction& objective,
                                                         const std::vector<ParameterBound>& bounds,
                                                         const std::vector<double>& initial,
                                                         std::size_t maxIterations = 200,
                                                         double relativeTolerance = 1.0e-8);

} // namespace vulkax::optimize
