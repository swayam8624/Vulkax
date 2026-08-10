#include "vulkax/relativity/kerr_live_queue.hpp"
#include "runtime_paths.hpp"

#include "lve_buffer.hpp"
#include "lve_device.hpp"
#include "vulkax/relativity/kerr_geodesic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace vulkax::relativity {
namespace {

constexpr uint32_t kWorkgroupSize = 256;
constexpr float kMass = 1.0f;
constexpr float kObserverRadius = 12.0f;
constexpr float kObserverPolar = 1.25f;
constexpr float kImagePlaneScale = 10.0f;

struct alignas(16) RayState {
  std::array<float, 4> position{};
  std::array<float, 4> conservedSigns{};
  std::array<float, 4> integration{};
  std::array<float, 4> jacobiPosition{};
  std::array<float, 4> jacobiWave{};
  std::array<float, 4> horizontal{};
  std::array<float, 4> horizontalDerivative{};
  std::array<float, 4> vertical{};
  std::array<float, 4> verticalDerivative{};
  std::array<float, 4> jacobiDiagnostics{};
  std::array<float, 4> transfer{};
  std::array<uint32_t, 4> counters{};
};

struct Control {
  uint32_t activeCount;
  uint32_t previousCount;
  uint32_t maximumCount;
  uint32_t iterations;
};

struct IndirectCommand {
  uint32_t x;
  uint32_t y;
  uint32_t z;
};

struct QueuePush {
  uint32_t phase;
  uint32_t sourceQueue;
  float mass;
  float spin;
  float horizon;
  float minimumStep;
  float maximumStep;
  float errorTolerance;
  uint32_t jacobiRayCount;
  uint32_t stepsPerDispatch;
  float observerRadius;
  uint32_t terminateAtObserver;
};

struct ShadePush {
  uint32_t outputWidth;
  uint32_t outputHeight;
  uint32_t queueWidth;
  uint32_t queueHeight;
  float mass;
  float spin;
  float diskGain;
  float exposure;
  float alphaPerRay;
  float betaPerRay;
  float observerInclination;
  float frameIndex;
};

static_assert(sizeof(RayState) == 192);
static_assert(sizeof(QueuePush) == 48);
static_assert(sizeof(ShadePush) == 48);

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) throw std::runtime_error(std::string{operation} + " failed");
}

std::vector<char> readBinary(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) throw std::runtime_error("could not read shader: " + path.string());
  const auto size = input.tellg();
  std::vector<char> bytes(static_cast<size_t>(size));
  input.seekg(0);
  input.read(bytes.data(), size);
  return bytes;
}

std::array<float, 4> launchWave(const KerrGeodesicConfig& config, float alpha, float beta) {
  const auto constants = kerrConstantsFromImagePlane(config, alpha, beta);
  const double radius = config.observerRadius;
  const double polar = config.observerInclinationRadians;
  const double radius2 = radius * radius;
  const double sine = std::sin(polar);
  const double cosine = std::cos(polar);
  const double sine2 = std::max(sine * sine, 1e-12);
  const double sigma = radius2 + config.spin * config.spin * cosine * cosine;
  const double delta = radius2 - 2.0 * config.mass * radius + config.spin * config.spin;
  const double p = constants.energy * (radius2 + config.spin * config.spin) -
                   config.spin * constants.axialAngularMomentum;
  const double shifted = constants.axialAngularMomentum - config.spin * constants.energy;
  const double radialPotential =
      p * p - delta * (shifted * shifted + constants.carterConstant);
  const double polarPotential = constants.carterConstant + config.spin * config.spin * cosine * cosine -
      constants.axialAngularMomentum * constants.axialAngularMomentum * cosine * cosine / sine2;
  return {
      static_cast<float>((
          -config.spin * (config.spin * sine2 - constants.axialAngularMomentum) +
          (radius2 + config.spin * config.spin) * p / delta) / sigma),
      static_cast<float>(-std::sqrt(std::max(0.0, radialPotential)) / sigma),
      static_cast<float>((beta >= 0.0f ? 1.0 : -1.0) *
                         std::sqrt(std::max(0.0, polarPotential)) / sigma),
      static_cast<float>((constants.axialAngularMomentum / sine2 - config.spin +
                          config.spin * p / delta) / sigma)};
}

