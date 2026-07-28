#include "vulkax/equation/equation.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
bool close(double left, double right, double tolerance = 1e-10) {
  return std::abs(left - right) <= tolerance;
}
}

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
