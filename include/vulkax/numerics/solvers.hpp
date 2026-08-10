#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <vector>

namespace vulkax::numerics {

using Vector = std::vector<double>;
using LinearOperator = std::function<void(std::span<const double>, std::span<double>)>;

struct IterativeResult {
    Vector x;
    std::size_t iterations{};
    double residualNorm{};
    bool converged{false};
};

[[nodiscard]] IterativeResult conjugateGradient(const LinearOperator& A, std::span<const double> b,
                                                Vector initial, double tolerance = 1e-10,
                                                std::size_t maxIterations = 1000);

using OdeRhs = std::function<void(double, std::span<const double>, std::span<double>)>;
[[nodiscard]] Vector rk4Step(const OdeRhs& rhs, double t, std::span<const double> state, double dt);

using NonlinearResidual = std::function<void(std::span<const double>, std::span<double>)>;
[[nodiscard]] IterativeResult newtonSolve(const NonlinearResidual& residual, Vector initial,
                                          double tolerance = 1e-10, std::size_t maxIterations = 50,
                                          double finiteDifferenceStep = 1e-6);

[[nodiscard]] double cflTimeStep(double cellSize, double maxSpeed, double safety = 0.7,
                                 double ceiling = 1.0);

} // namespace vulkax::numerics
