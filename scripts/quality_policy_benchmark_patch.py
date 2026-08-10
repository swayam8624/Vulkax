from pathlib import Path

path = Path('tools/quality_benchmark.cpp')
text = path.read_text()

text = text.replace(
'''struct PolicySummary {
  std::vector<double> frameMilliseconds;
  std::vector<double> visualErrors;
  uint32_t changes = 0;
};
''',
'''struct PolicySummary {
  std::vector<double> frameMilliseconds;
  std::vector<double> numericalErrors;
  std::vector<double> visualErrors;
  uint32_t changes = 0;
};
''')

start = text.index('PolicySummary runPolicy(')
end = text.index('\n}\n\n}  // namespace', start) + 2
new_run = r'''PolicySummary runPolicy(
    const std::string& policyName,
    vulkax::research::QualityPolicy policy,
    const Options& options,
    const vulkax::equation::ScalarExpression& expression,
    const std::map<std::string, double>& parameters,
    std::ofstream& csv) {
  using Clock = std::chrono::steady_clock;
  vulkax::research::QualityBudget budget{
      options.targetMilliseconds, 0.05, 0.35, 1.0, 1, 8};
  budget.targetNumericalError = 0.005;
  vulkax::research::QualityController controller{budget, policy};
  controller.reset({1.0, 1, 1});
  PolicySummary summary{};
  for (uint32_t frame = 0; frame < options.frames; ++frame) {
    const auto state = controller.state();
    const bool fixed = policy == vulkax::research::QualityPolicy::Fixed;
    const double scale = fixed ? 1.0 : state.resolutionScale;
    const uint32_t width = std::max(
        8u, static_cast<uint32_t>(std::lround(options.width * scale)));
    const uint32_t height = std::max(
        8u, static_cast<uint32_t>(std::lround(options.height * scale)));
    const double timeSeconds = static_cast<double>(frame) / 60.0;
    const auto started = Clock::now();
    const auto field = evaluateField(expression, parameters, width, height, timeSeconds);
    const double frameMilliseconds =
        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    const auto reference =
        evaluateField(expression, parameters, options.width, options.height, timeSeconds);
    const double numericalMse =
        resampleMse(field, width, height, reference, options.width, options.height);
    // This CPU benchmark has no rendered perceptual image. RMS field error is
    // therefore recorded explicitly as a screen-space surrogate, never as a
    // perceptual metric.
    const double screenSpaceRms = std::sqrt(numericalMse);

    using vulkax::research::QualityMeasurements;
    using vulkax::research::QualityPolicy;
    QualityMeasurements measurements{};
    measurements.frameMilliseconds = frameMilliseconds;
    if (policy == QualityPolicy::NumericalOnly || policy == QualityPolicy::EquationAware)
      measurements.numericalError = numericalMse;
    if (policy == QualityPolicy::ScreenSpaceOnly || policy == QualityPolicy::EquationAware)
      measurements.visualError = screenSpaceRms;
    controller.update(measurements);

    csv << policyName << ',' << frame << ',' << std::setprecision(17)
        << frameMilliseconds << ',' << numericalMse << ',' << screenSpaceRms << ','
        << scale << ',' << state.samplesPerPixel << ',' << state.simulationSubsteps << ','
        << width << ',' << height << ',' << controller.frameTimeEwma() << ','
        << controller.numericalErrorEwma().value_or(
               std::numeric_limits<double>::quiet_NaN()) << ','
        << controller.visualErrorEwma().value_or(
               std::numeric_limits<double>::quiet_NaN()) << '\n';
    summary.frameMilliseconds.push_back(frameMilliseconds);
    summary.numericalErrors.push_back(numericalMse);
    summary.visualErrors.push_back(screenSpaceRms);
  }
  summary.changes = controller.changeCount();
  return summary;
}'''
text = text[:start] + new_run + text[end:]

