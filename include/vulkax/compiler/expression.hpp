#pragma once

#include "vulkax/core/units.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vulkax::compiler {

enum class ExprKind { Constant, Variable, Add, Subtract, Multiply, Divide, Negate, Power, Function };

struct ExprNode {
    ExprKind kind{ExprKind::Constant};
    double value{};
    std::string symbol;
    std::shared_ptr<ExprNode> left;
    std::shared_ptr<ExprNode> right;
};

struct ParseResult {
    std::shared_ptr<ExprNode> root;
    std::string diagnostic;
    [[nodiscard]] bool ok() const noexcept { return static_cast<bool>(root) && diagnostic.empty(); }
};

[[nodiscard]] ParseResult parseExpression(std::string_view source);
[[nodiscard]] double evaluateExpression(const ExprNode& node,
                                        const std::unordered_map<std::string, double>& variables);
[[nodiscard]] std::shared_ptr<ExprNode> differentiate(const ExprNode& node, std::string_view variable);
[[nodiscard]] std::string canonicalExpression(const ExprNode& node);
[[nodiscard]] std::optional<units::Dimension> inferDimension(
    const ExprNode& node, const std::unordered_map<std::string, units::Dimension>& symbols,
    std::string* diagnostic = nullptr);

} // namespace vulkax::compiler
