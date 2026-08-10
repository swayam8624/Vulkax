#pragma once

#include "vulkax/problem/problem_ir.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace vulkax::operators {

// This is a bipartite field/operator view rather than a DAG. Coupled physical systems can be cyclic;
// forcing them into a topological ordering would encode a solver decision in the physics model.
class OperatorGraph {
  public:
    explicit OperatorGraph(const problem::ProblemIR& problem);

    [[nodiscard]] std::vector<std::string> operatorsReading(std::string_view fieldId) const;
    [[nodiscard]] std::vector<std::string> operatorsWriting(std::string_view fieldId) const;
    [[nodiscard]] const std::vector<problem::ResidualOperator>& operators() const noexcept;

  private:
    std::vector<problem::ResidualOperator> operators_;
};

struct OperatorInfluenceRequest {
    std::string observableId;
    std::vector<std::string> operatorIds;
};

} // namespace vulkax::operators
