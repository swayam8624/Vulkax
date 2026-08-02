#include "vulkax/sim/mac_live_volume.hpp"

#include "lve_buffer.hpp"
#include "lve_device.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vulkax::sim {
namespace {

constexpr uint32_t kNx = 24;
constexpr uint32_t kNy = 32;
constexpr uint32_t kNz = 24;

struct alignas(16) MacPass {
  uint32_t nx = kNx;
  uint32_t ny = kNy;
  uint32_t nz = kNz;
  uint32_t pass = 0;
  float dt = 1.0f / 60.0f;
  float buoyancy = 2.4f;
  float smokeWeight = 0.55f;
  float parity = 0.0f;
  uint32_t outputWidth = 1;
  uint32_t outputHeight = 1;
  float extinction = 3.2f;
  float emission = 1.4f;
  uint32_t triangleCount = 0;
  float bodyMass = 2.0f;
  uint32_t padding[2]{};
};

struct PresentPush {
  uint32_t width;
  uint32_t height;
};

static_assert(sizeof(MacPass) == 64);

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

VkPipeline createComputePipeline(
    VkDevice device, VkPipelineLayout layout, const std::filesystem::path& path) {
  const auto code = readBinary(path);
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

MacLiveVolume::MacLiveVolume(
    lve::LveDevice& device, VkImageView outputImage, VkExtent2D outputExtent)
    : device_{device}, outputImage_{outputImage}, outputExtent_{outputExtent} {
  createBuffers();
  initializeFields();
  createDescriptors();
  createPipelines();
}

MacLiveVolume::~MacLiveVolume() { destroyVulkanObjects(); }

void MacLiveVolume::createBuffers() {
  const size_t cells = static_cast<size_t>(kNx) * kNy * kNz;
  const size_t coarseCells =
      static_cast<size_t>((kNx + 1u) / 2u) * ((kNy + 1u) / 2u) * ((kNz + 1u) / 2u);
  const size_t bricks =
      static_cast<size_t>((kNx + 3u) / 4u) * ((kNy + 3u) / 4u) * ((kNz + 3u) / 4u);
  const std::array<size_t, 32> counts{
      cells, cells,
      static_cast<size_t>(kNx + 1u) * kNy * kNz,
      static_cast<size_t>(kNx) * (kNy + 1u) * kNz,
      static_cast<size_t>(kNx) * kNy * (kNz + 1u),
      static_cast<size_t>(kNx + 1u) * kNy * kNz,
      static_cast<size_t>(kNx) * (kNy + 1u) * kNz,
      static_cast<size_t>(kNx) * kNy * (kNz + 1u),
      cells, cells, cells, cells, cells, cells, cells, cells, cells, cells,
      static_cast<size_t>(outputExtent_.width) * outputExtent_.height * 4u,
      cells, cells * 3u, coarseCells, 4u, coarseCells, coarseCells, coarseCells,
      bricks, bricks, 4u, 3u, 8u, 24u};
  for (size_t index = 0; index < buffers_.size(); ++index) {
    buffers_[index] = std::make_unique<lve::LveBuffer>(
        device_,
        sizeof(float),
        static_cast<uint32_t>(counts[index]),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(buffers_[index]->map(), "vkMapMemory MAC live buffer");
    std::vector<float> zero(counts[index], 0.0f);
    buffers_[index]->writeToBuffer(zero.data(), zero.size() * sizeof(float));
  }
}

void MacLiveVolume::initializeFields() {
  const size_t cells = static_cast<size_t>(kNx) * kNy * kNz;
  std::vector<float> density(cells);
  std::vector<float> temperature(cells);
  for (uint32_t z = 0; z < kNz; ++z) {
    for (uint32_t y = 0; y < kNy; ++y) {
      for (uint32_t x = 0; x < kNx; ++x) {
        const float px = (static_cast<float>(x) + 0.5f) / kNx - 0.5f;
        const float py = (static_cast<float>(y) + 0.5f) / kNy - 0.22f;
        const float pz = (static_cast<float>(z) + 0.5f) / kNz - 0.5f;
        const float source = std::exp(-(px * px + 1.8f * py * py + pz * pz) / 0.018f);
        const size_t index = (static_cast<size_t>(z) * kNy + y) * kNx + x;
        density[index] = 0.65f * source;
        temperature[index] = source;
      }
    }
  }
  buffers_[0]->writeToBuffer(density.data(), density.size() * sizeof(float));
  buffers_[1]->writeToBuffer(temperature.data(), temperature.size() * sizeof(float));
  std::array<float, 24> body{
      0.66f, 0.30f, 0.50f, 2.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
      0.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f,
      0.012f, 0.012f, 0.012f, 0.0f,
      1.0f, 1.0f, 1.0f, 0.0f};
  buffers_[31]->writeToBuffer(body.data(), sizeof(body));
  const size_t bricks = buffers_[27]->getBufferSize() / sizeof(float);
  std::vector<uint32_t> active(bricks, 1u);
  buffers_[27]->writeToBuffer(active.data(), active.size() * sizeof(uint32_t));
}

void MacLiveVolume::createDescriptors() {
  std::array<VkDescriptorSetLayoutBinding, 32> solverBindings{};
  for (uint32_t index = 0; index < solverBindings.size(); ++index)
    solverBindings[index] =
        {index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = static_cast<uint32_t>(solverBindings.size());
  layoutInfo.pBindings = solverBindings.data();
  check(vkCreateDescriptorSetLayout(
            device_.device(), &layoutInfo, nullptr, &solverDescriptorLayout_),
        "vkCreateDescriptorSetLayout MAC solver");
  const std::array<VkDescriptorSetLayoutBinding, 2> presentBindings{{
      {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}};
  layoutInfo.bindingCount = static_cast<uint32_t>(presentBindings.size());
  layoutInfo.pBindings = presentBindings.data();
  check(vkCreateDescriptorSetLayout(
            device_.device(), &layoutInfo, nullptr, &presentDescriptorLayout_),
        "vkCreateDescriptorSetLayout MAC present");
  const std::array<VkDescriptorPoolSize, 2> sizes{{
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 33},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}}};
  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.maxSets = 2;
  poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
  poolInfo.pPoolSizes = sizes.data();
  check(vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &descriptorPool_),
        "vkCreateDescriptorPool MAC live");
  const VkDescriptorSetLayout layouts[2]{solverDescriptorLayout_, presentDescriptorLayout_};
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorPool = descriptorPool_;
  allocate.descriptorSetCount = 2;
  allocate.pSetLayouts = layouts;
  VkDescriptorSet sets[2]{};
  check(vkAllocateDescriptorSets(device_.device(), &allocate, sets),
        "vkAllocateDescriptorSets MAC live");
  solverDescriptorSet_ = sets[0];
  presentDescriptorSet_ = sets[1];
  std::array<VkDescriptorBufferInfo, 33> infos{};
  std::array<VkWriteDescriptorSet, 34> writes{};
  for (uint32_t index = 0; index < buffers_.size(); ++index) {
    infos[index] = buffers_[index]->descriptorInfo();
    writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[index].dstSet = solverDescriptorSet_;
    writes[index].dstBinding = index;
    writes[index].descriptorCount = 1;
    writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[index].pBufferInfo = &infos[index];
  }
  infos[32] = buffers_[18]->descriptorInfo();
  writes[32] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  writes[32].dstSet = presentDescriptorSet_;
  writes[32].dstBinding = 0;
  writes[32].descriptorCount = 1;
  writes[32].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[32].pBufferInfo = &infos[32];
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageView = outputImage_;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  writes[33] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  writes[33].dstSet = presentDescriptorSet_;
  writes[33].dstBinding = 1;
  writes[33].descriptorCount = 1;
  writes[33].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  writes[33].pImageInfo = &imageInfo;
  vkUpdateDescriptorSets(
      device_.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void MacLiveVolume::createPipelines() {
  VkPushConstantRange solverRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(MacPass)};
  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &solverDescriptorLayout_;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &solverRange;
  check(vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &solverPipelineLayout_),
        "vkCreatePipelineLayout MAC solver");
  VkPushConstantRange presentRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PresentPush)};
  layoutInfo.pSetLayouts = &presentDescriptorLayout_;
  layoutInfo.pPushConstantRanges = &presentRange;
  check(vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &presentPipelineLayout_),
        "vkCreatePipelineLayout MAC present");
  solverPipeline_ = createComputePipeline(
      device_.device(), solverPipelineLayout_,
      std::filesystem::path{ENGINE_DIR} / "shaders/vulkax_mac_projection.comp.spv");
  presentPipeline_ = createComputePipeline(
      device_.device(), presentPipelineLayout_,
      std::filesystem::path{ENGINE_DIR} / "shaders/vulkax_mac_present.comp.spv");
}

