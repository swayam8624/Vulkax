#pragma once

#include <span>
#include <string>
#include <vector>

namespace vulkax::research {

struct OperatorInfluenceField {
    std::string operatorId;
    std::vector<double> values;
};

[[nodiscard]] OperatorInfluenceField computeOperatorInfluence(std::string operatorId,
                                                              std::span<const double> adjoint,
                                                              std::span<const double> residualTerm);
[[nodiscard]] double predictObjectiveDelta(const OperatorInfluenceField& influence,
                                           std::span<const double> fractionalIntervention,
                                           double quadratureWeight = 1.0);
[[nodiscard]] double relativeCounterfactualError(double predictedDelta, double measuredDelta,
                                                 double floor = 1.0e-12);

} // namespace vulkax::research