std::array<float, 4> launchDerivative(
    const KerrGeodesicConfig& config, float alpha, float beta, bool horizontal) {
  constexpr float differential = 1e-4f;
  const auto positive = launchWave(
      config,
      alpha + (horizontal ? differential : 0.0f),
      beta + (horizontal ? 0.0f : differential));
  const auto negative = launchWave(
      config,
      alpha - (horizontal ? differential : 0.0f),
      beta - (horizontal ? 0.0f : differential));
  std::array<float, 4> result{};
  for (size_t component = 0; component < result.size(); ++component)
    result[component] = (positive[component] - negative[component]) / (2.0f * differential);
  return result;
}

VkPipeline createComputePipeline(
    VkDevice device,
    VkPipelineLayout layout,
    const std::filesystem::path& shaderPath) {
  const auto code = readBinary(shaderPath);
  VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  moduleInfo.codeSize = code.size();
  moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
  VkShaderModule module = VK_NULL_HANDLE;
  check(vkCreateShaderModule(device, &moduleInfo, nullptr, &module), "vkCreateShaderModule");
  VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipelineInfo.stage = {
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      nullptr,
      0,
      VK_SHADER_STAGE_COMPUTE_BIT,
      module,
      "main",
      nullptr};
  pipelineInfo.layout = layout;
  VkPipeline pipeline = VK_NULL_HANDLE;
  const VkResult result =
      vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
  vkDestroyShaderModule(device, module, nullptr);
  check(result, "vkCreateComputePipelines");
  return pipeline;
}

}  // namespace

KerrLiveQueue::KerrLiveQueue(
    lve::LveDevice& device,
    VkImageView outputImage,
    VkExtent2D outputExtent,
    float spin)
    : device_{device}, outputImage_{outputImage}, outputExtent_{outputExtent}, spin_{spin} {
  queueWidth_ = std::max(1u, (outputExtent_.width + 3u) / 4u);
  queueHeight_ = std::max(1u, (outputExtent_.height + 3u) / 4u);
  rayCount_ = queueWidth_ * queueHeight_;
  groupCount_ = (rayCount_ + kWorkgroupSize - 1u) / kWorkgroupSize;
  if (groupCount_ > 1024u) throw std::runtime_error("Kerr live queue exceeds scan capacity");
  createBuffers();
  initializeRays();
  createDescriptors();
  createPipelines();
}

KerrLiveQueue::~KerrLiveQueue() { destroyVulkanObjects(); }

