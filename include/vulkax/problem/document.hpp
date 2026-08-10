#pragma once

#include "vulkax/problem/problem_ir.hpp"

#include <string>
#include <string_view>

namespace vulkax::problem {

// Human-editable, deterministic problem document. The grammar is line-oriented so documents are
// diff-friendly and do not depend on a third-party serialization runtime.
[[nodiscard]] ProblemIR parseProblemDocument(std::string_view source);
[[nodiscard]] ProblemIR loadProblemDocument(const std::string& path);
[[nodiscard]] std::string writeProblemDocument(const ProblemIR& problem);
void saveProblemDocument(const ProblemIR& problem, const std::string& path);

} // namespace vulkax::problem
