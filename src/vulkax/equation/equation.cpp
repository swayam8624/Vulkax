#include "vulkax/equation/equation.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string_view>

namespace vulkax::equation {
namespace {

class Parser {
 public:
  explicit Parser(std::string_view source) : source_(source) {}

  EquationNode parse() {
    EquationNode result = parseExpression();
    skipWhitespace();
    if (!atEnd()) {
      fail("unexpected token");
    }
    return result;
  }

 private:
  EquationNode parseExpression() {
    EquationNode left = parseTerm();
    while (true) {
      if (consume('+')) {
        left = binary(NodeKind::Add, std::move(left), parseTerm());
      } else if (consume('-')) {
        left = binary(NodeKind::Subtract, std::move(left), parseTerm());
      } else {
        return left;
      }
    }
  }

  EquationNode parseTerm() {
    EquationNode left = parseUnary();
    while (true) {
      if (consume('*')) {
        left = binary(NodeKind::Multiply, std::move(left), parseUnary());
      } else if (consume('/')) {
        left = binary(NodeKind::Divide, std::move(left), parseUnary());
      } else {
        return left;
      }
    }
  }

  EquationNode parsePower() {
    EquationNode left = parsePrimary();
    if (consume('^')) {
      return binary(NodeKind::Power, std::move(left), parseUnary());
    }
    return left;
  }

  EquationNode parseUnary() {
    if (consume('-')) {
      return {NodeKind::Negate, 0.0, {}, {parseUnary()}};
    }
    if (consume('+')) return parseUnary();
    return parsePower();
  }

  EquationNode parsePrimary() {
    skipWhitespace();
    if (consume('(')) {
      EquationNode node = parseExpression();
      expect(')');
      return node;
    }
    if (position_ < source_.size() &&
        (std::isdigit(static_cast<unsigned char>(source_[position_])) ||
         source_[position_] == '.')) {
      return {NodeKind::Constant, parseNumber(), {}, {}};
    }
    const std::string identifier = parseIdentifier();
    if (identifier.empty()) fail("expected a number, variable, or parenthesized expression");
    if (consume('(')) {
      std::vector<EquationNode> arguments;
      if (!consume(')')) {
        do {
          arguments.push_back(parseExpression());
        } while (consume(','));
        expect(')');
      }
      return {NodeKind::Function, 0.0, identifier, std::move(arguments)};
    }
    if (identifier == "pi") return {NodeKind::Constant, std::numbers::pi, {}, {}};
    if (identifier == "e") return {NodeKind::Constant, std::numbers::e, {}, {}};
    return {NodeKind::Variable, 0.0, identifier, {}};
  }

  double parseNumber() {
    skipWhitespace();
    const size_t begin = position_;
    bool seenExponent = false;
    while (position_ < source_.size()) {
      const char character = source_[position_];
      if (std::isdigit(static_cast<unsigned char>(character)) || character == '.') {
        ++position_;
      } else if ((character == 'e' || character == 'E') && !seenExponent) {
        seenExponent = true;
        ++position_;
        if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-'))
          ++position_;
      } else {
        break;
      }
    }
    try {
      return std::stod(std::string{source_.substr(begin, position_ - begin)});
    } catch (const std::exception&) {
      fail("invalid numeric literal");
    }
  }

  std::string parseIdentifier() {
    skipWhitespace();
    const size_t begin = position_;
    if (position_ >= source_.size() ||
        !(std::isalpha(static_cast<unsigned char>(source_[position_])) ||
          source_[position_] == '_'))
      return {};
    ++position_;
    while (
        position_ < source_.size() &&
        (std::isalnum(static_cast<unsigned char>(source_[position_])) || source_[position_] == '_'))
      ++position_;
    return std::string{source_.substr(begin, position_ - begin)};
  }

