#include "vulkax/operators/operator_graph.hpp"

#include <algorithm>
#include <deque>
#include <unordered_set>
#include <utility>

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

OperatorGraph::UpstreamTrace OperatorGraph::traceUpstream(std::string_view observableFieldId,
                                                          std::size_t maxDepth) const {
    UpstreamTrace trace;
    trace.observableFieldId = std::string(observableFieldId);
    if (observableFieldId.empty()) return trace;

    std::deque<std::pair<std::string, std::size_t>> frontier;
    frontier.emplace_back(observableFieldId, 0U);
    std::unordered_set<std::string> visitedFields;
    std::unordered_set<std::string> visitedOperators;

    while (!frontier.empty()) {
        auto [fieldId, depth] = std::move(frontier.front());
        frontier.pop_front();
        if (!visitedFields.insert(fieldId).second) continue;
        trace.visitedFields.push_back(fieldId);
        if (depth > maxDepth) continue;

        for (const auto& op : operators_) {
            if (op.outputFieldId != fieldId) continue;
            if (visitedOperators.insert(op.id).second) {
                trace.operators.push_back({op.id, op.outputFieldId, depth});
            }
            if (depth == maxDepth) continue;
            for (const auto& inputField : op.inputFieldIds) {
                if (!visitedFields.contains(inputField)) {
                    frontier.emplace_back(inputField, depth + 1U);
                }
            }
        }
    }

    std::sort(trace.visitedFields.begin(), trace.visitedFields.end());
    std::stable_sort(trace.operators.begin(), trace.operators.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.depth != rhs.depth) return lhs.depth < rhs.depth;
        return lhs.operatorId < rhs.operatorId;
    });
    return trace;
}

} // namespace vulkax::operators
