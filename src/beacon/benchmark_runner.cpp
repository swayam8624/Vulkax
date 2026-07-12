#include "beacon/benchmark_runner.hpp"

#include "beacon/cluster_experiment.hpp"
#include "beacon/progress.hpp"
#include "beacon/scene_generator.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>

namespace lve::beacon {
namespace {

double percentile(std::vector<double> values, double p) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  double index = (values.size() - 1) * p;
  size_t lo = static_cast<size_t>(index);
  size_t hi = std::min(lo + 1, values.size() - 1);
  double t = index - static_cast<double>(lo);
  return values[lo] * (1.0 - t) + values[hi] * t;
}

void writeManifest(const BenchmarkConfig& config, const std::filesystem::path& path) {
  std::ofstream out{path};
  out << "{\n";
  out << "  \"technique\": \"" << toString(config.technique) << "\",\n";
  out << "  \"scene\": \"" << toString(config.scene) << "\",\n";
  out << "  \"lightDistribution\": \"" << toString(config.lightDistribution) << "\",\n";
  out << "  \"objects\": " << config.objectCount << ",\n";
  out << "  \"lights\": " << config.lightCount << ",\n";
  out << "  \"frames\": " << config.frameCount << ",\n";
  out << "  \"warmupFrames\": " << config.warmupFrames << ",\n";
  out << "  \"seed\": " << config.randomSeed << ",\n";
  out << "  \"width\": " << config.width << ",\n";
  out << "  \"height\": " << config.height << ",\n";
  out << "  \"measurementGroup\": \"model_predictions\",\n";
  out << "  \"note\": \"Headless analytical model. These are model predictions, not Vulkan GPU timings or rendered image errors.\"\n";
  out << "}\n";
}

void writeClusterRows(
    std::ofstream& out,
    uint32_t frame,
    const std::vector<ClusterExperimentRecord>& clusters) {
  for (size_t i = 0; i < clusters.size(); ++i) {
    const auto& c = clusters[i];
    out << frame << "," << i << "," << c.depth << "," << c.candidateLights << ","
        << c.retainedLights << "," << c.prunedLights << "," << c.listBytes << ","
        << c.shadedPixels << "," << c.predictedCost << "," << c.predictedErrorBound << ","
        << c.exactReference << "," << c.approximateReference << ","
        << c.modeledSquaredBoundDifference << "," << toString(c.encoding) << "\n";
  }
}

}  // namespace

