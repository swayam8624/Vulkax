#pragma once

#include "vulkax/problem/problem_ir.hpp"

#include <cstddef>
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

    struct StructuralInfluence {
        std::string operatorId;
        std::string outputFieldId;
        std::size_t depth{};
    };

    struct UpstreamTrace {
        std::string observableFieldId;
        std::vector<std::string> visitedFields;
        std::vector<StructuralInfluence> operators;
    };

    // Traces structural upstream reachability through field <- operator <- input-field
    // links. Cycles are expected and are terminated by visited sets. This establishes
    // possible model influence, not numerical sensitivity or formal causal effect.
    [[nodiscard]] UpstreamTrace traceUpstream(std::string_view observableFieldId,
                                              std::size_t maxDepth = 32) const;

  private:
    std::vector<problem::ResidualOperator> operators_;
};

struct OperatorInfluenceRequest {
    std::string observableId;
    std::vector<std::string> operatorIds;
};

} // namespace vulkax::operators
