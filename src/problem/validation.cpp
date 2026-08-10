#include "vulkax/problem/validation.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace vulkax::problem {
namespace {

template <typename T>
void checkUniqueIds(const std::vector<T>& values, std::string_view collection, ValidationReport& report) {
    std::unordered_set<std::string> seen;
    for (const auto& value : values) {
        if (value.id.empty()) {
            report.issues.push_back({IssueSeverity::Error, std::string(collection), "id must not be empty"});
        } else if (!seen.insert(value.id).second) {
            report.issues.push_back(
                {IssueSeverity::Error, std::string(collection) + "." + value.id, "duplicate id"});
        }
    }
}

} // namespace

bool ValidationReport::ok() const {
    return std::none_of(issues.begin(), issues.end(), [](const ValidationIssue& issue) {
        return issue.severity == IssueSeverity::Error;
    });
}

std::size_t ValidationReport::errorCount() const {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.severity == IssueSeverity::Error;
    }));
}

std::size_t ValidationReport::warningCount() const {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.severity == IssueSeverity::Warning;
    }));
}

ValidationReport validateProblem(const ProblemIR& problem) {
    ValidationReport report;
    if (problem.id.empty()) {
        report.issues.push_back({IssueSeverity::Error, "problem.id", "problem id must not be empty"});
    }
    if (problem.name.empty()) {
        report.issues.push_back({IssueSeverity::Warning, "problem.name", "problem has no display name"});
    }

    checkUniqueIds(problem.domains, "domains", report);
    checkUniqueIds(problem.fields, "fields", report);
    checkUniqueIds(problem.operators, "operators", report);
    checkUniqueIds(problem.materials, "materials", report);
    checkUniqueIds(problem.boundaryConditions, "boundaryConditions", report);
    checkUniqueIds(problem.objectives, "objectives", report);

    std::unordered_map<std::string, const Domain*> domains;
    for (const auto& domain : problem.domains) {
        domains[domain.id] = &domain;
        if (domain.spatialDimensions == 0 || domain.spatialDimensions > 3) {
            report.issues.push_back({IssueSeverity::Error, "domains." + domain.id,
                                     "spatial dimensions must be in [1, 3]"});
        }
    }

    std::unordered_map<std::string, const Field*> fields;
    for (const auto& field : problem.fields) {
        fields[field.id] = &field;
        if (!domains.contains(field.domainId)) {
            report.issues.push_back({IssueSeverity::Error, "fields." + field.id + ".domainId",
                                     "references an unknown domain"});
        }
        if (field.components == 0) {
            report.issues.push_back({IssueSeverity::Error, "fields." + field.id + ".components",
                                     "component count must be positive"});
        }
        if (field.rank == FieldRank::Scalar && field.components != 1) {
            report.issues.push_back({IssueSeverity::Error, "fields." + field.id + ".components",
                                     "scalar fields must have exactly one component"});
        }
    }

    for (const auto& op : problem.operators) {
        if (!fields.contains(op.outputFieldId)) {
            report.issues.push_back({IssueSeverity::Error, "operators." + op.id + ".outputFieldId",
                                     "references an unknown output field"});
        }
        for (const auto& input : op.inputFieldIds) {
            if (!fields.contains(input)) {
                report.issues.push_back({IssueSeverity::Error, "operators." + op.id + ".inputFieldIds",
                                         "references unknown field '" + input + "'"});
            }
        }
        if (op.expression.empty()) {
            report.issues.push_back({IssueSeverity::Warning, "operators." + op.id + ".expression",
                                     "operator has no symbolic/operator expression"});
        }
    }

    for (const auto& boundary : problem.boundaryConditions) {
        if (!domains.contains(boundary.domainId)) {
            report.issues.push_back({IssueSeverity::Error, "boundaryConditions." + boundary.id + ".domainId",
                                     "references an unknown domain"});
        }
        const auto field = fields.find(boundary.fieldId);
        if (field == fields.end()) {
            report.issues.push_back({IssueSeverity::Error, "boundaryConditions." + boundary.id + ".fieldId",
                                     "references an unknown field"});
        } else if (!(field->second->physicalDimension == boundary.physicalDimension)) {
            report.issues.push_back({IssueSeverity::Error, "boundaryConditions." + boundary.id,
                                     "boundary value dimension does not match the field dimension"});
        }
    }

    std::unordered_set<std::string> objectiveIds;
    for (const auto& objective : problem.objectives) {
        objectiveIds.insert(objective.id);
        if (objective.expression.empty()) {
            report.issues.push_back({IssueSeverity::Error, "objectives." + objective.id + ".expression",
                                     "objective expression must not be empty"});
        }
    }

    for (const auto& accuracy : problem.accuracyTargets) {
        if (!objectiveIds.contains(accuracy.observableId)) {
            report.issues.push_back({IssueSeverity::Error, "accuracyTargets." + accuracy.observableId,
                                     "accuracy target must reference an objective/observable"});
        }
        if (!(accuracy.relativeTolerance > 0.0)) {
            report.issues.push_back({IssueSeverity::Error, "accuracyTargets." + accuracy.observableId,
                                     "relative tolerance must be positive"});
        }
        if (accuracy.absoluteTolerance && !(*accuracy.absoluteTolerance > 0.0)) {
            report.issues.push_back({IssueSeverity::Error, "accuracyTargets." + accuracy.observableId,
                                     "absolute tolerance must be positive when present"});
        }
    }

    if (problem.computeBudget.wallSeconds && !(*problem.computeBudget.wallSeconds > 0.0)) {
        report.issues.push_back({IssueSeverity::Error, "computeBudget.wallSeconds",
                                 "wall-time budget must be positive"});
    }
    if (problem.computeBudget.gpuMemoryBytes && *problem.computeBudget.gpuMemoryBytes == 0) {
        report.issues.push_back({IssueSeverity::Error, "computeBudget.gpuMemoryBytes",
                                 "GPU memory budget must be non-zero"});
    }

    return report;
}

} // namespace vulkax::problem
