#include "first_app.hpp"

#include "keyboard_movement_controller.hpp"
#include "lve_buffer.hpp"
#include "lve_camera.hpp"
#include "beacon/progress.hpp"
#include "beacon/gpu_profiler.hpp"
#include "beacon/offscreen_comparison.hpp"
#include "beacon/adaptive_vulkan_builder.hpp"
#include "systems/clustered_lighting_system.hpp"
#include "systems/point_light_system.hpp"
#include "systems/simple_render_system.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// std
#include <array>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace lve {
namespace {

uint64_t fnv1aFile(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary};
  uint64_t hash = 1469598103934665603ull;
  char byte = 0;
  while (input.get(byte)) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string currentGitCommit() {
  std::ifstream head{std::filesystem::path{ENGINE_DIR} / ".git/HEAD"};
  std::string value;
  std::getline(head, value);
  constexpr const char* prefix = "ref: ";
  if (value.rfind(prefix, 0) == 0) {
    std::ifstream ref{std::filesystem::path{ENGINE_DIR} / ".git" / value.substr(5)};
    std::getline(ref, value);
  }
  return value.empty() ? "unknown" : value;
}

bool usesSsboLightBuffer(beacon::RenderTechnique technique) {
  return technique == beacon::RenderTechnique::SsboForward ||
         technique == beacon::RenderTechnique::InstancedForward ||
         technique == beacon::RenderTechnique::GpuClusteredFixed ||
         technique == beacon::RenderTechnique::AdaptiveClusteredExact ||
         technique == beacon::RenderTechnique::AdaptiveClusteredBounded ||
         technique == beacon::RenderTechnique::BeaconFull;
}

bool usesObjectInstanceBuffer(beacon::RenderTechnique technique) {
  return technique == beacon::RenderTechnique::InstancedForward;
}

bool usesGpuClusteredLighting(beacon::RenderTechnique technique) {
  return technique == beacon::RenderTechnique::GpuClusteredFixed;
}

bool usesAdaptiveClusteredLighting(beacon::RenderTechnique technique) {
  return technique == beacon::RenderTechnique::AdaptiveClusteredExact ||
         technique == beacon::RenderTechnique::AdaptiveClusteredBounded ||
         technique == beacon::RenderTechnique::BeaconFull;
}

bool usesClusteredLighting(beacon::RenderTechnique technique) {
  return usesGpuClusteredLighting(technique) || usesAdaptiveClusteredLighting(technique);
}

glm::mat4 mat4FromMat3(const glm::mat3& value) {
  return glm::mat4{
      glm::vec4{value[0], 0.f},
      glm::vec4{value[1], 0.f},
      glm::vec4{value[2], 0.f},
      glm::vec4{0.f, 0.f, 0.f, 1.f}};
}

ClusterBuildPushConstants makeClusterBuildConfig(const LveGameObject::Map& gameObjects) {
  glm::vec3 minBounds{std::numeric_limits<float>::max()};
  glm::vec3 maxBounds{std::numeric_limits<float>::lowest()};
  bool found = false;
  for (const auto& kv : gameObjects) {
    const auto& obj = kv.second;
    glm::vec3 radius{0.5f};
    if (obj.pointLight != nullptr) {
      radius = glm::vec3{obj.pointLight->influenceRadius};
    } else if (obj.model != nullptr) {
      radius = glm::max(glm::abs(obj.transform.scale), glm::vec3{0.5f});
    } else {
      continue;
    }
    minBounds = glm::min(minBounds, obj.transform.translation - radius);
    maxBounds = glm::max(maxBounds, obj.transform.translation + radius);
    found = true;
  }
  if (!found) {
    minBounds = {-8.f, -8.f, -8.f};
    maxBounds = {8.f, 8.f, 8.f};
  }
  glm::vec3 padding{1.f};
  minBounds -= padding;
  maxBounds += padding;

  ClusterBuildPushConstants config{};
  config.gridSize = {16u, 9u, 24u, 0u};
  config.clusterCount = config.gridSize.x * config.gridSize.y * config.gridSize.z;
  config.maxLightsPerCluster = 128u;
  config.lightIndexCapacity = config.clusterCount * config.maxLightsPerCluster;
  config.worldMin = glm::vec4{minBounds, 0.f};
  config.worldMax = glm::vec4{maxBounds, 0.f};
  return config;
}

ClusterConfigData makeClusterRuntimeConfig(const ClusterBuildPushConstants& buildConfig) {
  ClusterConfigData runtime{};
  runtime.worldMin = buildConfig.worldMin;
  runtime.worldMax = buildConfig.worldMax;
  runtime.gridSize = buildConfig.gridSize;
  runtime.clusterCount = buildConfig.clusterCount;
  runtime.maxLightsPerCluster = buildConfig.maxLightsPerCluster;
  runtime.lightIndexCapacity = buildConfig.lightIndexCapacity;
  runtime.flags = 0;
  return runtime;
}

}  // namespace

