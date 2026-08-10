#include <vulkan/vulkan.h>

#include "runtime_paths.hpp"
#include "vulkax/gpu/vk_result.hpp"
#include "vulkax/gpu/vulkan_compute_context.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct alignas(16) Parameters {
  uint32_t width = 0;
  uint32_t height = 0;
  float time = 0.0f;
  float amplitude = 1.0f;
  float wavenumber = 2.0f;
  float angularFrequency = 3.0f;
  float padding[2]{};
};
static_assert(sizeof(Parameters) == 32);

std::vector<char> readFile(const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  if (!file) throw std::runtime_error("could not read shader: " + path.string());
  const auto size = file.tellg();
  if (size <= 0) throw std::runtime_error("shader is empty: " + path.string());
  std::vector<char> data(static_cast<size_t>(size));
  file.seekg(0);
  file.read(data.data(), size);
  if (!file) throw std::runtime_error("could not read complete shader: " + path.string());
  return data;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    uint32_t width = 128;
    uint32_t height = 72;
    std::filesystem::path output = "docs/results/physics_studio_current/gpu_wave";
    bool generated = false;
    for (int index = 1; index < argc; ++index) {
      const std::string option{argv[index]};
      if (option == "--width" && index + 1 < argc)
        width = std::stoul(argv[++index]);
      else if (option == "--height" && index + 1 < argc)
        height = std::stoul(argv[++index]);
      else if (option == "--output" && index + 1 < argc)
        output = argv[++index];
      else if (option == "--generated")
        generated = true;
      else
        throw std::invalid_argument(
            "usage: vulkax-compute [--generated --width N --height N --output PATH]");
    }
    if (width == 0 || height == 0) throw std::invalid_argument("field extent must be positive");

    vulkax::gpu::VulkanComputeContext context{"Vulkax Physics Compute"};
    const VkDevice device = context.device();

    const Parameters parameters{width, height, 0.5f, 1.0f, 2.0f, 3.0f, {0.0f, 0.0f}};
    const VkDeviceSize valuesBytes = static_cast<VkDeviceSize>(width) * height * sizeof(float);
    auto field = context.createHostBuffer(valuesBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto uniform = context.createHostBuffer(sizeof(Parameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniform.write(std::as_bytes(std::span{&parameters, 1}));

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    vulkax::gpu::checkVk(
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout),
        "create Vulkax compute descriptor layout");

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    try {
      VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
      pipelineLayoutInfo.setLayoutCount = 1;
      pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
      vulkax::gpu::checkVk(
          vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
          "create Vulkax compute pipeline layout");

      const auto shaderPath = generated
#ifdef VULKAX_GENERATED_WAVE_SPIRV
          ? std::filesystem::path{VULKAX_GENERATED_WAVE_SPIRV}
#else
          ? lve::resolveRuntimeResource("shaders/vulkax_generated_wave_field.comp.spv")
#endif
          : lve::resolveRuntimeResource("shaders/vulkax_wave_field.comp.spv");
      const auto code = readFile(shaderPath);
      VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      shaderInfo.codeSize = code.size();
      shaderInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
      vulkax::gpu::checkVk(
          vkCreateShaderModule(device, &shaderInfo, nullptr, &shader),
          "create Vulkax compute shader module");

      VkComputePipelineCreateInfo computeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
      computeInfo.stage = {
          VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          nullptr,
          0,
          VK_SHADER_STAGE_COMPUTE_BIT,
          shader,
          "main",
          nullptr};
      computeInfo.layout = pipelineLayout;
      vulkax::gpu::checkVk(
          vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &pipeline),
          "create Vulkax compute pipeline");

      VkDescriptorPoolSize poolSizes[2]{
          {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
      VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      poolInfo.maxSets = 1;
      poolInfo.poolSizeCount = 2;
      poolInfo.pPoolSizes = poolSizes;
      vulkax::gpu::checkVk(
          vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool),
          "create Vulkax compute descriptor pool");

      VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      setInfo.descriptorPool = pool;
      setInfo.descriptorSetCount = 1;
      setInfo.pSetLayouts = &descriptorLayout;
      VkDescriptorSet set = VK_NULL_HANDLE;
      vulkax::gpu::checkVk(
          vkAllocateDescriptorSets(device, &setInfo, &set),
          "allocate Vulkax compute descriptor set");
      VkDescriptorBufferInfo fieldInfo{field.handle(), 0, valuesBytes};
      VkDescriptorBufferInfo uniformInfo{uniform.handle(), 0, sizeof(Parameters)};
      VkWriteDescriptorSet writes[2]{};
      writes[0] = {
          VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          nullptr,
          set,
          0,
          0,
          1,
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          nullptr,
          &fieldInfo,
          nullptr};
      writes[1] = {
          VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          nullptr,
          set,
          1,
          0,
          1,
          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          nullptr,
          &uniformInfo,
          nullptr};
      vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

      context.submitAndWait([&](VkCommandBuffer command) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(
            command,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout,
            0,
            1,
            &set,
            0,
            nullptr);
        vkCmdDispatch(command, (width + 15) / 16, (height + 15) / 16, 1);
      });

      std::vector<float> values(static_cast<size_t>(width) * height);
      field.read(std::as_writable_bytes(std::span{values}));
      double mse = 0.0;
      double maxError = 0.0;
      for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
          const double coordinate =
              (static_cast<double>(x) / std::max(1u, width - 1)) * 8.0 - 4.0;
          const double reference = std::sin(2.0 * coordinate - 3.0 * 0.5);
          const double error =
              std::abs(values[static_cast<size_t>(y) * width + x] - reference);
          mse += error * error;
          maxError = std::max(maxError, error);
        }
      }
      mse /= values.size();
      std::filesystem::create_directories(output);
      std::ofstream report{output / "gpu_wave_agreement.json"};
      report << "{\n  \"measurement_class\": \""
             << (generated ? "vulkan_ast_generated_compute_readback"
                           : "vulkan_compute_readback")
             << "\",\n  \"device\": \"" << context.deviceName()
             << "\",\n  \"width\": " << width
             << ",\n  \"height\": " << height
             << ",\n  \"mse\": " << mse
             << ",\n  \"max_error\": " << maxError << "\n}\n";
      std::cout << "Vulkan compute device: " << context.deviceName()
                << "\nMSE=" << mse << " max error=" << maxError << '\n';

      vkDestroyDescriptorPool(device, pool, nullptr);
      pool = VK_NULL_HANDLE;
      vkDestroyPipeline(device, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
      vkDestroyShaderModule(device, shader, nullptr);
      shader = VK_NULL_HANDLE;
      vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
      pipelineLayout = VK_NULL_HANDLE;
      vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
      descriptorLayout = VK_NULL_HANDLE;
      return maxError < 1e-5 ? 0 : 3;
    } catch (...) {
      if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
      if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
      if (shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, shader, nullptr);
      if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
      if (descriptorLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
      throw;
    }
  } catch (const std::exception& error) {
    std::cerr << "vulkax-compute: " << error.what() << '\n';
    return 1;
  }
}
