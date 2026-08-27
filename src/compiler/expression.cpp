#include "vulkax/compiler/expression.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vulkax::compiler {

namespace {

using NodePtr = std::shared_ptr<const ExpressionNode>;

enum class TokenKind { End, Number, Identifier, Plus, Minus, Star, Slash, Caret, LeftParen, RightParen, Comma };

struct Token {
    TokenKind kind{TokenKind::End};
    double number{};
    std::string text;
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    Token next() {
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_])) != 0) {
            ++position_;
        }
        if (position_ >= source_.size()) {
            return {};
        }
        const char ch = source_[position_];
        switch (ch) {
        case '+': ++position_; return {TokenKind::Plus, 0.0, {}};
        case '-': ++position_; return {TokenKind::Minus, 0.0, {}};
        case '*': ++position_; return {TokenKind::Star, 0.0, {}};
        case '/': ++position_; return {TokenKind::Slash, 0.0, {}};
        case '^': ++position_; return {TokenKind::Caret, 0.0, {}};
        case '(': ++position_; return {TokenKind::LeftParen, 0.0, {}};
        case ')': ++position_; return {TokenKind::RightParen, 0.0, {}};
        case ',': ++position_; return {TokenKind::Comma, 0.0, {}};
        default: break;
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '.') {
            const std::size_t start = position_;
            bool exponentSeen = false;
            while (position_ < source_.size()) {
                const char current = source_[position_];
                if (std::isdigit(static_cast<unsigned char>(current)) != 0 || current == '.') {
                    ++position_;
                } else if ((current == 'e' || current == 'E') && !exponentSeen) {
                    exponentSeen = true;
                    ++position_;
                    if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) {
                        ++position_;
                    }
                } else {
                    break;
                }
            }
            const std::string text(source_.substr(start, position_ - start));
            char* end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            if (end == nullptr || *end != '\0' || !std::isfinite(value)) {
                throw std::invalid_argument("invalid numeric literal: " + text);
            }
            return {TokenKind::Number, value, text};
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            const std::size_t start = position_++;
            while (position_ < source_.size()) {
                const char current = source_[position_];
                if (std::isalnum(static_cast<unsigned char>(current)) == 0 && current != '_') {
                    break;
                }
                ++position_;
            }
            return {TokenKind::Identifier, 0.0, std::string(source_.substr(start, position_ - start))};
        }
        throw std::invalid_argument(std::string("unexpected character in expression: ") + ch);
    }

private:
    std::string_view source_;
    std::size_t position_{};
};

NodePtr node(ExpressionKind kind, NodePtr left = {}, NodePtr right = {}, std::string text = {}, double number = 0.0) {
    auto result = std::make_shared<ExpressionNode>();
    result->kind = kind;
    result->left = std::move(left);
    result->right = std::move(right);
    result->text = std::move(text);
    result->number = number;
    return result;
}

class Parser {
public:
    explicit Parser(std::string_view source) : lexer_(source), current_(lexer_.next()) {}

    Expression parse() {
        NodePtr root = parseAdditive();
        if (current_.kind != TokenKind::End) {
            throw std::invalid_argument("trailing tokens in expression");
        }
        return Expression(std::move(root));
    }

private:
    void advance() { current_ = lexer_.next(); }