void KerrLiveQueue::createBuffers() {
  const auto make = [&](VkDeviceSize elementBytes, uint32_t count, VkBufferUsageFlags usage) {
    auto buffer = std::make_unique<lve::LveBuffer>(
        device_,
        elementBytes,
        count,
        usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(buffer->map(), "vkMapMemory Kerr queue");
    return buffer;
  };
  buffers_.push_back(make(sizeof(RayState), rayCount_, 0));
  for (uint32_t index = 0; index < 4; ++index)
    buffers_.push_back(make(sizeof(uint32_t), rayCount_, 0));
  buffers_.push_back(make(sizeof(Control), 1, 0));
  buffers_.push_back(make(
      sizeof(IndirectCommand), 1, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));
  buffers_.push_back(make(sizeof(uint32_t), groupCount_, 0));
  buffers_.push_back(make(sizeof(uint32_t), groupCount_, 0));
}

void KerrLiveQueue::initializeRays() {
  std::vector<RayState> states(rayCount_);
  std::vector<uint32_t> active(rayCount_);
  KerrGeodesicConfig config{};
  config.mass = kMass;
  config.spin = spin_;
  config.observerRadius = kObserverRadius;
  config.observerInclinationRadians = kObserverPolar;
  const float sineObserver = std::sin(kObserverPolar);
  const float cosineObserver = std::cos(kObserverPolar);
  const float aspect = static_cast<float>(outputExtent_.width) / outputExtent_.height;
  for (uint32_t index = 0; index < rayCount_; ++index) {
    const uint32_t x = index % queueWidth_;
    const uint32_t y = index / queueWidth_;
    const float normalizedX = (static_cast<float>(x) + 0.5f) / queueWidth_;
    const float normalizedY = (static_cast<float>(y) + 0.5f) / queueHeight_;
    const float alpha = (normalizedX - 0.5f) * 2.0f * aspect * kImagePlaneScale;
    const float beta = (normalizedY - 0.5f) * 2.0f * kImagePlaneScale;
    RayState& ray = states[index];
    ray.position = {kObserverRadius, kObserverPolar, 0.0f, 0.0f};
    ray.conservedSigns = {
        -alpha * sineObserver,
        beta * beta + cosineObserver * cosineObserver * (alpha * alpha - spin_ * spin_),
        -1.0f,
        beta >= 0.0f ? 1.0f : -1.0f};
    ray.integration = {0.0f, 64.0f, 0.07f, 0.0f};
    ray.jacobiPosition = {0.0f, kObserverRadius, kObserverPolar, 0.0f};
    ray.jacobiWave = launchWave(config, alpha, beta);
    ray.horizontalDerivative = launchDerivative(config, alpha, beta, true);
    ray.verticalDerivative = launchDerivative(config, alpha, beta, false);
    ray.transfer = {0.0f, 0.0f, kObserverPolar, 1.0f};
    active[index] = index;
  }
  buffers_[0]->writeToBuffer(states.data(), sizeof(RayState) * states.size());
  buffers_[1]->writeToBuffer(active.data(), sizeof(uint32_t) * active.size());
  const Control control{rayCount_, 0u, rayCount_, 0u};
  buffers_[5]->writeToBuffer(const_cast<Control*>(&control), sizeof(control));
  const IndirectCommand indirect{groupCount_, 1u, 1u};
  buffers_[6]->writeToBuffer(const_cast<IndirectCommand*>(&indirect), sizeof(indirect));
}

void KerrLiveQueue::createDescriptors() {
  std::array<VkDescriptorSetLayoutBinding, 9> queueBindings{};
  for (uint32_t index = 0; index < queueBindings.size(); ++index)
    queueBindings[index] =
        {index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = static_cast<uint32_t>(queueBindings.size());
  layoutInfo.pBindings = queueBindings.data();
  check(
      vkCreateDescriptorSetLayout(
          device_.device(), &layoutInfo, nullptr, &queueDescriptorLayout_),
      "vkCreateDescriptorSetLayout Kerr queue");

  const std::array<VkDescriptorSetLayoutBinding, 2> shadeBindings{{
      {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}};
  layoutInfo.bindingCount = static_cast<uint32_t>(shadeBindings.size());
  layoutInfo.pBindings = shadeBindings.data();
  check(
      vkCreateDescriptorSetLayout(
          device_.device(), &layoutInfo, nullptr, &shadeDescriptorLayout_),
      "vkCreateDescriptorSetLayout Kerr shade");

  const std::array<VkDescriptorPoolSize, 2> poolSizes{{
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}}};
  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.maxSets = 2;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  check(vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &descriptorPool_),
        "vkCreateDescriptorPool Kerr");
  const VkDescriptorSetLayout layouts[2]{queueDescriptorLayout_, shadeDescriptorLayout_};
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorPool = descriptorPool_;
  allocate.descriptorSetCount = 2;
  allocate.pSetLayouts = layouts;
  VkDescriptorSet sets[2]{};
  check(vkAllocateDescriptorSets(device_.device(), &allocate, sets),
        "vkAllocateDescriptorSets Kerr");
  queueDescriptorSet_ = sets[0];
  shadeDescriptorSet_ = sets[1];

  std::array<VkDescriptorBufferInfo, 10> bufferInfos{};
  std::array<VkWriteDescriptorSet, 11> writes{};
  for (uint32_t index = 0; index < buffers_.size(); ++index) {
    bufferInfos[index] = buffers_[index]->descriptorInfo();
    writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[index].dstSet = queueDescriptorSet_;
    writes[index].dstBinding = index;
    writes[index].descriptorCount = 1;
    writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[index].pBufferInfo = &bufferInfos[index];
  }
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageView = outputImage_;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  writes[9] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  writes[9].dstSet = shadeDescriptorSet_;
  writes[9].dstBinding = 0;
  writes[9].descriptorCount = 1;
  writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  writes[9].pImageInfo = &imageInfo;
  bufferInfos[9] = buffers_[0]->descriptorInfo();
  writes[10] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  writes[10].dstSet = shadeDescriptorSet_;
  writes[10].dstBinding = 1;
  writes[10].descriptorCount = 1;
  writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[10].pBufferInfo = &bufferInfos[9];
  vkUpdateDescriptorSets(
      device_.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void KerrLiveQueue::createPipelines() {
  VkPushConstantRange queueRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(QueuePush)};
  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &queueDescriptorLayout_;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &queueRange;
  check(vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &queuePipelineLayout_),
        "vkCreatePipelineLayout Kerr queue");
  VkPushConstantRange shadeRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ShadePush)};
  layoutInfo.pSetLayouts = &shadeDescriptorLayout_;
  layoutInfo.pPushConstantRanges = &shadeRange;
  check(vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &shadePipelineLayout_),
        "vkCreatePipelineLayout Kerr shade");
  queuePipeline_ = createComputePipeline(
      device_.device(),
      queuePipelineLayout_,
      lve::resolveRuntimeResource("shaders/vulkax_active_ray_compaction.comp.spv"));
  shadePipeline_ = createComputePipeline(
      device_.device(),
      shadePipelineLayout_,
      lve::resolveRuntimeResource("shaders/vulkax_kerr_queue_shade.comp.spv"));
}

