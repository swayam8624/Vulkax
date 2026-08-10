#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace vulkax::autodiff {

using State=std::vector<double>;
using Parameters=std::vector<double>;
using StepFunction=std::function<State(const State&,const Parameters&,double dt)>;
using ObjectiveFunction=std::function<double(const State&)>;

struct AdjointOptions{
    double stateFiniteDifferenceStep{1.0e-6};
    double parameterFiniteDifferenceStep{1.0e-6};
};

struct AdjointResult{
    std::vector<State> trajectory;
    double objective{};
    Parameters gradient;
};

[[nodiscard]] AdjointResult differentiateTrajectory(const StepFunction&step,
                                                     const ObjectiveFunction&objective,
                                                     State initialState,
                                                     const Parameters&parameters,
                                                     double dt,std::size_t steps,
                                                     const AdjointOptions&options={});
[[nodiscard]] Parameters finiteDifferenceTrajectoryGradient(const StepFunction&step,
                                                             const ObjectiveFunction&objective,
                                                             const State&initialState,
                                                             const Parameters&parameters,
                                                             double dt,std::size_t steps,
                                                             double relativeStep=1.0e-6);

} // namespace vulkax::autodiff
