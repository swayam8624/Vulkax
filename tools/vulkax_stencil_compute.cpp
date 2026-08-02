#include "vulkax/physics/stencil_ir.hpp"
#include "vulkax/physics/vulkan_resource_arena.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct alignas(16) Parameters {
  uint32_t width = 16;
  uint32_t height = 16;
  uint32_t depth = 16;
  float dt = 0.00025f;
  float time = 0.0f;
  float coefficient0 = 0.15f;
  float coefficient1 = 0.05f;
  float coefficient2 = 0.0367f;
  float coefficient3 = 0.0649f;
  float padding[3]{};
};
static_assert(sizeof(Parameters) == 48);

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string{operation} + " failed: " + std::to_string(result));
  }
}

std::vector<char> readFile(const std::filesystem::path& path) {
  std::ifstream stream{path, std::ios::binary | std::ios::ate};
  if (!stream) throw std::runtime_error("could not read generated shader: " + path.string());
  const std::streamsize size = stream.tellg();
  std::vector<char> bytes(static_cast<size_t>(size));
  stream.seekg(0);
  stream.read(bytes.data(), size);
  return bytes;
}

bool hasPortabilityEnumeration() {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
  return std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
    return std::string{extension.extensionName} == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
  });
}

bool hasDeviceExtension(VkPhysicalDevice physical, const char* name) {
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
  return std::any_of(extensions.begin(), extensions.end(), [&](const auto& extension) {
    return std::string{extension.extensionName} == name;
  });
}

vulkax::physics::ScalarEvolutionProgram makeReferenceProgram() {
  using namespace vulkax::physics;
  PhysicsModel model{};
  model.name = "generated-diffusion";
  model.domain.minimum = {0.0, 0.0, 0.0};
  model.domain.maximum = {1.0, 1.0, 1.0};
  model.domain.resolution = {16, 16, 16};
  model.fields = {{"u", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter}};
  model.boundaries = {{"u", BoundaryKind::Periodic, std::nullopt}};
  model.solver.timestepSeconds = 0.00025;
  const auto lowered = lowerScalarEvolutionProgram(
      model, "u", vulkax::equation::parseScalarExpression(
          "diffusivity * laplacian(u) - decay * u"),
      {"diffusivity", "decay"});
  if (!lowered.valid()) throw std::runtime_error("could not lower CPU diffusion reference");
  return *lowered.program;
}

vulkax::physics::CoupledScalarEvolutionProgram makeCoupledReferenceProgram() {
  using namespace vulkax::physics;
  PhysicsModel model{};
  model.name = "generated-gray-scott";
  model.domain.minimum = {0.0, 0.0, 0.0};
  model.domain.maximum = {1.0, 1.0, 1.0};
  model.domain.resolution = {16, 16, 16};
  model.fields = {
      {"a", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter},
      {"b", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter}};
  model.boundaries = {
      {"a", BoundaryKind::Periodic, std::nullopt},
      {"b", BoundaryKind::Periodic, std::nullopt}};
  model.solver.timestepSeconds = 0.00025;
  const auto lowered = lowerCoupledScalarEvolutionProgram(
      model,
      {
          {"a", vulkax::equation::parseScalarExpression(
                    "diffusion_a * laplacian(a) - a*b*b + feed*(1-a)")},
          {"b", vulkax::equation::parseScalarExpression(
                    "diffusion_b * laplacian(b) + a*b*b - (feed+kill)*b")},
      },
      {"diffusion_a", "diffusion_b", "feed", "kill"});
  if (!lowered.valid()) throw std::runtime_error("could not lower CPU coupled reference");
  return *lowered.program;
}

}  // namespace

