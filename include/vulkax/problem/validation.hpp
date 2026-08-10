#pragma once

#include "vulkax/problem/problem_ir.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace vulkax::problem {

enum class IssueSeverity { Warning, Error };

struct ValidationIssue {
    IssueSeverity severity{IssueSeverity::Error};
    std::string path;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] bool ok() const;
    [[nodiscard]] std::size_t errorCount() const;
    [[nodiscard]] std::size_t warningCount() const;
};

[[nodiscard]] ValidationReport validateProblem(const ProblemIR& problem);

} // namespace vulkax::problem
