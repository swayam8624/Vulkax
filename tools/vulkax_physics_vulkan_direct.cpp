#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "beacon/benchmark_config.hpp"
#include "lve_device.hpp"
#include "lve_pipeline.hpp"
#include "lve_renderer.hpp"
#include "lve_window.hpp"

#if defined(VULKAX_HAS_OPENEXR)
#include <OpenEXR/ImfRgbaFile.h>
#endif

namespace {

struct alignas(16) DirectFrameConstants {
  float timeSeconds = 0.0f;
  float width = 1.0f;
  float height = 1.0f;
  float exposure = 1.0f;
  float mode = 0.0f;
  float mass = 1.0f;
  float diskGain = 1.0f;
  float cameraScale = 1.0f;
  float spin = 0.0f;
  float sampleIndex = 0.0f;
  float observerInclination = 1.25f;
  float bundleScale = 1.0f;
};
static_assert(sizeof(DirectFrameConstants) == 48);

float halfToFloat(uint16_t value) {
  const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16u;
  uint32_t exponent = (value >> 10u) & 0x1fu;
  uint32_t mantissa = value & 0x03ffu;
  uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa != 0) {
      exponent = 1;
      while ((mantissa & 0x0400u) == 0) {
        mantissa <<= 1u;
        --exponent;
      }
      mantissa &= 0x03ffu;
      bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
    } else {
      bits = sign;
    }
  } else if (exponent == 31) {
    bits = sign | 0x7f800000u | (mantissa << 13u);
  } else {
    bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
  }
  float result = 0.0f;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

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

class DirectPhysicsPresenter final {
 public:
  DirectPhysicsPresenter(
      uint32_t frameLimit,
      bool schwarzschild,
      bool kerr,
      float spin,
      std::optional<std::filesystem::path> exportPath)
      : frameLimit_{frameLimit},
        schwarzschild_{schwarzschild},
        kerr_{kerr},
        spin_{spin},
        exportPath_{std::move(exportPath)},
        window_{1280, 720, "Vulkax Physics Studio - Direct Vulkan"},
        device_{window_, benchmarkConfig_},
        renderer_{window_, device_} {
    createDescriptorsAndPipelines();
    createTimestampQueryPool();
  }

  ~DirectPhysicsPresenter() {
    if (device_.device() != VK_NULL_HANDLE) vkDeviceWaitIdle(device_.device());
    destroyWaveImage();
    if (computePipeline_ != VK_NULL_HANDLE)
      vkDestroyPipeline(device_.device(), computePipeline_, nullptr);
    if (timestampPool_ != VK_NULL_HANDLE)
      vkDestroyQueryPool(device_.device(), timestampPool_, nullptr);
    if (computePipelineLayout_ != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device_.device(), computePipelineLayout_, nullptr);
    if (graphicsPipelineLayout_ != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device_.device(), graphicsPipelineLayout_, nullptr);
    if (descriptorPool_ != VK_NULL_HANDLE)
      vkDestroyDescriptorPool(device_.device(), descriptorPool_, nullptr);
    if (computeDescriptorLayout_ != VK_NULL_HANDLE)
      vkDestroyDescriptorSetLayout(device_.device(), computeDescriptorLayout_, nullptr);
    if (graphicsDescriptorLayout_ != VK_NULL_HANDLE)
      vkDestroyDescriptorSetLayout(device_.device(), graphicsDescriptorLayout_, nullptr);
  }