int runResearchBenchmark(const BenchmarkConfig& inputConfig) {
  BenchmarkConfig config = inputConfig;
  if (config.frameCount == 0) config.frameCount = 300;
  std::filesystem::create_directories(config.outputDirectory);

  BudgetController controller{
      config.clusterBuildBudgetMs,
      config.lightingBudgetMs,
      config.imageErrorBudget};

  if (config.verbose) {
    std::cout << "BEACON benchmark\n"
              << "  technique: " << toString(config.technique) << "\n"
              << "  scene: " << toString(config.scene) << "\n"
              << "  lights: " << config.lightCount << "\n"
              << "  objects: " << config.objectCount << "\n"
              << "  output: " << std::filesystem::absolute(config.outputDirectory) << std::endl;
  }

  std::ofstream frames{config.outputDirectory / "frames.csv"};
  frames << "frame,activeClusters,activeLights,visibleObjects,avgLightsPerCluster,maxLightsPerCluster,"
            "lightListBytes,evaluatedLightSamples,prunedLightSamples,predictedErrorBound,"
            "modeledScalarBoundDifference,modeledMaxBoundDifference,derivedScalarScore,"
            "clusterBuildModeledMs,lightingModeledMs,totalModeledCostMs,splitCount,clusterChurn,"
            "measurementGroup,timingSource\n";

  std::ofstream clusters{config.outputDirectory / "clusters.csv"};
  clusters << "frame,cluster,depth,candidateLights,retainedLights,prunedLights,listBytes,"
              "shadedPixels,predictedCost,predictedErrorBound,exactReference,approximateReference,"
              "modeledSquaredBoundDifference,encoding\n";

  std::vector<double> totalModeledCostMs;
  std::vector<double> lightingMs;
  RenderStats lastStats{};
  uint32_t totalFrames = config.warmupFrames + config.frameCount;
  for (uint32_t frame = 0; frame < config.warmupFrames + config.frameCount; ++frame) {
    printProgressBar("BEACON", frame + 1, totalFrames, config.verbose);
    BenchmarkScene scene = generateBenchmarkScene(config, frame);
    ClusterExperimentResult result = runClusterExperiment(config, scene, controller);
    controller.update(result.stats);
    if (frame < config.warmupFrames) continue;

    uint32_t outputFrame = frame - config.warmupFrames;
    const auto& s = result.stats;
    frames << outputFrame << "," << s.activeClusters << "," << s.activeLights << ","
           << s.visibleObjects << "," << s.averageLightsPerCluster << ","
           << s.maximumLightsPerCluster << "," << s.lightListBytes << ","
           << s.evaluatedLightSamples << "," << s.prunedLightSamples << ","
           << s.predictedErrorBound << "," << s.modeledScalarBoundDifference << ","
           << s.modeledMaxBoundDifference << "," << s.derivedScalarScore << ","
           << s.gpu.clusterBuildMs << ","
           << s.gpu.lightingPassMs << "," << s.gpu.totalFrameMs << "," << s.splitCount << ","
           << s.clusterChurn << ",model_predictions,modeled-headless\n";
    if (outputFrame < 8) {
      writeClusterRows(clusters, outputFrame, result.clusters);
    }
    totalModeledCostMs.push_back(s.gpu.totalFrameMs);
    lightingMs.push_back(s.gpu.lightingPassMs);
    lastStats = s;
  }
  finishProgressBar(config.verbose);

  writeManifest(config, config.outputDirectory / "manifest.json");

  std::ofstream summary{config.outputDirectory / "summary.json"};
  summary << "{\n";
  summary << "  \"technique\": \"" << toString(config.technique) << "\",\n";
  summary << "  \"frames\": " << config.frameCount << ",\n";
  summary << "  \"activeClusters\": " << lastStats.activeClusters << ",\n";
  summary << "  \"averageLightsPerCluster\": " << lastStats.averageLightsPerCluster << ",\n";
  summary << "  \"maximumLightsPerCluster\": " << lastStats.maximumLightsPerCluster << ",\n";
  summary << "  \"lightListBytes\": " << lastStats.lightListBytes << ",\n";
  summary << "  \"evaluatedLightSamples\": " << lastStats.evaluatedLightSamples << ",\n";
  summary << "  \"prunedLightSamples\": " << lastStats.prunedLightSamples << ",\n";
  summary << "  \"predictedErrorBound\": " << lastStats.predictedErrorBound << ",\n";
  summary << "  \"modeledScalarBoundDifference\": " << lastStats.modeledScalarBoundDifference << ",\n";
  summary << "  \"modeledMaxBoundDifference\": " << lastStats.modeledMaxBoundDifference << ",\n";
  summary << "  \"derivedScalarScore\": " << lastStats.derivedScalarScore << ",\n";
  summary << "  \"measurementGroup\": \"model_predictions\",\n";
  summary << "  \"totalModeledCostMsP50\": " << percentile(totalModeledCostMs, 0.50) << ",\n";
  summary << "  \"totalModeledCostMsP95\": " << percentile(totalModeledCostMs, 0.95) << ",\n";
  summary << "  \"totalModeledCostMsP99\": " << percentile(totalModeledCostMs, 0.99) << ",\n";
  summary << "  \"lightingModeledMsP50\": " << percentile(lightingMs, 0.50) << ",\n";
  summary << "  \"lightingModeledMsP95\": " << percentile(lightingMs, 0.95) << ",\n";
  summary << "  \"clusterChurn\": " << lastStats.clusterChurn << "\n";
  summary << "}\n";

  std::cout << "BEACON research benchmark wrote results to "
            << std::filesystem::absolute(config.outputDirectory) << std::endl;
  return 0;
}

}  // namespace lve::beacon
