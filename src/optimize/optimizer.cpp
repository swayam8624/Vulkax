#include "vulkax/optimize/optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vulkax::optimize {

std::vector<double> finiteDifferenceGradient(const ObjectiveFunction& objective,
                                             const std::vector<double>& point,
                                             double relativeStep) {
    if (!objective || relativeStep <= 0.0) {
        throw std::invalid_argument("invalid finite-difference gradient request");
    }
    std::vector<double> gradient(point.size(), 0.0);
    for (std::size_t index = 0; index < point.size(); ++index) {
        const double step = relativeStep * std::max(1.0, std::abs(point[index]));
        auto plus = point;
        auto minus = point;
        plus[index] += step;
        minus[index] -= step;
        gradient[index] = (objective(plus) - objective(minus)) / (2.0 * step);
    }
    return gradient;
}

OptimizationResult boundedCoordinateSearch(const ObjectiveFunction& objective,
                                           const std::vector<ParameterBound>& bounds,
                                           const std::vector<double>& initial,
                                           std::size_t maxIterations,
                                           double relativeTolerance) {
    if (!objective || bounds.empty() || bounds.size() != initial.size() || maxIterations == 0 ||
        relativeTolerance <= 0.0) {
        throw std::invalid_argument("invalid coordinate-search configuration");
    }
    std::vector<double> point = initial;
    std::vector<double> step(bounds.size(), 0.0);
    for (std::size_t index = 0; index < bounds.size(); ++index) {
        if (!(bounds[index].minimum < bounds[index].maximum)) {
            throw std::invalid_argument("optimizer bounds must have positive width");
        }
        point[index] = std::clamp(point[index], bounds[index].minimum, bounds[index].maximum);
        step[index] = 0.25 * (bounds[index].maximum - bounds[index].minimum);
    }

    OptimizationResult result;
    result.parameters = point;
    result.objective = objective(point);
    result.evaluations = 1;

    for (std::size_t iteration = 0; iteration < maxIterations; ++iteration) {
        bool improved = false;
        for (std::size_t index = 0; index < bounds.size(); ++index) {
            for (double direction : {-1.0, 1.0}) {
                auto candidate = result.parameters;
                candidate[index] = std::clamp(candidate[index] + direction * step[index],
                                              bounds[index].minimum, bounds[index].maximum);
                if (candidate[index] == result.parameters[index]) {
                    continue;
                }
                const double value = objective(candidate);
                ++result.evaluations;
                if (value < result.objective) {
                    result.objective = value;
                    result.parameters = std::move(candidate);
                    improved = true;
                }
            }
        }
        if (!improved) {
            double largestRelativeStep = 0.0;
            for (std::size_t index = 0; index < step.size(); ++index) {
                step[index] *= 0.5;
                const double width = bounds[index].maximum - bounds[index].minimum;
                largestRelativeStep = std::max(largestRelativeStep, step[index] / width);
            }
            if (largestRelativeStep < relativeTolerance) {
                result.converged = true;
                break;
            }
        }
    }
    return result;
}

} // namespace vulkax::optimize
