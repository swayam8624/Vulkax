#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace vulkax::optimize {

struct ScalarOptimum { double x{}; double value{}; std::size_t evaluations{}; bool converged{false}; };
[[nodiscard]] ScalarOptimum goldenSectionMinimize(const std::function<double(double)>& objective,
                                                  double lower, double upper, double tolerance = 1e-6,
                                                  std::size_t maxIterations = 128);

struct VectorOptimum { std::vector<double> x; double value{}; std::size_t iterations{}; bool converged{false}; };
[[nodiscard]] VectorOptimum projectedGradientMinimize(
    const std::function<double(const std::vector<double>&)>& objective,
    std::vector<double> initial, const std::vector<double>& lower, const std::vector<double>& upper,
    double gradientStep = 1e-5, double tolerance = 1e-6, std::size_t maxIterations = 200);

} // namespace vulkax::optimize