FirstApp::FirstApp(beacon::BenchmarkConfig appConfig)
    : config{std::move(appConfig)},
      lveWindow{
          static_cast<int>(config.width),
          static_cast<int>(config.height),
          "BEACON Little Vulkan Engine"} {
  globalPool =
      LveDescriptorPool::Builder(lveDevice)
          .setMaxSets(LveSwapChain::MAX_FRAMES_IN_FLIGHT)
          .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, LveSwapChain::MAX_FRAMES_IN_FLIGHT * 2)
          .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, LveSwapChain::MAX_FRAMES_IN_FLIGHT * 5)
          .build();
  loadGameObjects();
}

FirstApp::~FirstApp() {}

void FirstApp::run() {
  std::vector<std::unique_ptr<LveBuffer>> uboBuffers(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  std::vector<std::unique_ptr<LveBuffer>> lightBuffers(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  std::vector<std::unique_ptr<LveBuffer>> objectBuffers(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  std::vector<std::unique_ptr<LveBuffer>> clusterConfigBuffers(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  std::vector<std::unique_ptr<LveBuffer>> clusterHeaderBuffers(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  std::vector<std::unique_ptr<LveBuffer>> clusterLightIndexBuffers(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  std::vector<std::unique_ptr<LveBuffer>> adaptiveNodeBuffers(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  ClusterBuildPushConstants clusterBuildConfig = makeClusterBuildConfig(gameObjects);
  uint32_t lightBufferCapacity = std::max<uint32_t>({1, config.lightCount, MAX_LIGHTS});
  uint32_t objectBufferCapacity = std::max<uint32_t>(1, static_cast<uint32_t>(gameObjects.size()));
  for (int i = 0; i < uboBuffers.size(); i++) {
    uboBuffers[i] = std::make_unique<LveBuffer>(
        lveDevice,
        sizeof(GlobalUbo),
        1,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    uboBuffers[i]->map();

    lightBuffers[i] = std::make_unique<LveBuffer>(
        lveDevice,
        sizeof(SsboPointLight),
        lightBufferCapacity,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    lightBuffers[i]->map();

    objectBuffers[i] = std::make_unique<LveBuffer>(
        lveDevice,
        sizeof(SsboObjectData),
        objectBufferCapacity,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    objectBuffers[i]->map();

    clusterConfigBuffers[i] = std::make_unique<LveBuffer>(
        lveDevice,
        sizeof(ClusterConfigData),
        1,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    clusterConfigBuffers[i]->map();

    uint32_t adaptiveSlots = 16u * 9u * 8u;
    uint32_t adaptiveStride = std::max(128u, (lightBufferCapacity + 31u) / 32u);
    uint32_t headerCapacity = std::max(clusterBuildConfig.clusterCount, adaptiveSlots);
    uint32_t indexCapacity = std::max(clusterBuildConfig.lightIndexCapacity, adaptiveSlots * adaptiveStride);
    clusterHeaderBuffers[i] = std::make_unique<LveBuffer>(
        lveDevice,
        sizeof(ClusterHeaderData),
        headerCapacity,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    clusterHeaderBuffers[i]->map();

    clusterLightIndexBuffers[i] = std::make_unique<LveBuffer>(
        lveDevice,
        sizeof(uint32_t),
        indexCapacity,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    clusterLightIndexBuffers[i]->map();

    adaptiveNodeBuffers[i] = std::make_unique<LveBuffer>(
        lveDevice,
        sizeof(AdaptiveClusterNodeData),
        headerCapacity,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    adaptiveNodeBuffers[i]->map();
  }

  auto globalSetLayout =
      LveDescriptorSetLayout::Builder(lveDevice)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS)
          .addBinding(
              1,
              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
          .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
          .addBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(
              4,
              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
          .addBinding(
              5,
              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
          .addBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  std::vector<VkDescriptorSet> globalDescriptorSets(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  for (int i = 0; i < globalDescriptorSets.size(); i++) {
    auto bufferInfo = uboBuffers[i]->descriptorInfo();
    auto lightBufferInfo = lightBuffers[i]->descriptorInfo();
    auto objectBufferInfo = objectBuffers[i]->descriptorInfo();
    auto clusterConfigInfo = clusterConfigBuffers[i]->descriptorInfo();
    auto clusterHeaderInfo = clusterHeaderBuffers[i]->descriptorInfo();
    auto clusterLightIndexInfo = clusterLightIndexBuffers[i]->descriptorInfo();
    auto adaptiveNodeInfo = adaptiveNodeBuffers[i]->descriptorInfo();
    LveDescriptorWriter(*globalSetLayout, *globalPool)
        .writeBuffer(0, &bufferInfo)
        .writeBuffer(1, &lightBufferInfo)
        .writeBuffer(2, &objectBufferInfo)
        .writeBuffer(3, &clusterConfigInfo)
        .writeBuffer(4, &clusterHeaderInfo)
        .writeBuffer(5, &clusterLightIndexInfo)
        .writeBuffer(6, &adaptiveNodeInfo)
        .build(globalDescriptorSets[i]);
  }

  SimpleRenderSystem simpleRenderSystem{
      lveDevice,
      lveRenderer.getSwapChainRenderPass(),
      globalSetLayout->getDescriptorSetLayout(),
      config.technique};
  std::unique_ptr<beacon::OffscreenComparison> offscreenComparison;
  std::unique_ptr<SimpleRenderSystem> offscreenReferenceSystem;
  std::unique_ptr<SimpleRenderSystem> offscreenClusteredSystem;
  if (usesClusteredLighting(config.technique) && config.captureReference) {
    offscreenComparison =
        std::make_unique<beacon::OffscreenComparison>(lveDevice, config.width, config.height);
    offscreenReferenceSystem = std::make_unique<SimpleRenderSystem>(
        lveDevice,
        offscreenComparison->getRenderPass(),
        globalSetLayout->getDescriptorSetLayout(),
        beacon::RenderTechnique::SsboForward);
    offscreenClusteredSystem = std::make_unique<SimpleRenderSystem>(
        lveDevice,
        offscreenComparison->getRenderPass(),
        globalSetLayout->getDescriptorSetLayout(),
        config.technique);
  }
  PointLightSystem pointLightSystem{
      lveDevice,
      lveRenderer.getSwapChainRenderPass(),
      globalSetLayout->getDescriptorSetLayout()};
  std::unique_ptr<ClusteredLightingSystem> clusteredLightingSystem;
  if (usesGpuClusteredLighting(config.technique)) {
    clusteredLightingSystem =
        std::make_unique<ClusteredLightingSystem>(lveDevice, globalSetLayout->getDescriptorSetLayout());
  }
  beacon::GpuProfiler gpuProfiler{lveDevice};
  beacon::BudgetController budgetController{
      config.clusterBuildBudgetMs, config.lightingBudgetMs, config.imageErrorBudget};
  beacon::AdaptiveTemporalState adaptiveTemporalState{};
  beacon::RenderStats adaptiveStats{};
  LveCamera camera{};

  auto viewerObject = LveGameObject::createGameObject();
  viewerObject.transform.translation.z = -2.5f;
  KeyboardMovementController cameraController{};

  auto currentTime = std::chrono::high_resolution_clock::now();
  uint32_t submittedFrames = 0;
  uint32_t measuredFrames = 0;
  uint32_t targetSubmittedFrames =
      config.benchmark && config.frameCount > 0 ? config.warmupFrames + config.frameCount : 0;
  std::ofstream benchmarkFrames;
  if (config.benchmark) {
    std::filesystem::create_directories(config.outputDirectory);
    benchmarkFrames.open(config.outputDirectory / "frames.csv");
    benchmarkFrames
        << "frame,technique,cpuFrameMs,cpuClusterBuildMs,gpuClusterBuildMs,gpuLightingPassMs,gpuObjectPassMs,"
           "timingSource,activeLights,drawCalls,visibleObjects,activeClusters,maxLightsPerCluster,"
           "lightIndexCapacity,explicitClusters,bitsetClusters,overflowCount,evaluatedLightSamples,"
           "prunedLightSamples,predictedErrorBound,splitCount,mergeCount,clusterChurn,"
           "offscreenMse,offscreenPsnr,offscreenSsim,measurementGroup\n";
    std::ofstream manifest{config.outputDirectory / "manifest.json"};
    const auto capabilities = lveDevice.getCapabilities();
    manifest << "{\n";
    manifest << "  \"technique\": \"" << beacon::toString(config.technique) << "\",\n";
    manifest << "  \"scene\": \"" << beacon::toString(config.scene) << "\",\n";
    manifest << "  \"lightDistribution\": \"" << beacon::toString(config.lightDistribution) << "\",\n";
    manifest << "  \"objects\": " << config.objectCount << ",\n";
    manifest << "  \"lights\": " << config.lightCount << ",\n";
    manifest << "  \"frames\": " << config.frameCount << ",\n";
    manifest << "  \"warmupFrames\": " << config.warmupFrames << ",\n";
    manifest << "  \"showLightBillboards\": " << (config.showLightBillboards ? "true" : "false") << ",\n";
    manifest << "  \"captureReference\": " << (config.captureReference ? "true" : "false") << ",\n";
    manifest << "  \"gitCommit\": \"" << currentGitCommit() << "\",\n";
    manifest << "  \"deviceName\": \"" << capabilities.properties.deviceName << "\",\n";
    manifest << "  \"vendorId\": " << capabilities.properties.vendorID << ",\n";
    manifest << "  \"deviceId\": " << capabilities.properties.deviceID << ",\n";
    manifest << "  \"apiVersion\": " << capabilities.properties.apiVersion << ",\n";
    manifest << "  \"driverVersion\": " << capabilities.properties.driverVersion << ",\n";
    manifest << "  \"timestampQueries\": " << (capabilities.timestampQueries ? "true" : "false") << ",\n";
    manifest << "  \"multiDrawIndirect\": " << (capabilities.multiDrawIndirect ? "true" : "false") << ",\n";
    manifest << "  \"shaderInt64\": " << (capabilities.shaderInt64 ? "true" : "false") << ",\n";
    manifest << "  \"shaderHashesFnv1a64\": {\n";
    std::vector<std::filesystem::path> shaderFiles;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path{ENGINE_DIR} / "shaders")) {
      if (entry.path().extension() == ".spv") shaderFiles.push_back(entry.path());
    }
    std::sort(shaderFiles.begin(), shaderFiles.end());
    for (size_t i = 0; i < shaderFiles.size(); ++i) {
      manifest << "    \"" << shaderFiles[i].filename().string() << "\": \""
               << std::hex << std::setw(16) << std::setfill('0') << fnv1aFile(shaderFiles[i])
               << std::dec << "\"" << (i + 1 < shaderFiles.size() ? "," : "") << "\n";
    }
    manifest << "  },\n";
    manifest << "  \"measurementGroup\": \"vulkan_windowed_measurements\",\n";
    manifest << "  \"clusterCount\": " << clusterBuildConfig.clusterCount << ",\n";
    manifest << "  \"maxLightsPerCluster\": " << clusterBuildConfig.maxLightsPerCluster << ",\n";
    manifest << "  \"path\": \"vulkan-windowed\"\n";
    manifest << "}\n";
  }
  if (config.verbose) {
    std::cout << "BEACON Vulkan run\n"
              << "  technique: " << beacon::toString(config.technique) << "\n"
              << "  lights requested: " << config.lightCount << "\n"
              << "  objects requested: " << config.objectCount << "\n"
              << "  warmup frames: " << config.warmupFrames << "\n"
              << "  measured frames: " << config.frameCount << "\n"
              << "  fixed compute clustered path: " << usesGpuClusteredLighting(config.technique) << "\n"
              << "  adaptive clustered path: " << usesAdaptiveClusteredLighting(config.technique) << "\n"
              << "  offscreen reference capture: "
              << (usesClusteredLighting(config.technique) && config.captureReference) << "\n"
              << "  show light billboards: " << config.showLightBillboards << std::endl;
  }
  while (!lveWindow.shouldClose()) {
    auto frameCpuStart = std::chrono::high_resolution_clock::now();
    double cpuClusterBuildMs = 0.0;
    glfwPollEvents();

    auto newTime = std::chrono::high_resolution_clock::now();
    float frameTime =
        std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
    currentTime = newTime;

    cameraController.moveInPlaneXZ(lveWindow.getGLFWwindow(), frameTime, viewerObject);
    camera.setViewYXZ(viewerObject.transform.translation, viewerObject.transform.rotation);

    float aspect = lveRenderer.getAspectRatio();
    camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100.f);

    if (auto commandBuffer = lveRenderer.beginFrame()) {
      gpuProfiler.reset(commandBuffer);
      gpuProfiler.writeTimestamp(commandBuffer, "frame_start");
      int frameIndex = lveRenderer.getFrameIndex();
      std::vector<FrameInfo::InstancedDrawBatch> instancedBatches;
      FrameInfo frameInfo{
          frameIndex,
          frameTime,
          commandBuffer,
          camera,
          globalDescriptorSets[frameIndex],
          gameObjects,
          usesObjectInstanceBuffer(config.technique) ? &instancedBatches : nullptr};

      // update
      if (usesObjectInstanceBuffer(config.technique)) {
        std::unordered_map<LveModel*, std::vector<LveGameObject::id_t>> groupedObjects;
        for (auto& kv : gameObjects) {
          auto& obj = kv.second;
          if (obj.model == nullptr) continue;
          groupedObjects[obj.model.get()].push_back(obj.getId());
        }

        std::vector<SsboObjectData> objectData;
        objectData.reserve(objectBufferCapacity);
        for (const auto& kv : groupedObjects) {
          const auto& ids = kv.second;
          if (ids.empty()) continue;
          FrameInfo::InstancedDrawBatch batch{};
          batch.representativeObjectId = ids.front();
          batch.firstInstance = static_cast<uint32_t>(objectData.size());
          batch.instanceCount = static_cast<uint32_t>(ids.size());
          instancedBatches.push_back(batch);
          for (auto id : ids) {
            auto& obj = gameObjects.at(id);
            objectData.push_back({obj.transform.mat4(), mat4FromMat3(obj.transform.normalMatrix())});
          }
        }
        if (objectData.empty()) {
          objectData.push_back({});
        }
        objectBuffers[frameIndex]->writeToBuffer(
            objectData.data(), sizeof(SsboObjectData) * objectData.size());
        objectBuffers[frameIndex]->flush(sizeof(SsboObjectData) * objectData.size());
      }

      GlobalUbo ubo{};
      ubo.projection = camera.getProjection();
      ubo.view = camera.getView();
      ubo.inverseView = camera.getInverseView();
      std::vector<SsboPointLight> ssboLights;
      pointLightSystem.update(
          frameInfo,
          ubo,
          usesSsboLightBuffer(config.technique) ? &ssboLights : nullptr,
          usesSsboLightBuffer(config.technique));
      uboBuffers[frameIndex]->writeToBuffer(&ubo);
      uboBuffers[frameIndex]->flush();
      if (usesSsboLightBuffer(config.technique)) {
        if (ssboLights.empty()) {
          ssboLights.push_back({});
        }
        lightBuffers[frameIndex]->writeToBuffer(
            ssboLights.data(), sizeof(SsboPointLight) * ssboLights.size());
        lightBuffers[frameIndex]->flush(sizeof(SsboPointLight) * ssboLights.size());
      }
      if (usesGpuClusteredLighting(config.technique)) {
        clusterBuildConfig = makeClusterBuildConfig(gameObjects);
        clusterBuildConfig.lightCount = static_cast<uint32_t>(ssboLights.size());
        auto clusterRuntimeConfig = makeClusterRuntimeConfig(clusterBuildConfig);
        clusterConfigBuffers[frameIndex]->writeToBuffer(&clusterRuntimeConfig);
        clusterConfigBuffers[frameIndex]->flush();
        gpuProfiler.writeTimestamp(commandBuffer, "cluster_build_start");
        clusteredLightingSystem->dispatch(
            commandBuffer, globalDescriptorSets[frameIndex], clusterBuildConfig);
        gpuProfiler.writeTimestamp(commandBuffer, "cluster_build_end");
      } else if (usesAdaptiveClusteredLighting(config.technique)) {
        clusterBuildConfig = makeClusterBuildConfig(gameObjects);
        gpuProfiler.writeTimestamp(commandBuffer, "cluster_build_start");
        auto clusterCpuStart = std::chrono::high_resolution_clock::now();
        auto adaptive = beacon::buildAdaptiveVulkanClusters(
            ssboLights,
            glm::vec3{clusterBuildConfig.worldMin},
            glm::vec3{clusterBuildConfig.worldMax},
            config.technique,
            budgetController,
            config.imageErrorBudget,
            adaptiveTemporalState);
        cpuClusterBuildMs = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - clusterCpuStart).count();
        adaptiveStats = adaptive.stats;
        clusterConfigBuffers[frameIndex]->writeToBuffer(&adaptive.config);
        clusterConfigBuffers[frameIndex]->flush();
        clusterHeaderBuffers[frameIndex]->writeToBuffer(
            adaptive.headers.data(), adaptive.headers.size() * sizeof(ClusterHeaderData));
        clusterHeaderBuffers[frameIndex]->flush(adaptive.headers.size() * sizeof(ClusterHeaderData));
        clusterLightIndexBuffers[frameIndex]->writeToBuffer(
            adaptive.lightData.data(), adaptive.lightData.size() * sizeof(uint32_t));
        clusterLightIndexBuffers[frameIndex]->flush(adaptive.lightData.size() * sizeof(uint32_t));
        adaptiveNodeBuffers[frameIndex]->writeToBuffer(
            adaptive.nodes.data(), adaptive.nodes.size() * sizeof(AdaptiveClusterNodeData));
        adaptiveNodeBuffers[frameIndex]->flush(adaptive.nodes.size() * sizeof(AdaptiveClusterNodeData));
        clusterBuildConfig.clusterCount = adaptive.config.clusterCount;
        clusterBuildConfig.maxLightsPerCluster = adaptive.config.maxLightsPerCluster;
        clusterBuildConfig.lightIndexCapacity = adaptive.config.lightIndexCapacity;
        gpuProfiler.writeTimestamp(commandBuffer, "cluster_build_end");
      }

      if (offscreenComparison != nullptr) {
        offscreenComparison->begin(commandBuffer, beacon::OffscreenComparison::TARGET_REFERENCE);
        offscreenReferenceSystem->renderGameObjects(frameInfo);
        offscreenComparison->end(commandBuffer);

        offscreenComparison->begin(commandBuffer, beacon::OffscreenComparison::TARGET_TEST);
        offscreenClusteredSystem->renderGameObjects(frameInfo);
        offscreenComparison->end(commandBuffer);
        offscreenComparison->copyTargetsToBuffers(commandBuffer);
      }

      // render
      lveRenderer.beginSwapChainRenderPass(commandBuffer);
      gpuProfiler.writeTimestamp(commandBuffer, "lighting_pass_start");

      // order here matters
      simpleRenderSystem.renderGameObjects(frameInfo);
      if (config.showLightBillboards) {
        pointLightSystem.render(frameInfo);
      }
      gpuProfiler.writeTimestamp(commandBuffer, "lighting_pass_end");

      lveRenderer.endSwapChainRenderPass(commandBuffer);
      lveRenderer.endFrame();
      beacon::ImageComparisonMetrics imageMetrics{};
      bool hasImageMetrics = false;
      if (offscreenComparison != nullptr) {
        vkDeviceWaitIdle(lveDevice.device());
        imageMetrics = offscreenComparison->compare();
        hasImageMetrics = true;
      }
      submittedFrames += 1;
      bool isWarmup = config.benchmark && submittedFrames <= config.warmupFrames;
      if (benchmarkFrames.is_open() && !isWarmup) {
        uint32_t activeLights =
            usesSsboLightBuffer(config.technique) ? static_cast<uint32_t>(ssboLights.size())
                                                  : static_cast<uint32_t>(ubo.numLights);
        uint32_t drawCalls = 0;
        if (usesObjectInstanceBuffer(config.technique)) {
          drawCalls = static_cast<uint32_t>(instancedBatches.size());
        } else {
          for (auto& kv : gameObjects) {
            if (kv.second.model != nullptr) drawCalls += 1;
          }
        }
        if (config.showLightBillboards) {
          for (auto& kv : gameObjects) {
            if (kv.second.pointLight != nullptr) drawCalls += 1;
          }
        }
        auto frameCpuEnd = std::chrono::high_resolution_clock::now();
        double cpuFrameMs =
            std::chrono::duration<double, std::milli>(frameCpuEnd - frameCpuStart).count();
        auto gpuTimings = gpuProfiler.collectMilliseconds();
        double gpuObjectPassMs = 0.0;
        double gpuClusterBuildMs = 0.0;
        double gpuLightingPassMs = 0.0;
        std::string timingSource = "cpu-fallback";
        auto clusterBuildIt = gpuTimings.find("cluster_build_start_to_cluster_build_end");
        if (clusterBuildIt != gpuTimings.end()) {
          gpuClusterBuildMs = clusterBuildIt->second;
          timingSource = "vulkan-query-pool";
        }
        auto objectPassIt = gpuTimings.find("lighting_pass_start_to_lighting_pass_end");
        if (objectPassIt != gpuTimings.end()) {
          gpuObjectPassMs = objectPassIt->second;
          gpuLightingPassMs = objectPassIt->second;
          timingSource = "vulkan-query-pool";
        }
        benchmarkFrames << measuredFrames << "," << beacon::toString(config.technique) << ","
                        << cpuFrameMs << "," << cpuClusterBuildMs << "," << gpuClusterBuildMs << "," << gpuLightingPassMs
                        << "," << gpuObjectPassMs << "," << timingSource << "," << activeLights
                        << "," << drawCalls << "," << gameObjects.size() << ","
                        << (usesClusteredLighting(config.technique) ?
                              (usesAdaptiveClusteredLighting(config.technique) ? adaptiveStats.activeClusters
                                                                              : clusterBuildConfig.clusterCount) : 0)
                        << ","
                        << (usesAdaptiveClusteredLighting(config.technique) ? adaptiveStats.maximumLightsPerCluster :
                            usesGpuClusteredLighting(config.technique) ? clusterBuildConfig.maxLightsPerCluster : 0)
                        << ","
                        << (usesClusteredLighting(config.technique) ? clusterBuildConfig.lightIndexCapacity : 0)
                        << "," << adaptiveStats.explicitClusterCount
                        << "," << adaptiveStats.bitsetClusterCount
                        << "," << adaptiveStats.overflowCount
                        << "," << adaptiveStats.evaluatedLightSamples
                        << "," << adaptiveStats.prunedLightSamples
                        << "," << adaptiveStats.predictedErrorBound
                        << "," << adaptiveStats.splitCount
                        << "," << adaptiveStats.mergeCount
                        << "," << adaptiveStats.clusterChurn
                        << ","
                        << (hasImageMetrics ? imageMetrics.mse : -1.0) << ","
                        << (hasImageMetrics ? imageMetrics.psnr : -1.0) << ","
                        << (hasImageMetrics ? imageMetrics.ssim : -1.0) << ","
                        << (timingSource == "vulkan-query-pool" ? "vulkan_gpu_measurements"
                                                                 : "vulkan_cpu_measurements")
                        << "\n";
        if (config.technique == beacon::RenderTechnique::BeaconFull) {
          adaptiveStats.gpu.clusterBuildMs = gpuClusterBuildMs > 0.0 ? gpuClusterBuildMs : cpuClusterBuildMs;
          adaptiveStats.gpu.lightingPassMs = gpuLightingPassMs > 0.0 ? gpuLightingPassMs : cpuFrameMs;
          budgetController.update(adaptiveStats);
        }
        measuredFrames += 1;
      }
      if (config.benchmark && targetSubmittedFrames > 0) {
        beacon::printProgressBar("Vulkan", submittedFrames, targetSubmittedFrames, config.verbose);
      }
      if (config.benchmark && targetSubmittedFrames > 0 && submittedFrames >= targetSubmittedFrames) {
        break;
      }
    }
  }

  vkDeviceWaitIdle(lveDevice.device());
  if (config.benchmark) {
    std::ofstream summary{config.outputDirectory / "summary.json"};
    summary << "{\n";
    summary << "  \"technique\": \"" << beacon::toString(config.technique) << "\",\n";
    summary << "  \"frames\": " << measuredFrames << ",\n";
    summary << "  \"warmupFrames\": " << config.warmupFrames << ",\n";
    summary << "  \"measurementGroup\": \"vulkan_windowed_measurements\",\n";
    summary << "  \"path\": \"vulkan-windowed\"\n";
    summary << "}\n";
    if (config.verbose) {
      std::cout << "Vulkan benchmark wrote results to "
                << std::filesystem::absolute(config.outputDirectory) << std::endl;
    }
  }
  if (config.benchmark && config.frameCount > 0) {
    beacon::finishProgressBar(config.verbose);
  }
}

void FirstApp::loadGameObjects() {
  if (config.scene == beacon::ScenePreset::Tutorial && config.lightDistribution == beacon::LightDistribution::Tutorial &&
      config.objectCount <= 3 && config.lightCount <= 6) {
    loadTutorialScene();
  } else {
    loadGeneratedScene();
  }
}

void FirstApp::loadTutorialScene() {
  std::shared_ptr<LveModel> lveModel =
      LveModel::createModelFromFile(lveDevice, "models/flat_vase.obj");
  auto flatVase = LveGameObject::createGameObject();
  flatVase.model = lveModel;
  flatVase.transform.translation = {-.5f, .5f, 0.f};
  flatVase.transform.scale = {3.f, 1.5f, 3.f};
  gameObjects.emplace(flatVase.getId(), std::move(flatVase));

  lveModel = LveModel::createModelFromFile(lveDevice, "models/smooth_vase.obj");
  auto smoothVase = LveGameObject::createGameObject();
  smoothVase.model = lveModel;
  smoothVase.transform.translation = {.5f, .5f, 0.f};
  smoothVase.transform.scale = {3.f, 1.5f, 3.f};
  gameObjects.emplace(smoothVase.getId(), std::move(smoothVase));

  lveModel = LveModel::createModelFromFile(lveDevice, "models/quad.obj");
  auto floor = LveGameObject::createGameObject();
  floor.model = lveModel;
  floor.transform.translation = {0.f, .5f, 0.f};
  floor.transform.scale = {3.f, 1.f, 3.f};
  gameObjects.emplace(floor.getId(), std::move(floor));

  std::vector<glm::vec3> lightColors{
      {1.f, .1f, .1f},
      {.1f, .1f, 1.f},
      {.1f, 1.f, .1f},
      {1.f, 1.f, .1f},
      {.1f, 1.f, 1.f},
      {1.f, 1.f, 1.f}  //
  };

  for (int i = 0; i < lightColors.size(); i++) {
    auto pointLight = LveGameObject::makePointLight(0.2f, 0.1f, glm::vec3(1.f), 4.0f);
    pointLight.color = lightColors[i];
    auto rotateLight = glm::rotate(
        glm::mat4(1.f),
        (i * glm::two_pi<float>()) / lightColors.size(),
        {0.f, -1.f, 0.f});
    pointLight.transform.translation = glm::vec3(rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f));
    gameObjects.emplace(pointLight.getId(), std::move(pointLight));
  }
}

void FirstApp::loadGeneratedScene() {
  std::shared_ptr<LveModel> flatVase =
      LveModel::createModelFromFile(lveDevice, "models/flat_vase.obj");
  std::shared_ptr<LveModel> smoothVase =
      LveModel::createModelFromFile(lveDevice, "models/smooth_vase.obj");
  std::shared_ptr<LveModel> floorModel =
      LveModel::createModelFromFile(lveDevice, "models/quad.obj");

  std::mt19937 rng{config.randomSeed};
  std::uniform_real_distribution<float> unit{-1.f, 1.f};
  std::uniform_real_distribution<float> color{0.15f, 1.f};

  auto floor = LveGameObject::createGameObject();
  floor.model = floorModel;
  floor.transform.translation = {0.f, 1.25f, 0.f};
  floor.transform.scale = {12.f, 1.f, 12.f};
  gameObjects.emplace(floor.getId(), std::move(floor));

  uint32_t objectTarget = std::max<uint32_t>(config.objectCount, 1);
  for (uint32_t i = 0; i < objectTarget; ++i) {
    auto object = LveGameObject::createGameObject();
    object.model = (i % 2 == 0) ? flatVase : smoothVase;
    float gridX = static_cast<float>(i % 32) - 16.f;
    float gridZ = static_cast<float>(i / 32) - 8.f;
    object.transform.translation = {gridX * 0.55f, 0.35f + 0.1f * unit(rng), gridZ * 0.55f};
    object.transform.rotation.y = unit(rng) * glm::two_pi<float>();
    object.transform.scale = {0.45f, 0.55f, 0.45f};
    gameObjects.emplace(object.getId(), std::move(object));
  }

  uint32_t lightTarget = usesSsboLightBuffer(config.technique)
                             ? config.lightCount
                             : std::min<uint32_t>(config.lightCount, MAX_LIGHTS);
  if (!usesSsboLightBuffer(config.technique) && config.lightCount > MAX_LIGHTS) {
    std::cerr << "Baseline UBO renderer supports " << MAX_LIGHTS << " lights; requested "
              << config.lightCount << ". Use ssbo, instanced, gpu-clustered, or a BEACON technique for larger counts.\n";
  }

  for (uint32_t i = 0; i < lightTarget; ++i) {
    float influenceRadius = config.lightDistribution == beacon::LightDistribution::LargeRadiusAdversarial
                                ? 18.f
                                : config.lightDistribution == beacon::LightDistribution::CameraAttached ? 10.f : 4.f;
    auto pointLight = LveGameObject::makePointLight(1.5f, 0.18f, glm::vec3(1.f), influenceRadius);
    pointLight.color = {color(rng), color(rng), color(rng)};
    if (config.lightDistribution == beacon::LightDistribution::SingleHotspot) {
      pointLight.transform.translation = {0.5f * unit(rng), -0.8f + 0.5f * unit(rng), 0.5f * unit(rng)};
    } else if (config.lightDistribution == beacon::LightDistribution::DepthStacked) {
      pointLight.transform.translation = {0.5f * unit(rng), -0.8f, -4.f + static_cast<float>(i) * 0.8f};
    } else {
      pointLight.transform.translation = {4.f * unit(rng), -0.8f + unit(rng), 4.f * unit(rng)};
    }
    gameObjects.emplace(pointLight.getId(), std::move(pointLight));
  }
}

}  // namespace lve
