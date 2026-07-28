#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vulkax::equation {

enum class NodeKind {
  Constant,
  Variable,
  Add,
  Subtract,
  Multiply,
  Divide,
  Power,
  Negate,
  Function,
};

struct EquationNode {
  NodeKind kind = NodeKind::Constant;
  double value = 0.0;
  std::string symbol{};
  std::vector<EquationNode> children{};
};

struct Parameter {
  std::string name;
  double value = 0.0;
  std::string units;
  double minimum = 0.0;
  double maximum = 0.0;
};

struct EquationPreset {
  std::string id;
  std::string displayName;
  std::string description;
  std::vector<std::string> expressions;
  std::vector<std::string> variables;
  std::vector<Parameter> parameters;
};

struct Sample {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double t = 0.0;
};

struct EvaluationResult {
  std::vector<double> values;
};

struct PresetRunConfig {
  uint32_t frames = 120;
  uint32_t samplesPerFrame = 256;
  double timestepSeconds = 1.0 / 60.0;
};

struct PresetRunSummary {
  std::string presetId;
  uint32_t frames = 0;
  uint32_t samplesPerFrame = 0;
  double minimumValue = 0.0;
  double maximumValue = 0.0;
  double meanAbsoluteValue = 0.0;
  double energyProxy = 0.0;
};

class ScalarExpression {
 public:
  explicit ScalarExpression(EquationNode root);

  [[nodiscard]] double evaluate(
      const std::unordered_map<std::string, double>& variables) const;
  [[nodiscard]] const EquationNode& root() const { return root_; }

 private:
  EquationNode root_;
};

[[nodiscard]] ScalarExpression parseScalarExpression(const std::string& source);
// Returns every variable symbol referenced by an expression, once and in stable
// lexical order. Function names and constants are not returned.
[[nodiscard]] std::vector<std::string> variableNames(const ScalarExpression& expression);

[[nodiscard]] std::vector<EquationPreset> builtInPresets();
[[nodiscard]] std::optional<EquationPreset> findPreset(const std::string& id);
[[nodiscard]] EvaluationResult evaluatePreset(
    const EquationPreset& preset,
    const Sample& sample,
    const std::map<std::string, double>& parameterOverrides = {});
[[nodiscard]] PresetRunSummary runPreset(
    const EquationPreset& preset,
    const PresetRunConfig& config);

}  // namespace vulkax::equation
