#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "vulkax/equation/equation.hpp"

namespace {
bool close(double left, double right, double tolerance = 1e-10) {
  return std::abs(left - right) <= tolerance;
}
}  // namespace

int main() {
  using namespace vulkax::equation;
  assert(close(parseScalarExpression("2 + 3 * 4").evaluate({}), 14.0));
  assert(close(parseScalarExpression("2^3^2").evaluate({}), 512.0));
  assert(close(parseScalarExpression("-2^2").evaluate({}), -4.0));
  assert(close(parseScalarExpression("2^-3").evaluate({}), 0.125));
  assert(close(parseScalarExpression("sin(pi / 2) + max(1, 3)").evaluate({}), 4.0));
  assert(close(parseScalarExpression("-x + 2").evaluate({{"x", 5.0}}), -3.0));
  const auto symbols = variableNames(parseScalarExpression("amplitude * sin(k * x - omega * t)"));
  assert((symbols == std::vector<std::string>{"amplitude", "k", "omega", "t", "x"}));

  const auto bounded = analyzeRange(
      parseScalarExpression("amplitude * sin(k*x)"),
      {{"amplitude", {0.5, 2.0}}, {"k", {1.0, 4.0}}, {"x", {-3.0, 3.0}}});
  assert(bounded.safe());
  assert(bounded.range.minimum <= -2.0 && bounded.range.maximum >= 2.0);
  const auto singular = analyzeRange(
      parseScalarExpression("1 / (x - critical)"),
      {{"x", {-1.0, 1.0}}, {"critical", {0.0, 0.0}}});
  assert(!singular.safe());
  assert(!singular.diagnostics.empty());
  const auto invalidRoot = analyzeRange(parseScalarExpression("sqrt(x)"), {{"x", {-1.0, 4.0}}});
  assert(!invalidRoot.safe());
  const auto safeRoot = analyzeRange(parseScalarExpression("sqrt(x)"), {{"x", {0.0, 4.0}}});
  assert(safeRoot.safe());
  assert(close(safeRoot.range.maximum, 2.0));

  bool invalidExpressionRejected = false;
  try {
    (void)parseScalarExpression("sin(");
  } catch (const std::invalid_argument&) {
    invalidExpressionRejected = true;
  }
  assert(invalidExpressionRejected);

  const auto wave = findPreset("wave-field");
  assert(wave.has_value());
  const auto waveValue = evaluatePreset(*wave, {0.0, 0.0, 0.0, 0.0});
  assert(waveValue.values.size() == 1);
  assert(close(waveValue.values.front(), 0.0));

  const auto gravity = findPreset("gravity-potential");
  assert(gravity.has_value());
  const auto nearValue = evaluatePreset(*gravity, {0.0, 0.0, 0.0, 0.0}).values.front();
  const auto farValue = evaluatePreset(*gravity, {5.0, 0.0, 0.0, 0.0}).values.front();
  assert(nearValue < farValue);

  const auto summary = runPreset(*wave, {12, 32, 1.0 / 60.0});
  assert(summary.minimumValue < -0.9);
  assert(summary.maximumValue > 0.9);
  assert(summary.energyProxy > 0.0);
  std::cout << "Vulkax equation core tests passed\n";
  return 0;
}
