#pragma once

#include "vulkax/core/units.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vulkax::compiler {

enum class ExpressionKind {
    Number,
    Symbol,
    Add,
    Subtract,
    Multiply,
    Divide,
    Power,
    Negate,
    Function,
};

struct ExpressionNode {
    ExpressionKind kind{ExpressionKind::Number};
    double number{};
    std::string text;
    std::shared_ptr<const ExpressionNode> left;
    std::shared_ptr<const ExpressionNode> right;
};

class Expression {
public:
    explicit Expression(std::shared_ptr<const ExpressionNode> root);
    [[nodiscard]] const std::shared_ptr<const ExpressionNode>& root() const noexcept { return root_; }

private:
    std::shared_ptr<const ExpressionNode> root_;
};

[[nodiscard]] Expression compileExpression(std::string_view source);
[[nodiscard]] double evaluate(const Expression& expression,
                              const std::unordered_map<std::string, double>& symbols);
[[nodiscard]] units::Dimension inferDimension(
    const Expression& expression,
    const std::unordered_map<std::string, units::Dimension>& symbolDimensions);
[[nodiscard]] std::string canonicalExpression(const Expression& expression);

} // namespace vulkax::compiler