    NodePtr parseAdditive() {
        NodePtr lhs = parseMultiplicative();
        while (current_.kind == TokenKind::Plus || current_.kind == TokenKind::Minus) {
            const TokenKind op = current_.kind;
            advance();
            NodePtr rhs = parseMultiplicative();
            lhs = node(op == TokenKind::Plus ? ExpressionKind::Add : ExpressionKind::Subtract,
                       std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    NodePtr parseMultiplicative() {
        NodePtr lhs = parsePower();
        while (current_.kind == TokenKind::Star || current_.kind == TokenKind::Slash) {
            const TokenKind op = current_.kind;
            advance();
            NodePtr rhs = parsePower();
            lhs = node(op == TokenKind::Star ? ExpressionKind::Multiply : ExpressionKind::Divide,
                       std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    NodePtr parsePower() {
        NodePtr lhs = parseUnary();
        if (current_.kind == TokenKind::Caret) {
            advance();
            lhs = node(ExpressionKind::Power, std::move(lhs), parsePower());
        }
        return lhs;
    }

    NodePtr parseUnary() {
        if (current_.kind == TokenKind::Minus) {
            advance();
            return node(ExpressionKind::Negate, parseUnary());
        }
        if (current_.kind == TokenKind::Plus) {
            advance();
            return parseUnary();
        }
        return parsePrimary();
    }

    NodePtr parsePrimary() {
        if (current_.kind == TokenKind::Number) {
            const double value = current_.number;
            advance();
            return node(ExpressionKind::Number, {}, {}, {}, value);
        }
        if (current_.kind == TokenKind::Identifier) {
            std::string identifier = current_.text;
            advance();
            if (current_.kind != TokenKind::LeftParen) {
                return node(ExpressionKind::Symbol, {}, {}, std::move(identifier));
            }
            advance();
            NodePtr argument = parseAdditive();
            if (current_.kind != TokenKind::RightParen) {
                throw std::invalid_argument("expected ')' after function argument");
            }
            advance();
            return node(ExpressionKind::Function, std::move(argument), {}, std::move(identifier));
        }
        if (current_.kind == TokenKind::LeftParen) {
            advance();
            NodePtr result = parseAdditive();
            if (current_.kind != TokenKind::RightParen) {
                throw std::invalid_argument("expected ')' in expression");
            }
            advance();
            return result;
        }
        throw std::invalid_argument("expected expression operand");
    }

    Lexer lexer_;
    Token current_;
};

double evalNode(const NodePtr& current, const std::unordered_map<std::string, double>& symbols) {
    switch (current->kind) {
    case ExpressionKind::Number: return current->number;
    case ExpressionKind::Symbol: {
        const auto iterator = symbols.find(current->text);
        if (iterator == symbols.end()) {
            throw std::invalid_argument("missing symbol value: " + current->text);
        }
        return iterator->second;
    }
    case ExpressionKind::Add: return evalNode(current->left, symbols) + evalNode(current->right, symbols);
    case ExpressionKind::Subtract: return evalNode(current->left, symbols) - evalNode(current->right, symbols);
    case ExpressionKind::Multiply: return evalNode(current->left, symbols) * evalNode(current->right, symbols);
    case ExpressionKind::Divide: return evalNode(current->left, symbols) / evalNode(current->right, symbols);
    case ExpressionKind::Power: return std::pow(evalNode(current->left, symbols), evalNode(current->right, symbols));
    case ExpressionKind::Negate: return -evalNode(current->left, symbols);
    case ExpressionKind::Function: {
        const double value = evalNode(current->left, symbols);
        if (current->text == "sin") return std::sin(value);
        if (current->text == "cos") return std::cos(value);
        if (current->text == "exp") return std::exp(value);
        if (current->text == "log") return std::log(value);
        if (current->text == "sqrt") return std::sqrt(value);
        if (current->text == "abs") return std::abs(value);
        throw std::invalid_argument("unknown function: " + current->text);
    }
    }
    throw std::logic_error("unreachable expression kind");
}

units::Dimension scaleDimension(units::Dimension dimension, int exponent) {
    for (auto& value : dimension.exponent) {
        const int scaled = static_cast<int>(value) * exponent;
        if (scaled < std::numeric_limits<std::int8_t>::min() ||
            scaled > std::numeric_limits<std::int8_t>::max()) {
            throw std::overflow_error("physical dimension exponent overflow");
        }
        value = static_cast<std::int8_t>(scaled);
    }
    return dimension;
}

units::Dimension dimensionNode(
    const NodePtr& current,
    const std::unordered_map<std::string, units::Dimension>& symbolDimensions) {
    switch (current->kind) {
    case ExpressionKind::Number: return units::dimensionless;
    case ExpressionKind::Symbol: {
        const auto iterator = symbolDimensions.find(current->text);
        if (iterator == symbolDimensions.end()) {
            throw std::invalid_argument("missing symbol dimension: " + current->text);
        }
        return iterator->second;
    }
    case ExpressionKind::Add:
    case ExpressionKind::Subtract: {
        const auto lhs = dimensionNode(current->left, symbolDimensions);
        const auto rhs = dimensionNode(current->right, symbolDimensions);
        if (!(lhs == rhs)) {
            throw std::invalid_argument("addition/subtraction requires identical dimensions");
        }
        return lhs;
    }
    case ExpressionKind::Multiply:
        return units::multiply(dimensionNode(current->left, symbolDimensions),
                               dimensionNode(current->right, symbolDimensions));
    case ExpressionKind::Divide:
        return units::divide(dimensionNode(current->left, symbolDimensions),
                             dimensionNode(current->right, symbolDimensions));
    case ExpressionKind::Power: {
        const auto exponentDimension = dimensionNode(current->right, symbolDimensions);
        if (!(exponentDimension == units::dimensionless) || current->right->kind != ExpressionKind::Number) {
            throw std::invalid_argument("dimensioned powers require a literal dimensionless exponent");
        }
        const double exponentValue = current->right->number;
        const double rounded = std::round(exponentValue);
        if (std::abs(exponentValue - rounded) > 1.0e-12) {
            const auto baseDimension = dimensionNode(current->left, symbolDimensions);
            if (!(baseDimension == units::dimensionless)) {
                throw std::invalid_argument("fractional powers of dimensioned quantities are not supported");
            }
            return units::dimensionless;
        }
        return scaleDimension(dimensionNode(current->left, symbolDimensions), static_cast<int>(rounded));
    }
    case ExpressionKind::Negate: return dimensionNode(current->left, symbolDimensions);
    case ExpressionKind::Function: {
        const auto argument = dimensionNode(current->left, symbolDimensions);
        if (current->text == "abs") return argument;
        if (current->text == "sqrt") {
            units::Dimension result = argument;
            for (auto& exponent : result.exponent) {
                if (static_cast<int>(exponent) % 2 != 0) {
                    throw std::invalid_argument("sqrt requires even physical dimension exponents");
                }
                exponent = static_cast<std::int8_t>(exponent / 2);
            }
            return result;
        }
        if (!(argument == units::dimensionless)) {
            throw std::invalid_argument(current->text + " requires a dimensionless argument");
        }
        if (current->text == "sin" || current->text == "cos" || current->text == "exp" || current->text == "log") {
            return units::dimensionless;
        }
        throw std::invalid_argument("unknown function: " + current->text);
    }
    }
    throw std::logic_error("unreachable expression kind");
}

std::string canonicalNode(const NodePtr& current) {
    std::ostringstream stream;
    stream << std::setprecision(17);
    switch (current->kind) {
    case ExpressionKind::Number: stream << current->number; break;
    case ExpressionKind::Symbol: stream << current->text; break;
    case ExpressionKind::Add: stream << '(' << canonicalNode(current->left) << '+' << canonicalNode(current->right) << ')'; break;
    case ExpressionKind::Subtract: stream << '(' << canonicalNode(current->left) << '-' << canonicalNode(current->right) << ')'; break;
    case ExpressionKind::Multiply: stream << '(' << canonicalNode(current->left) << '*' << canonicalNode(current->right) << ')'; break;
    case ExpressionKind::Divide: stream << '(' << canonicalNode(current->left) << '/' << canonicalNode(current->right) << ')'; break;
    case ExpressionKind::Power: stream << '(' << canonicalNode(current->left) << '^' << canonicalNode(current->right) << ')'; break;
    case ExpressionKind::Negate: stream << "(-" << canonicalNode(current->left) << ')'; break;
    case ExpressionKind::Function: stream << current->text << '(' << canonicalNode(current->left) << ')'; break;
    }
    return stream.str();
}

} // namespace

Expression::Expression(std::shared_ptr<const ExpressionNode> root) : root_(std::move(root)) {
    if (!root_) {
        throw std::invalid_argument("Expression requires a root node");
    }
}

Expression compileExpression(std::string_view source) { return Parser(source).parse(); }

double evaluate(const Expression& expression, const std::unordered_map<std::string, double>& symbols) {
    return evalNode(expression.root(), symbols);
}

units::Dimension inferDimension(
    const Expression& expression,
    const std::unordered_map<std::string, units::Dimension>& symbolDimensions) {
    return dimensionNode(expression.root(), symbolDimensions);
}

std::string canonicalExpression(const Expression& expression) { return canonicalNode(expression.root()); }

} // namespace vulkax::compiler