int main(int argc, char** argv) {
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  try {
    const bool coupled = argc == 2 && std::string{argv[1]} == "--coupled";
    if (argc > 2 || (argc == 2 && !coupled)) {
      throw std::invalid_argument("usage: vulkax-stencil-compute [--coupled]");
    }
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Vulkax Generated Stencil Compute";
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application;
    std::vector<const char*> instanceExtensions;
    if (hasPortabilityEnumeration()) {
      instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    uint32_t physicalCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr), "vkEnumeratePhysicalDevices");
    if (physicalCount == 0) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, physicalDevices.data()), "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    for (VkPhysicalDevice candidate : physicalDevices) {
      uint32_t queueCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
      std::vector<VkQueueFamilyProperties> queues(queueCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
      for (uint32_t index = 0; index < queueCount; ++index) {
        if ((queues[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
          physical = candidate;
          queueFamily = index;
          break;
        }
      }
      if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) throw std::runtime_error("no Vulkan compute queue");
    VkPhysicalDeviceProperties physicalProperties{};
    vkGetPhysicalDeviceProperties(physical, &physicalProperties);
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    std::vector<const char*> deviceExtensions;
    if (hasDeviceExtension(physical, "VK_KHR_portability_subset")) {
      deviceExtensions.push_back("VK_KHR_portability_subset");
    }
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    const Parameters parameters{};
    const size_t cellCount = static_cast<size_t>(parameters.width) * parameters.height * parameters.depth;
    const size_t valueCount = cellCount * (coupled ? 2u : 1u);
    const VkDeviceSize valueBytes = valueCount * sizeof(float);
    std::vector<float> initial(valueCount, coupled ? 1.0f : 0.0f);
    for (uint32_t z = 0; z < parameters.depth; ++z) {
      for (uint32_t y = 0; y < parameters.height; ++y) {
        for (uint32_t x = 0; x < parameters.width; ++x) {
          const float px = (static_cast<float>(x) + 0.5f) / parameters.width;
          const size_t cell =
              (static_cast<size_t>(z) * parameters.height + y) * parameters.width + x;
          if (coupled) {
            const float py = (static_cast<float>(y) + 0.5f) / parameters.height;
            const float pz = (static_cast<float>(z) + 0.5f) / parameters.depth;
            const float radius2 = (px - 0.5f) * (px - 0.5f) +
                (py - 0.5f) * (py - 0.5f) + (pz - 0.5f) * (pz - 0.5f);
            const float seed = std::exp(-radius2 / 0.0125f);
            initial[cell] = 1.0f - 0.45f * seed;
            initial[cellCount + cell] = 0.25f * seed;
          } else {
            initial[cell] = std::sin(2.0f * std::numbers::pi_v<float> * px);
          }
        }
      }
    }
    using vulkax::physics::VulkanResourceArena;
    using vulkax::physics::VulkanResourceBinding;
    using vulkax::physics::VulkanResourcePlan;
    const VkMemoryPropertyFlags hostVisible =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VulkanResourcePlan resourcePlan{};
    resourcePlan.canonicalHash = coupled ? 0x4752415953434f54ull : 0x444946465553494full;
    resourcePlan.bindings.resize(3);
    for (uint32_t index = 0; index < 2; ++index) {
      VulkanResourceBinding& binding = resourcePlan.bindings[index];
      binding.binding = index;
      binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      binding.bufferBytes = valueBytes;
      binding.memoryProperties = hostVisible;
    }
    VulkanResourceBinding& parameterBinding = resourcePlan.bindings[2];
    parameterBinding.binding = 2;
    parameterBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    parameterBinding.bufferBytes = sizeof(parameters);
    parameterBinding.bufferUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    parameterBinding.memoryProperties = hostVisible;
    auto resources =
        std::make_unique<VulkanResourceArena>(physical, device, std::move(resourcePlan));
    resources->uploadBuffer(0, std::as_bytes(std::span{initial}));
    resources->uploadBuffer(2, std::as_bytes(std::span{&parameters, 1u}));
    const auto shaderCode = readFile(
        coupled ? VULKAX_GENERATED_GRAY_SCOTT_SPIRV : VULKAX_GENERATED_DIFFUSION_SPIRV);
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = shaderCode.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());
    VkShaderModule shader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule");
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                          VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", nullptr};
    pipelineInfo.layout = resources->pipelineLayout();
    VkPipeline pipeline = VK_NULL_HANDLE;
    check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline),
          "vkCreateComputePipelines");

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = queueFamily;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device, &commandInfo, &command), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    const VkDescriptorSet descriptorSet = resources->descriptorSet();
    vkCmdBindDescriptorSets(
        command, VK_PIPELINE_BIND_POINT_COMPUTE, resources->pipelineLayout(), 0, 1,
        &descriptorSet, 0, nullptr);
    vkCmdDispatch(command, 2, 2, 4);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
    check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    std::vector<float> gpu(valueCount);
    resources->downloadBuffer(1, std::as_writable_bytes(std::span{gpu}));
    std::vector<float> reference;
    if (coupled) {
      const std::map<std::string, std::vector<float>> coupledInput{
          {"a", std::vector<float>(
                    initial.begin(), initial.begin() + static_cast<std::ptrdiff_t>(cellCount))},
          {"b", std::vector<float>(
                    initial.begin() + static_cast<std::ptrdiff_t>(cellCount), initial.end())}};
      const auto coupledReference = vulkax::physics::executeCoupledScalarEvolution3D(
          makeCoupledReferenceProgram(), coupledInput,
          parameters.dt, parameters.time,
          {{"diffusion_a", parameters.coefficient0},
           {"diffusion_b", parameters.coefficient1},
           {"feed", parameters.coefficient2},
           {"kill", parameters.coefficient3}});
      reference.reserve(valueCount);
      reference.insert(reference.end(), coupledReference.at("a").begin(), coupledReference.at("a").end());
      reference.insert(reference.end(), coupledReference.at("b").begin(), coupledReference.at("b").end());
    } else {
      reference = vulkax::physics::executeScalarEvolution3D(
          makeReferenceProgram(), initial, parameters.dt, parameters.time,
          {{"diffusivity", parameters.coefficient0}, {"decay", parameters.coefficient1}});
    }
    double mse = 0.0;
    double maximumError = 0.0;
    for (size_t index = 0; index < valueCount; ++index) {
      const double error = std::abs(static_cast<double>(gpu[index]) - reference[index]);
      mse += error * error;
      maximumError = std::max(maximumError, error);
    }
    mse /= valueCount;
    std::cout << "Generated " << (coupled ? "coupled " : "")
              << "stencil Vulkan device: " << physicalProperties.deviceName
              << "\nMSE=" << mse << " max error=" << maximumError << '\n';

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, shader, nullptr);
    resources.reset();
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return maximumError < 2e-6 ? 0 : 3;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-stencil-compute: " << error.what() << '\n';
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    return 1;
  }
}