void KerrLiveQueue::record(
    VkCommandBuffer commandBuffer, uint32_t integrationDispatches, uint32_t frameIndex) {
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, queuePipeline_);
  vkCmdBindDescriptorSets(
      commandBuffer,
      VK_PIPELINE_BIND_POINT_COMPUTE,
      queuePipelineLayout_,
      0,
      1,
      &queueDescriptorSet_,
      0,
      nullptr);
  const VkMemoryBarrier barrier{
      VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      nullptr,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT};
  const float horizon = kMass + std::sqrt(kMass * kMass - spin_ * spin_) + 1e-4f;
  for (uint32_t dispatch = 0; dispatch < integrationDispatches; ++dispatch) {
    QueuePush push{
        0,
        queueIteration_ % 2u,
        kMass,
        spin_,
        horizon,
        0.0025f,
        0.12f,
        2e-5f,
        rayCount_,
        4u,
        kObserverRadius,
        1u | (16u << 8u)};
    vkCmdPushConstants(
        commandBuffer, queuePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatchIndirect(commandBuffer, buffers_[6]->getBuffer(), 0);
    vkCmdPipelineBarrier(
        commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    push.phase = 1;
    vkCmdPushConstants(
        commandBuffer, queuePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatchIndirect(commandBuffer, buffers_[6]->getBuffer(), 0);
    vkCmdPipelineBarrier(
        commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    push.phase = 2;
    vkCmdPushConstants(
        commandBuffer, queuePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    vkCmdPipelineBarrier(
        commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    push.phase = 3;
    vkCmdPushConstants(
        commandBuffer, queuePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(commandBuffer, groupCount_, 1, 1);
    vkCmdPipelineBarrier(
        commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    ++queueIteration_;
  }

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shadePipeline_);
  vkCmdBindDescriptorSets(
      commandBuffer,
      VK_PIPELINE_BIND_POINT_COMPUTE,
      shadePipelineLayout_,
      0,
      1,
      &shadeDescriptorSet_,
      0,
      nullptr);
  const float aspect = static_cast<float>(outputExtent_.width) / outputExtent_.height;
  const ShadePush shade{
      outputExtent_.width,
      outputExtent_.height,
      queueWidth_,
      queueHeight_,
      kMass,
      spin_,
      1.0f,
      1.0f,
      2.0f * aspect * kImagePlaneScale / queueWidth_,
      2.0f * kImagePlaneScale / queueHeight_,
      kObserverPolar,
      static_cast<float>(frameIndex)};
  vkCmdPushConstants(
      commandBuffer, shadePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(shade), &shade);
  vkCmdDispatch(
      commandBuffer, (outputExtent_.width + 15u) / 16u, (outputExtent_.height + 15u) / 16u, 1);
}

void KerrLiveQueue::destroyVulkanObjects() {
  buffers_.clear();
  if (shadePipeline_ != VK_NULL_HANDLE)
    vkDestroyPipeline(device_.device(), shadePipeline_, nullptr);
  if (queuePipeline_ != VK_NULL_HANDLE)
    vkDestroyPipeline(device_.device(), queuePipeline_, nullptr);
  if (shadePipelineLayout_ != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(device_.device(), shadePipelineLayout_, nullptr);
  if (queuePipelineLayout_ != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(device_.device(), queuePipelineLayout_, nullptr);
  if (descriptorPool_ != VK_NULL_HANDLE)
    vkDestroyDescriptorPool(device_.device(), descriptorPool_, nullptr);
  if (shadeDescriptorLayout_ != VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(device_.device(), shadeDescriptorLayout_, nullptr);
  if (queueDescriptorLayout_ != VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(device_.device(), queueDescriptorLayout_, nullptr);
}

}  // namespace vulkax::relativity
