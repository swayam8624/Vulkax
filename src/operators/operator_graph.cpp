#include "vulkax/operators/operator_graph.hpp"

#include <algorithm>

namespace vulkax::operators {

OperatorGraph::OperatorGraph(const problem::ProblemIR& problem) : operators_(problem.operators) {}

std::vector<std::string> OperatorGraph::operatorsReading(std::string_view fieldId) const {
    std::vector<std::string> result;
    for (const auto& op : operators_) {
        if (std::find(op.inputFieldIds.begin(), op.inputFieldIds.end(), fieldId) !=
            op.inputFieldIds.end()) {
            result.push_back(op.id);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> OperatorGraph::operatorsWriting(std::string_view fieldId) const {
    std::vector<std::string> result;
    for (const auto& op : operators_) {
        if (op.outputFieldId == fieldId) {
            result.push_back(op.id);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

const std::vector<problem::ResidualOperator>& OperatorGraph::operators() const noexcept {
    return operators_;
}

} // namespace vulkax::operators