old_main = '''    csv << "policy,frame,frame_ms,visual_mse,resolution_scale,samples_per_pixel,width,height,frame_ewma_ms,visual_ewma\\n";
    const auto fixed = runPolicy("fixed", false, options, expression, parameters, csv);
    const auto adaptive = runPolicy("adaptive", true, options, expression, parameters, csv);
    std::ofstream summary(options.output / "quality_summary.json");
    summary << "{\\n"
            << "  \\"measurement_class\\": \\"cpu_analytical_preview_quality_benchmark\\",\\n"
            << "  \\"frames\\": " << options.frames << ",\\n"
            << "  \\"target_frame_ms\\": " << options.targetMilliseconds << ",\\n"
            << "  \\"fixed\\": {\\"p50_frame_ms\\": " << percentile(fixed.frameMilliseconds, 0.50)
            << ", \\"p95_frame_ms\\": " << percentile(fixed.frameMilliseconds, 0.95)
            << ", \\"mean_visual_mse\\": " << std::accumulate(fixed.visualErrors.begin(), fixed.visualErrors.end(), 0.0) / fixed.visualErrors.size()
            << ", \\"changes\\": 0},\\n"
            << "  \\"adaptive\\": {\\"p50_frame_ms\\": " << percentile(adaptive.frameMilliseconds, 0.50)
            << ", \\"p95_frame_ms\\": " << percentile(adaptive.frameMilliseconds, 0.95)
            << ", \\"mean_visual_mse\\": " << std::accumulate(adaptive.visualErrors.begin(), adaptive.visualErrors.end(), 0.0) / adaptive.visualErrors.size()
            << ", \\"changes\\": " << adaptive.changes << "}\\n}\\n";
'''
# The exact literal is easier to replace by locating the CSV header through summary close.
header_start = text.index('    csv << "policy,frame,frame_ms,')
summary_end = text.index('    std::cout << "Wrote "', header_start)
new_main = r'''    csv << "policy,frame,frame_ms,numerical_mse,screen_space_rms,resolution_scale,"
           "samples_per_pixel,simulation_substeps,width,height,frame_ewma_ms,"
           "numerical_ewma,visual_ewma\n";
    using vulkax::research::QualityPolicy;
    const auto fixed = runPolicy(
        "fixed", QualityPolicy::Fixed, options, expression, parameters, csv);
    const auto screen = runPolicy(
        "screen_space_only", QualityPolicy::ScreenSpaceOnly,
        options, expression, parameters, csv);
    const auto numerical = runPolicy(
        "numerical_only", QualityPolicy::NumericalOnly,
        options, expression, parameters, csv);
    const auto equationAware = runPolicy(
        "equation_aware", QualityPolicy::EquationAware,
        options, expression, parameters, csv);

    const auto mean = [](const std::vector<double>& values) {
      return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    };
    const auto writePolicy = [&](std::ofstream& out, const char* name,
                                 const PolicySummary& value, bool trailingComma) {
      out << "  \"" << name << "\": {"
          << "\"p50_frame_ms\": " << percentile(value.frameMilliseconds, 0.50)
          << ", \"p95_frame_ms\": " << percentile(value.frameMilliseconds, 0.95)
          << ", \"mean_numerical_mse\": " << mean(value.numericalErrors)
          << ", \"mean_screen_space_rms\": " << mean(value.visualErrors)
          << ", \"changes\": " << value.changes << "}"
          << (trailingComma ? ",\n" : "\n");
    };
    std::ofstream summary(options.output / "quality_summary.json");
    summary << "{\n"
            << "  \"measurement_class\": "
               "\"cpu_analytical_preview_policy_comparison\",\n"
            << "  \"visual_metric_note\": "
               "\"screen_space_rms is a field-space surrogate, not perceptual image error\",\n"
            << "  \"frames\": " << options.frames << ",\n"
            << "  \"target_frame_ms\": " << options.targetMilliseconds << ",\n";
    writePolicy(summary, "fixed", fixed, true);
    writePolicy(summary, "screen_space_only", screen, true);
    writePolicy(summary, "numerical_only", numerical, true);
    writePolicy(summary, "equation_aware", equationAware, false);
    summary << "}\n";
'''
text = text[:header_start] + new_main + text[summary_end:]
path.write_text(text)
Path('scripts/quality_policy_benchmark_patch.py').unlink()