  void run() {
    const auto begin = std::chrono::steady_clock::now();
    uint32_t renderedFrames = 0;
    while (!window_.shouldClose() && (frameLimit_ == 0 || renderedFrames < frameLimit_)) {
      glfwPollEvents();
      ensureWaveImage(renderExtent(window_.getExtent()));
      const auto now = std::chrono::steady_clock::now();
      const float timeSeconds = std::chrono::duration<float>(now - begin).count();
      if (auto commandBuffer = renderer_.beginFrame()) {
        const DirectFrameConstants constants{
            timeSeconds,
            static_cast<float>(waveExtent_.width),
            static_cast<float>(waveExtent_.height),
            1.0f,
            kerr_ ? 2.0f : (schwarzschild_ ? 1.0f : 0.0f),
            1.0f,
            1.0f,
            1.0f,
            spin_,
            static_cast<float>((schwarzschild_ || kerr_) ? accumulatedSamples_ : 0u),
            1.25f,
            1.0f};
        dispatchWave(commandBuffer, constants);
        renderer_.beginSwapChainRenderPass(commandBuffer);
        pipeline_->bind(commandBuffer);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            graphicsPipelineLayout_,
            0,
            1,
            &graphicsDescriptorSet_,
            0,
            nullptr);
        vkCmdPushConstants(
            commandBuffer,
            graphicsPipelineLayout_,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(constants),
            &constants);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        writeRenderTimestamp(commandBuffer);
        renderer_.endSwapChainRenderPass(commandBuffer);
        renderer_.endFrame();
        if (schwarzschild_ || kerr_) ++accumulatedSamples_;
        ++renderedFrames;
      }
    }
    renderer_.waitForLastSubmittedFrame();
    reportGpuTiming();
    if (exportPath_) captureHdr(*exportPath_);
    const char* modeName = kerr_ ? "Kerr " : (schwarzschild_ ? "Schwarzschild " : "wave ");
    std::cout << "Vulkax direct Vulkan " << modeName << "compute-to-present completed "
              << renderedFrames << " frame(s)\n";
  }

  void hideWindowForSmoke() { glfwHideWindow(window_.getGLFWwindow()); }

 private:
  void createDescriptorsAndPipelines() {
    VkDescriptorSetLayoutBinding computeBinding{};
    computeBinding.binding = 0;
    computeBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    computeBinding.descriptorCount = 1;
    computeBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo computeLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    computeLayoutInfo.bindingCount = 1;
    computeLayoutInfo.pBindings = &computeBinding;
    check(
        vkCreateDescriptorSetLayout(
            device_.device(),
            &computeLayoutInfo,
            nullptr,
            &computeDescriptorLayout_),
        "vkCreateDescriptorSetLayout compute");

    VkDescriptorSetLayoutBinding graphicsBinding{};
    graphicsBinding.binding = 0;
    graphicsBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    graphicsBinding.descriptorCount = 1;
    graphicsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo graphicsLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    graphicsLayoutInfo.bindingCount = 1;
    graphicsLayoutInfo.pBindings = &graphicsBinding;
    check(
        vkCreateDescriptorSetLayout(
            device_.device(),
            &graphicsLayoutInfo,
            nullptr,
            &graphicsDescriptorLayout_),
        "vkCreateDescriptorSetLayout graphics");

    VkDescriptorPoolSize poolSizes[2]{
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    check(
        vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &descriptorPool_),
        "vkCreateDescriptorPool");
    const VkDescriptorSetLayout setLayouts[2]{computeDescriptorLayout_, graphicsDescriptorLayout_};
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = descriptorPool_;
    setInfo.descriptorSetCount = 2;
    setInfo.pSetLayouts = setLayouts;
    VkDescriptorSet sets[2]{};
    check(vkAllocateDescriptorSets(device_.device(), &setInfo, sets), "vkAllocateDescriptorSets");
    computeDescriptorSet_ = sets[0];
    graphicsDescriptorSet_ = sets[1];

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.size = sizeof(DirectFrameConstants);
    VkPipelineLayoutCreateInfo computePipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    computePipelineInfo.setLayoutCount = 1;
    computePipelineInfo.pSetLayouts = &computeDescriptorLayout_;
    computePipelineInfo.pushConstantRangeCount = 1;
    computePipelineInfo.pPushConstantRanges = &range;
    check(
        vkCreatePipelineLayout(
            device_.device(),
            &computePipelineInfo,
            nullptr,
            &computePipelineLayout_),
        "vkCreatePipelineLayout compute");

    const auto computeCode =
        readBinary(std::filesystem::path{ENGINE_DIR} / "shaders/vulkax_direct_wave.comp.spv");
    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = computeCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(computeCode.data());
    VkShaderModule module = VK_NULL_HANDLE;
    check(
        vkCreateShaderModule(device_.device(), &moduleInfo, nullptr, &module),
        "vkCreateShaderModule compute");
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        0,
        VK_SHADER_STAGE_COMPUTE_BIT,
        module,
        "main",
        nullptr};
    pipelineInfo.layout = computePipelineLayout_;
    const VkResult computeResult = vkCreateComputePipelines(
        device_.device(),
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &computePipeline_);
    vkDestroyShaderModule(device_.device(), module, nullptr);
    check(computeResult, "vkCreateComputePipelines");

    range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkPipelineLayoutCreateInfo graphicsPipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    graphicsPipelineInfo.setLayoutCount = 1;
    graphicsPipelineInfo.pSetLayouts = &graphicsDescriptorLayout_;
    graphicsPipelineInfo.pushConstantRangeCount = 1;
    graphicsPipelineInfo.pPushConstantRanges = &range;
    check(
        vkCreatePipelineLayout(
            device_.device(),
            &graphicsPipelineInfo,
            nullptr,
            &graphicsPipelineLayout_),
        "vkCreatePipelineLayout graphics");
    lve::PipelineConfigInfo config{};
    lve::LvePipeline::defaultPipelineConfigInfo(config);
    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.renderPass = renderer_.getSwapChainRenderPass();
    config.pipelineLayout = graphicsPipelineLayout_;
    pipeline_ = std::make_unique<lve::LvePipeline>(
        device_,
        "shaders/vulkax_direct_wave.vert.spv",
        "shaders/vulkax_direct_wave.frag.spv",
        config);
  }

  [[nodiscard]] VkExtent2D renderExtent(VkExtent2D drawableExtent) const {
    if (!schwarzschild_ && !kerr_) return drawableExtent;
    // Five Kerr rays plus spectral transfer are substantially more expensive
    // than the single Schwarzschild trace. Keep each Metal command buffer
    // below the macOS interactivity watchdog and reconstruct through the
    // graphics sampler while progressive samples converge.
    const uint32_t maximumWidth = kerr_ ? 384u : 512u;
    if (drawableExtent.width <= maximumWidth) return drawableExtent;
    const float scale = static_cast<float>(maximumWidth) / static_cast<float>(drawableExtent.width);
    return {
        maximumWidth,
        std::max(1u, static_cast<uint32_t>(std::lround(drawableExtent.height * scale)))};
  }

  void createTimestampQueryPool() {
    const auto capabilities = device_.getCapabilities();
    if (!capabilities.timestampQueries) return;
    VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 3;
    if (vkCreateQueryPool(device_.device(), &queryInfo, nullptr, &timestampPool_) == VK_SUCCESS) {
      timestampPeriod_ = capabilities.properties.limits.timestampPeriod;
    }
  }

  void ensureWaveImage(VkExtent2D extent) {
    if (extent.width == 0 || extent.height == 0) return;
    if (waveImage_ != VK_NULL_HANDLE && extent.width == waveExtent_.width &&
        extent.height == waveExtent_.height)
      return;
    vkDeviceWaitIdle(device_.device());  // Resize-only destruction; never executed in the frame
                                         // loop at a fixed extent.
    destroyWaveImage();
    waveExtent_ = extent;
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    device_.createImageWithInfo(
        imageInfo,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        waveImage_,
        waveImageMemory_);
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = waveImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    check(
        vkCreateImageView(device_.device(), &viewInfo, nullptr, &waveImageView_),
        "vkCreateImageView");
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    check(
        vkCreateSampler(device_.device(), &samplerInfo, nullptr, &waveSampler_),
        "vkCreateSampler");
    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageView = waveImageView_;
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo sampledInfo{};
    sampledInfo.sampler = waveSampler_;
    sampledInfo.imageView = waveImageView_;
    sampledInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writes[2]{};
    writes[0] = {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        nullptr,
        computeDescriptorSet_,
        0,
        0,
        1,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        &storageInfo,
        nullptr,
        nullptr};
    writes[1] = {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        nullptr,
        graphicsDescriptorSet_,
        0,
        0,
        1,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        &sampledInfo,
        nullptr,
        nullptr};
    vkUpdateDescriptorSets(device_.device(), 2, writes, 0, nullptr);
    waveImageInitialized_ = false;
    accumulatedSamples_ = 0;
  }

  void dispatchWave(VkCommandBuffer commandBuffer, const DirectFrameConstants& constants) {
    if (timestampPool_ != VK_NULL_HANDLE) {
      vkCmdResetQueryPool(commandBuffer, timestampPool_, 0, 3);
      vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampPool_, 0);
    }
    VkImageMemoryBarrier toCompute{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toCompute.srcAccessMask =
        waveImageInitialized_ ? (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT) : 0;
    toCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    toCompute.oldLayout =
        waveImageInitialized_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    toCompute.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toCompute.image = waveImage_;
    toCompute.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toCompute.subresourceRange.levelCount = 1;
    toCompute.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        commandBuffer,
        waveImageInitialized_
            ? (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toCompute);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        computePipelineLayout_,
        0,
        1,
        &computeDescriptorSet_,
        0,
        nullptr);
    vkCmdPushConstants(
        commandBuffer,
        computePipelineLayout_,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(constants),
        &constants);
    vkCmdDispatch(commandBuffer, (waveExtent_.width + 15) / 16, (waveExtent_.height + 15) / 16, 1);
    if (timestampPool_ != VK_NULL_HANDLE) {
      vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampPool_, 1);
    }
    VkImageMemoryBarrier toSample{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toSample.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toSample.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toSample.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toSample.image = waveImage_;
    toSample.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toSample.subresourceRange.levelCount = 1;
    toSample.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toSample);
    waveImageInitialized_ = true;
  }

  void writeRenderTimestamp(VkCommandBuffer commandBuffer) {
    if (timestampPool_ != VK_NULL_HANDLE) {
      vkCmdWriteTimestamp(
          commandBuffer,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          timestampPool_,
          2);
    }
  }

  void reportGpuTiming() {
    if (timestampPool_ == VK_NULL_HANDLE || timestampPeriod_ <= 0.0f) return;
    uint64_t ticks[3]{};
    if (vkGetQueryPoolResults(
            device_.device(),
            timestampPool_,
            0,
            3,
            sizeof(ticks),
            ticks,
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS ||
        ticks[1] < ticks[0] || ticks[2] < ticks[0]) {
      return;
    }
    constexpr double kNanosecondsPerMillisecond = 1'000'000.0;
    const double computeMs =
        static_cast<double>(ticks[1] - ticks[0]) * timestampPeriod_ / kNanosecondsPerMillisecond;
    const double frameMs =
        static_cast<double>(ticks[2] - ticks[0]) * timestampPeriod_ / kNanosecondsPerMillisecond;
    std::cout << "Vulkax Vulkan timestamps: compute=" << computeMs << " ms gpu-frame=" << frameMs
              << " ms\n";
  }

  void captureHdr(const std::filesystem::path& outputPath) {
#if defined(VULKAX_HAS_OPENEXR)
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(waveExtent_.width) * waveExtent_.height * 4u * sizeof(uint16_t);
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    try {
      device_.createBuffer(
          bytes,
          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          readback,
          readbackMemory);
      VkCommandBuffer commandBuffer = device_.beginSingleTimeCommands();
      VkImageMemoryBarrier toCopy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      toCopy.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      toCopy.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
      toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      toCopy.image = waveImage_;
      toCopy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      toCopy.subresourceRange.levelCount = 1;
      toCopy.subresourceRange.layerCount = 1;
      vkCmdPipelineBarrier(
          commandBuffer,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          0,
          0,
          nullptr,
          0,
          nullptr,
          1,
          &toCopy);
      VkBufferImageCopy copy{};
      copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      copy.imageSubresource.layerCount = 1;
      copy.imageExtent = {waveExtent_.width, waveExtent_.height, 1};
      vkCmdCopyImageToBuffer(
          commandBuffer,
          waveImage_,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          readback,
          1,
          &copy);
      VkImageMemoryBarrier restore{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      restore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      restore.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      restore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      restore.newLayout = VK_IMAGE_LAYOUT_GENERAL;
      restore.image = waveImage_;
      restore.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      restore.subresourceRange.levelCount = 1;
      restore.subresourceRange.layerCount = 1;
      vkCmdPipelineBarrier(
          commandBuffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0,
          0,
          nullptr,
          0,
          nullptr,
          1,
          &restore);
      device_.endSingleTimeCommands(commandBuffer);

      check(
          vkMapMemory(device_.device(), readbackMemory, 0, bytes, 0, &mapped),
          "vkMapMemory HDR capture");
      const auto* encoded = static_cast<const uint16_t*>(mapped);
      std::vector<OPENEXR_IMF_NAMESPACE::Rgba> pixels(
          static_cast<size_t>(waveExtent_.width) * waveExtent_.height);
      float minimumLuminance = std::numeric_limits<float>::max();
      float maximumLuminance = 0.0f;
      for (size_t pixel = 0; pixel < pixels.size(); ++pixel) {
        const float red = halfToFloat(encoded[pixel * 4 + 0]);
        const float green = halfToFloat(encoded[pixel * 4 + 1]);
        const float blue = halfToFloat(encoded[pixel * 4 + 2]);
        const float alpha = halfToFloat(encoded[pixel * 4 + 3]);
        if (!(std::isfinite(red) && std::isfinite(green) && std::isfinite(blue) &&
              std::isfinite(alpha))) {
          throw std::runtime_error("direct Vulkan HDR capture contains a non-finite sample");
        }
        const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
        minimumLuminance = std::min(minimumLuminance, luminance);
        maximumLuminance = std::max(maximumLuminance, luminance);
        pixels[pixel].r = red;
        pixels[pixel].g = green;
        pixels[pixel].b = blue;
        pixels[pixel].a = alpha;
      }
      vkUnmapMemory(device_.device(), readbackMemory);
      mapped = nullptr;
      if (minimumLuminance > 1e-4f || maximumLuminance < 1e-3f ||
          maximumLuminance <= minimumLuminance + 1e-3f) {
        throw std::runtime_error("direct Vulkan HDR capture lacks expected shadow/radiance range");
      }
      if (!outputPath.parent_path().empty())
        std::filesystem::create_directories(outputPath.parent_path());
      OPENEXR_IMF_NAMESPACE::RgbaOutputFile output(
          outputPath.string().c_str(),
          static_cast<int>(waveExtent_.width),
          static_cast<int>(waveExtent_.height),
          OPENEXR_IMF_NAMESPACE::WRITE_RGBA);
      output.setFrameBuffer(pixels.data(), 1, static_cast<int>(waveExtent_.width));
      output.writePixels(static_cast<int>(waveExtent_.height));
      vkDestroyBuffer(device_.device(), readback, nullptr);
      vkFreeMemory(device_.device(), readbackMemory, nullptr);
      std::cout << "Exported direct Vulkan HDR EXR: " << outputPath << " luminance=["
                << minimumLuminance << ", " << maximumLuminance << "]\n";
    } catch (...) {
      if (mapped != nullptr) vkUnmapMemory(device_.device(), readbackMemory);
      if (readback != VK_NULL_HANDLE) vkDestroyBuffer(device_.device(), readback, nullptr);
      if (readbackMemory != VK_NULL_HANDLE) vkFreeMemory(device_.device(), readbackMemory, nullptr);
      throw;
    }
#else
    static_cast<void>(outputPath);
    throw std::runtime_error("direct HDR export unavailable: OpenEXR was not found at build time");
#endif
  }

  void destroyWaveImage() {
    if (waveSampler_ != VK_NULL_HANDLE) vkDestroySampler(device_.device(), waveSampler_, nullptr);
    if (waveImageView_ != VK_NULL_HANDLE)
      vkDestroyImageView(device_.device(), waveImageView_, nullptr);
    if (waveImage_ != VK_NULL_HANDLE) vkDestroyImage(device_.device(), waveImage_, nullptr);
    if (waveImageMemory_ != VK_NULL_HANDLE)
      vkFreeMemory(device_.device(), waveImageMemory_, nullptr);
    waveSampler_ = VK_NULL_HANDLE;
    waveImageView_ = VK_NULL_HANDLE;
    waveImage_ = VK_NULL_HANDLE;
    waveImageMemory_ = VK_NULL_HANDLE;
    waveExtent_ = {};
    waveImageInitialized_ = false;
    accumulatedSamples_ = 0;
  }

  lve::beacon::BenchmarkConfig benchmarkConfig_{};
  uint32_t frameLimit_ = 0;
  bool schwarzschild_ = false;
  bool kerr_ = false;
  float spin_ = 0.75f;
  std::optional<std::filesystem::path> exportPath_;
  lve::LveWindow window_;
  lve::LveDevice device_;
  lve::LveRenderer renderer_;
  VkDescriptorSetLayout computeDescriptorLayout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout graphicsDescriptorLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  VkDescriptorSet computeDescriptorSet_ = VK_NULL_HANDLE;
  VkDescriptorSet graphicsDescriptorSet_ = VK_NULL_HANDLE;
  VkPipelineLayout computePipelineLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout graphicsPipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline computePipeline_ = VK_NULL_HANDLE;
  std::unique_ptr<lve::LvePipeline> pipeline_;
  VkImage waveImage_ = VK_NULL_HANDLE;
  VkDeviceMemory waveImageMemory_ = VK_NULL_HANDLE;
  VkImageView waveImageView_ = VK_NULL_HANDLE;
  VkSampler waveSampler_ = VK_NULL_HANDLE;
  VkExtent2D waveExtent_{};
  bool waveImageInitialized_ = false;
  uint32_t accumulatedSamples_ = 0;
  VkQueryPool timestampPool_ = VK_NULL_HANDLE;
  float timestampPeriod_ = 0.0f;
};

uint32_t parseFrameLimit(
    int argc,
    char** argv,
    bool& smoke,
    bool& schwarzschild,
    bool& kerr,
    float& spin,
    std::optional<std::filesystem::path>& exportPath) {
  uint32_t frameLimit = 0;
  for (int index = 1; index < argc; ++index) {
    const std::string option{argv[index]};
    if (option == "--frames" && index + 1 < argc) {
      frameLimit = static_cast<uint32_t>(std::stoul(argv[++index]));
    } else if (option == "--smoke") {
      smoke = true;
      if (frameLimit == 0) frameLimit = 3;
    } else if (option == "--black-hole") {
      schwarzschild = true;
      kerr = false;
    } else if (option == "--kerr") {
      kerr = true;
      schwarzschild = false;
    } else if (option == "--spin" && index + 1 < argc) {
      spin = std::stof(argv[++index]);
      if (!(std::abs(spin) < 1.0f)) throw std::invalid_argument("--spin requires |spin| < 1");
      kerr = true;
      schwarzschild = false;
    } else if (option == "--output" && index + 1 < argc) {
      exportPath = std::filesystem::path{argv[++index]};
    } else {
      throw std::invalid_argument(
          "usage: vulkax-physics-vulkan-direct [--frames N] [--smoke] [--black-hole|--kerr] "
          "[--spin (-1,1)] [--output frame.exr]");
    }
  }
  return frameLimit;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    bool smoke = false;
    bool schwarzschild = false;
    bool kerr = false;
    float spin = 0.75f;
    std::optional<std::filesystem::path> exportPath;
    uint32_t frameLimit = parseFrameLimit(argc, argv, smoke, schwarzschild, kerr, spin, exportPath);
    if (exportPath && frameLimit == 0) frameLimit = 1;
    DirectPhysicsPresenter presenter{frameLimit, schwarzschild, kerr, spin, std::move(exportPath)};
    if (smoke) presenter.hideWindowForSmoke();
    presenter.run();
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-physics-vulkan-direct: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