void MacLiveVolume::record(VkCommandBuffer commandBuffer, float deltaSeconds) {
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, solverPipeline_);
  vkCmdBindDescriptorSets(
      commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, solverPipelineLayout_, 0, 1,
      &solverDescriptorSet_, 0, nullptr);
  vkCmdFillBuffer(commandBuffer, buffers_[22]->getBuffer(), 0, VK_WHOLE_SIZE, 0);
  vkCmdFillBuffer(commandBuffer, buffers_[26]->getBuffer(), 0, VK_WHOLE_SIZE, 0);
  vkCmdFillBuffer(commandBuffer, buffers_[27]->getBuffer(), 0, VK_WHOLE_SIZE, 0);
  VkMemoryBarrier transferBarrier{
      VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      nullptr,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
  vkCmdPipelineBarrier(
      commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0, 1, &transferBarrier, 0, nullptr, 0, nullptr);
  MacPass push{};
  push.dt = std::clamp(deltaSeconds, 1.0f / 240.0f, 1.0f / 30.0f);
  push.outputWidth = outputExtent_.width;
  push.outputHeight = outputExtent_.height;
  const uint32_t groupsX = (kNx + 3u) / 4u;
  const uint32_t groupsY = (kNy + 3u) / 4u;
  const uint32_t groupsZ = (kNz + 3u) / 4u;
  const VkMemoryBarrier storageBarrier{
      VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      nullptr,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
  const auto dispatch = [&](uint32_t pass, float parity) {
    push.pass = pass;
    push.parity = parity;
    vkCmdPushConstants(
        commandBuffer, solverPipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
        sizeof(push), &push);
    vkCmdDispatch(commandBuffer, groupsX, groupsY, groupsZ);
    vkCmdPipelineBarrier(
        commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &storageBarrier, 0, nullptr, 0, nullptr);
  };
  dispatch(20, 0.0f);
  dispatch(18, 0.0f);
  dispatch(19, 0.0f);
  dispatch(10, 0.0f);
  dispatch(11, 0.0f);
  dispatch(0, 0.0f);
  dispatch(12, 0.0f);
  dispatch(13, 0.0f);
  dispatch(1, 0.0f);
  constexpr uint32_t pressureIterations = 40;
  constexpr uint32_t coarseIterations = 12;
  for (uint32_t iteration = 0; iteration < pressureIterations / 2u; ++iteration)
    dispatch(2, static_cast<float>(iteration & 1u));
  dispatch(14, 0.0f);
  for (uint32_t iteration = 0; iteration < coarseIterations; ++iteration)
    dispatch(15, static_cast<float>(iteration & 1u));
  dispatch(16, static_cast<float>(coarseIterations & 1u));
  for (uint32_t iteration = pressureIterations / 2u; iteration < pressureIterations; ++iteration)
    dispatch(2, static_cast<float>(iteration & 1u));
  dispatch(3, static_cast<float>(pressureIterations & 1u));
  dispatch(4, 0.0f);
  dispatch(5, 0.0f);
  dispatch(6, 0.0f);
  dispatch(7, 0.0f);
  dispatch(9, 0.0f);
  dispatch(17, 0.0f);
  push.pass = 8;
  push.parity = 0.0f;
  vkCmdPushConstants(
      commandBuffer, solverPipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
      sizeof(push), &push);
  vkCmdDispatch(
      commandBuffer, (outputExtent_.width + 3u) / 4u,
      (outputExtent_.height + 3u) / 4u, 1);
  vkCmdPipelineBarrier(
      commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &storageBarrier, 0, nullptr, 0, nullptr);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, presentPipeline_);
  vkCmdBindDescriptorSets(
      commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, presentPipelineLayout_, 0, 1,
      &presentDescriptorSet_, 0, nullptr);
  const PresentPush present{outputExtent_.width, outputExtent_.height};
  vkCmdPushConstants(
      commandBuffer, presentPipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
      sizeof(present), &present);
  vkCmdDispatch(
      commandBuffer, (outputExtent_.width + 15u) / 16u,
      (outputExtent_.height + 15u) / 16u, 1);
}

void MacLiveVolume::destroyVulkanObjects() {
  for (auto& buffer : buffers_) buffer.reset();
  if (presentPipeline_ != VK_NULL_HANDLE)
    vkDestroyPipeline(device_.device(), presentPipeline_, nullptr);
  if (solverPipeline_ != VK_NULL_HANDLE)
    vkDestroyPipeline(device_.device(), solverPipeline_, nullptr);
  if (presentPipelineLayout_ != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(device_.device(), presentPipelineLayout_, nullptr);
  if (solverPipelineLayout_ != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(device_.device(), solverPipelineLayout_, nullptr);
  if (descriptorPool_ != VK_NULL_HANDLE)
    vkDestroyDescriptorPool(device_.device(), descriptorPool_, nullptr);
  if (presentDescriptorLayout_ != VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(device_.device(), presentDescriptorLayout_, nullptr);
  if (solverDescriptorLayout_ != VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(device_.device(), solverDescriptorLayout_, nullptr);
}

}  // namespace vulkax::sim