  bool consume(char expected) {
    skipWhitespace();
    if (position_ < source_.size() && source_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void expect(char expected) {
    if (!consume(expected)) fail(std::string{"expected '"} + expected + "'");
  }

  void skipWhitespace() {
    while (position_ < source_.size() &&
           std::isspace(static_cast<unsigned char>(source_[position_])))
      ++position_;
  }

  [[noreturn]] void fail(const std::string& message) const {
    throw std::invalid_argument(
        "equation parse error at column " + std::to_string(position_ + 1) + ": " + message);
  }

  static EquationNode binary(NodeKind kind, EquationNode left, EquationNode right) {
    return {kind, 0.0, {}, {std::move(left), std::move(right)}};
  }

  bool atEnd() const { return position_ >= source_.size(); }

  std::string_view source_;
  size_t position_ = 0;
};

double evaluateNode(
    const EquationNode& node, const std::unordered_map<std::string, double>& variables) {
  auto argument = [&](size_t index) -> double {
    if (index >= node.children.size()) {
      throw std::invalid_argument("function '" + node.symbol + "' is missing an argument");
    }
    return evaluateNode(node.children[index], variables);
  };
  switch (node.kind) {
    case NodeKind::Constant:
      return node.value;
    case NodeKind::Variable: {
      const auto found = variables.find(node.symbol);
      if (found == variables.end()) {
        throw std::invalid_argument("equation variable '" + node.symbol + "' has no bound value");
      }
      return found->second;
    }
    case NodeKind::Add:
      return argument(0) + argument(1);
    case NodeKind::Subtract:
      return argument(0) - argument(1);
    case NodeKind::Multiply:
      return argument(0) * argument(1);
    case NodeKind::Divide: {
      const double divisor = argument(1);
      if (std::abs(divisor) <= std::numeric_limits<double>::epsilon()) {
        throw std::domain_error("division by zero while evaluating equation");
      }
      return argument(0) / divisor;
    }
    case NodeKind::Power:
      return std::pow(argument(0), argument(1));
    case NodeKind::Negate:
      return -argument(0);
    case NodeKind::Function:
      break;
  }
  const double first = argument(0);
  if (node.symbol == "sin") return std::sin(first);
  if (node.symbol == "cos") return std::cos(first);
  if (node.symbol == "tan") return std::tan(first);
  if (node.symbol == "exp") return std::exp(first);
  if (node.symbol == "sqrt") return std::sqrt(first);
  if (node.symbol == "abs") return std::abs(first);
  if (node.symbol == "log") return std::log(first);
  if (node.symbol == "min") return std::min(first, argument(1));
  if (node.symbol == "max") return std::max(first, argument(1));
  if (node.symbol == "clamp") return std::clamp(first, argument(1), argument(2));
  throw std::invalid_argument("unsupported equation function '" + node.symbol + "'");
}

void collectVariableNames(const EquationNode& node, std::set<std::string>& names) {
  if (node.kind == NodeKind::Variable) names.insert(node.symbol);
  for (const EquationNode& child : node.children) collectVariableNames(child, names);
}

constexpr double kExpOverflowThreshold = 709.0;

Interval entireRange() {
  return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), false};
}

Interval normalized(Interval interval) {
  if (interval.minimum > interval.maximum) std::swap(interval.minimum, interval.maximum);
  interval.finite =
      interval.finite && std::isfinite(interval.minimum) && std::isfinite(interval.maximum);
  return interval;
}

void diagnostic(
    RangeAnalysis& result,
    AnalysisSeverity severity,
    const std::string& path,
    const std::string& message) {
  result.diagnostics.push_back({severity, path, message});
}

Interval analyzeNode(
    const EquationNode& node,
    const std::map<std::string, Interval>& bindings,
    const std::string& path,
    RangeAnalysis& result) {
  const auto child = [&](size_t index) {
    return analyzeNode(
        node.children.at(index),
        bindings,
        path + "." + std::to_string(index),
        result);
  };
  switch (node.kind) {
    case NodeKind::Constant:
      return {node.value, node.value, std::isfinite(node.value)};
    case NodeKind::Variable: {
      const auto found = bindings.find(node.symbol);
      if (found == bindings.end()) {
        diagnostic(
            result,
            AnalysisSeverity::Error,
            path,
            "missing interval for variable '" + node.symbol + "'");
        return entireRange();
      }
      return normalized(found->second);
    }
    case NodeKind::Negate: {
      const Interval value = child(0);
      return {-value.maximum, -value.minimum, value.finite};
    }
    case NodeKind::Add:
    case NodeKind::Subtract: {
      const Interval left = child(0);
      const Interval right = child(1);
      if (node.kind == NodeKind::Add) {
        return normalized(
            {left.minimum + right.minimum,
             left.maximum + right.maximum,
             left.finite && right.finite});
      }
      return normalized(
          {left.minimum - right.maximum,
           left.maximum - right.minimum,
           left.finite && right.finite});
    }
    case NodeKind::Multiply: {
      const Interval left = child(0);
      const Interval right = child(1);
      const std::array<double, 4> products{
          left.minimum * right.minimum,
          left.minimum * right.maximum,
          left.maximum * right.minimum,
          left.maximum * right.maximum};
      return normalized(
          {*std::min_element(products.begin(), products.end()),
           *std::max_element(products.begin(), products.end()),
           left.finite && right.finite});
    }
    case NodeKind::Divide: {
      const Interval numerator = child(0);
      const Interval denominator = child(1);
      if (denominator.contains(0.0)) {
        diagnostic(result, AnalysisSeverity::Error, path, "denominator interval contains zero");
        return entireRange();
      }
      const std::array<double, 4> quotients{
          numerator.minimum / denominator.minimum,
          numerator.minimum / denominator.maximum,
          numerator.maximum / denominator.minimum,
          numerator.maximum / denominator.maximum};
      return normalized(
          {*std::min_element(quotients.begin(), quotients.end()),
           *std::max_element(quotients.begin(), quotients.end()),
           numerator.finite && denominator.finite});
    }
    case NodeKind::Power: {
      const Interval base = child(0);
      const Interval exponent = child(1);
      if (base.minimum < 0.0 && (exponent.minimum != exponent.maximum ||
                                 std::floor(exponent.minimum) != exponent.minimum)) {
        diagnostic(
            result,
            AnalysisSeverity::Error,
            path,
            "negative base may be raised to a non-integer power");
        return entireRange();
      }
      if (base.contains(0.0) && exponent.minimum < 0.0) {
        diagnostic(
            result,
            AnalysisSeverity::Error,
            path,
            "zero base may be raised to a negative power");
        return entireRange();
      }
      const std::array<double, 4> values{
          std::pow(base.minimum, exponent.minimum),
          std::pow(base.minimum, exponent.maximum),
          std::pow(base.maximum, exponent.minimum),
          std::pow(base.maximum, exponent.maximum)};
      if (std::any_of(values.begin(), values.end(), [](double value) {
            return !std::isfinite(value);
          })) {
        diagnostic(result, AnalysisSeverity::Error, path, "power may produce a non-finite value");
        return entireRange();
      }
      return {
          *std::min_element(values.begin(), values.end()),
          *std::max_element(values.begin(), values.end()),
          true};
    }
    case NodeKind::Function:
      break;
  }

  const Interval first = child(0);
  if (node.symbol == "sin" || node.symbol == "cos") return {-1.0, 1.0, first.finite};
  if (node.symbol == "tan") {
    const double halfPi = std::numbers::pi * 0.5;
    const auto poleIndex = [](double value) {
      return std::floor((value - std::numbers::pi * 0.5) / std::numbers::pi);
    };
    if (!first.finite || poleIndex(first.minimum) != poleIndex(first.maximum) ||
        std::abs(std::cos(first.minimum)) < 1e-12 || std::abs(std::cos(first.maximum)) < 1e-12) {
      diagnostic(result, AnalysisSeverity::Error, path, "tangent interval crosses a pole");
      return entireRange();
    }
    (void)halfPi;
    return normalized({std::tan(first.minimum), std::tan(first.maximum), true});
  }
  if (node.symbol == "exp") {
    if (first.maximum > kExpOverflowThreshold) {
      diagnostic(
          result,
          AnalysisSeverity::Error,
          path,
          "exponential may overflow double precision");
    }
    return normalized(
        {std::exp(first.minimum),
         std::exp(std::min(first.maximum, kExpOverflowThreshold)),
         first.finite && first.maximum <= kExpOverflowThreshold});
  }
  if (node.symbol == "sqrt") {
    if (first.minimum < 0.0) {
      diagnostic(result, AnalysisSeverity::Error, path, "square root input may be negative");
    }
    return {
        std::sqrt(std::max(0.0, first.minimum)),
        std::sqrt(std::max(0.0, first.maximum)),
        first.finite && first.minimum >= 0.0};
  }
  if (node.symbol == "log") {
    if (first.minimum <= 0.0) {
      diagnostic(result, AnalysisSeverity::Error, path, "logarithm input may be non-positive");
      return entireRange();
    }
    return {std::log(first.minimum), std::log(first.maximum), first.finite};
  }
  if (node.symbol == "abs") {
    return first.contains(0.0)
               ? Interval{0.0, std::max(-first.minimum, first.maximum), first.finite}
               : normalized({std::abs(first.minimum), std::abs(first.maximum), first.finite});
  }
  if (node.symbol == "min" || node.symbol == "max") {
    const Interval second = child(1);
    return node.symbol == "min"
               ? Interval{std::min(first.minimum, second.minimum), std::min(first.maximum, second.maximum),
                          first.finite && second.finite}
               : Interval{std::max(first.minimum, second.minimum), std::max(first.maximum, second.maximum),
                          first.finite && second.finite};
  }
  if (node.symbol == "clamp") {
    const Interval low = child(1);
    const Interval high = child(2);
    if (low.maximum > high.minimum) {
      diagnostic(
          result,
          AnalysisSeverity::Warning,
          path,
          "clamp bounds overlap or may be reversed");
    }
    return normalized(
        {std::max(first.minimum, low.minimum),
         std::min(first.maximum, high.maximum),
         first.finite && low.finite && high.finite});
  }
  diagnostic(result, AnalysisSeverity::Error, path, "unsupported function '" + node.symbol + "'");
  return entireRange();
}

std::unordered_map<std::string, double> bindings(
    const EquationPreset& preset,
    const Sample& sample,
    const std::map<std::string, double>& overrides) {
  std::unordered_map<std::string, double> result{
      {"x", sample.x},
      {"y", sample.y},
      {"z", sample.z},
      {"t", sample.t}};
  for (const Parameter& parameter : preset.parameters)
    result.emplace(parameter.name, parameter.value);
  for (const auto& [name, value] : overrides) result[name] = value;
  return result;
}

}  // namespace

ScalarExpression::ScalarExpression(EquationNode root) : root_(std::move(root)) {}

double ScalarExpression::evaluate(const std::unordered_map<std::string, double>& variables) const {
  return evaluateNode(root_, variables);
}

ScalarExpression parseScalarExpression(const std::string& source) {
  return ScalarExpression{Parser{source}.parse()};
}

std::vector<std::string> variableNames(const ScalarExpression& expression) {
  std::set<std::string> names;
  collectVariableNames(expression.root(), names);
  return {names.begin(), names.end()};
}

bool RangeAnalysis::safe() const {
  return range.finite &&
         std::none_of(diagnostics.begin(), diagnostics.end(), [](const AnalysisDiagnostic& issue) {
           return issue.severity == AnalysisSeverity::Error;
         });
}

RangeAnalysis analyzeRange(
    const ScalarExpression& expression, const std::map<std::string, Interval>& bindings) {
  RangeAnalysis result{};
  result.range = analyzeNode(expression.root(), bindings, "root", result);
  return result;
}

std::vector<EquationPreset> builtInPresets() {
  return {
      {"wave-field",
       "Wave Field",
       "Traveling scalar wave for first visual and GPU-field validation.",
       {"amplitude * sin(wavenumber * x - angular_frequency * t)"},
       {"x", "t"},
       {{"amplitude", 1.0, "field", 0.0, 10.0},
        {"wavenumber", 2.0, "rad/m", 0.01, 50.0},
        {"angular_frequency", 3.0, "rad/s", 0.01, 50.0}}},
      {"gravity-potential",
       "Gravity Potential",
       "Softened Newtonian potential; the softening avoids a singularity at the origin.",
       {"-gravitational_parameter / sqrt(x*x + y*y + z*z + softening*softening)"},
       {"x", "y", "z"},
       {{"gravitational_parameter", 8.0, "m3/s2", 0.001, 1000.0},
        {"softening", 0.25, "m", 0.001, 10.0}}},
      {"quantum-wavepacket",
       "Quantum Wavepacket",
       "A moving Gaussian-envelope carrier wave for probability-density visualizations.",
       {"exp(-((x - velocity*t)*(x - velocity*t)) / (2*width*width)) * cos(wavenumber*x - "
        "angular_frequency*t)"},
       {"x", "t"},
       {{"velocity", 0.6, "m/s", -10.0, 10.0},
        {"width", 1.2, "m", 0.01, 20.0},
        {"wavenumber", 5.0, "rad/m", 0.01, 100.0},
        {"angular_frequency", 3.0, "rad/s", 0.01, 100.0}}},
      {"electromagnetic-pulse",
       "Electromagnetic Pulse",
       "Damped oscillating field used to validate high-dynamic-range transfer functions.",
       {"amplitude * exp(-decay*t) * sin(wavenumber*x - angular_frequency*t)"},
       {"x", "t"},
       {{"amplitude", 1.0, "field", 0.0, 10.0},
        {"decay", 0.3, "1/s", 0.0, 10.0},
        {"wavenumber", 4.0, "rad/m", 0.01, 100.0},
        {"angular_frequency", 5.0, "rad/s", 0.01, 100.0}}},
      {"reaction-diffusion-seed",
       "Reaction Diffusion Seed",
       "Deterministic activator seed used as an input field for a later ping-pong simulation pass.",
       {"seed_strength * exp(-(x*x + y*y) / (2*radius*radius))"},
       {"x", "y"},
       {{"seed_strength", 1.0, "concentration", 0.0, 1.0},
        {"radius", 0.75, "m", 0.01, 20.0},
        {"diffusion_a", 1.0, "m2/s", 0.0, 2.0},
        {"diffusion_b", 0.5, "m2/s", 0.0, 2.0},
        {"feed", 0.0367, "1/s", 0.0, 0.1},
        {"kill", 0.0649, "1/s", 0.0, 0.1}}},
      {"buoyant-smoke",
       "Buoyant Smoke",
       "2D incompressible smoke with pressure projection, buoyancy, and vorticity confinement.",
       {"exp(-(x*x + y*y))"},
       {"x", "y"},
       {{"buoyancy", 2.0, "m/s2", 0.0, 8.0},
        {"vorticity", 0.35, "m2/s", 0.0, 3.0},
        {"density_dissipation", 0.998, "ratio", 0.9, 1.0}}},
      {"nbody-orbits",
       "N-Body Orbits",
       "Deterministic softened Newtonian N-body system integrated with velocity Verlet.",
       {"-central_mass / sqrt(x*x + y*y + softening*softening)"},
       {"x", "y"},
       {{"central_mass", 100.0, "mass", 1.0, 500.0},
        {"orbiter_mass", 1.0, "mass", 0.01, 20.0},
        {"softening", 0.05, "m", 0.005, 1.0}}},
      {"schwarzschild-lensing",
       "Schwarzschild Lensing",
       "Reference-guided Schwarzschild lensing preview using an RK4 deflection lookup.",
       {"1 / sqrt(x*x + y*y + 0.01)"},
       {"x", "y"},
       {}},
  };
}

std::optional<EquationPreset> findPreset(const std::string& id) {
  const auto presets = builtInPresets();
  const auto found =
      std::find_if(presets.begin(), presets.end(), [&](const EquationPreset& preset) {
        return preset.id == id;
      });
  if (found == presets.end()) return std::nullopt;
  return *found;
}

EvaluationResult evaluatePreset(
    const EquationPreset& preset,
    const Sample& sample,
    const std::map<std::string, double>& parameterOverrides) {
  const auto variables = bindings(preset, sample, parameterOverrides);
  EvaluationResult result{};
  result.values.reserve(preset.expressions.size());
  for (const std::string& expression : preset.expressions) {
    result.values.push_back(parseScalarExpression(expression).evaluate(variables));
  }
  return result;
}

PresetRunSummary runPreset(const EquationPreset& preset, const PresetRunConfig& config) {
  if (config.frames == 0 || config.samplesPerFrame == 0 || config.timestepSeconds <= 0.0) {
    throw std::invalid_argument(
        "preset run configuration must contain positive frames, samples, and timestep");
  }
  PresetRunSummary summary{};
  summary.presetId = preset.id;
  summary.frames = config.frames;
  summary.samplesPerFrame = config.samplesPerFrame;
  summary.minimumValue = std::numeric_limits<double>::infinity();
  summary.maximumValue = -std::numeric_limits<double>::infinity();
  double sumAbsolute = 0.0;
  double sumSquared = 0.0;
  const double sampleDenominator =
      static_cast<double>(config.samplesPerFrame - 1u == 0 ? 1u : config.samplesPerFrame - 1u);
  for (uint32_t frame = 0; frame < config.frames; ++frame) {
    for (uint32_t index = 0; index < config.samplesPerFrame; ++index) {
      const double normalized = static_cast<double>(index) / sampleDenominator;
      const Sample sample{
          normalized * 8.0 - 4.0,
          std::sin(normalized * 2.0 * std::numbers::pi),
          0.5 * std::cos(normalized * 2.0 * std::numbers::pi),
          frame * config.timestepSeconds};
      for (const double value : evaluatePreset(preset, sample).values) {
        if (!std::isfinite(value))
          throw std::runtime_error("preset produced a non-finite value: " + preset.id);
        summary.minimumValue = std::min(summary.minimumValue, value);
        summary.maximumValue = std::max(summary.maximumValue, value);
        sumAbsolute += std::abs(value);
        sumSquared += value * value;
      }
    }
  }
  const double count =
      static_cast<double>(config.frames) * config.samplesPerFrame * preset.expressions.size();
  summary.meanAbsoluteValue = sumAbsolute / count;
  summary.energyProxy = sumSquared / count;
  return summary;
}

}  // namespace vulkax::equation
