#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) throw std::runtime_error(std::string{operation} + " failed: " + std::to_string(result));
}

uint32_t memoryType(VkPhysicalDevice device, uint32_t mask, VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(device, &memory);
  for (uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
    if ((mask & (1u << index)) && (memory.memoryTypes[index].propertyFlags & flags) == flags) return index;
  }
  throw std::runtime_error("no compatible Vulkan memory type");
}

struct Buffer { VkBuffer handle = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; };

Buffer makeBuffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage) {
  VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  create.size = size;
  create.usage = usage;
  create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  Buffer buffer{};
  check(vkCreateBuffer(device, &create, nullptr, &buffer.handle), "vkCreateBuffer");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = memoryType(
      physical, requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  check(vkAllocateMemory(device, &allocation, nullptr, &buffer.memory), "vkAllocateMemory");
  check(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0), "vkBindBufferMemory");
  return buffer;
}

std::vector<char> readFile(const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  if (!file) throw std::runtime_error("could not read shader: " + path.string());
  const auto size = file.tellg();
  std::vector<char> data(static_cast<size_t>(size));
  file.seekg(0);
  file.read(data.data(), size);
  return data;
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

}  // namespace

int main(int argc, char** argv) {
  try {
    uint32_t width = 128, height = 72;
    std::filesystem::path output = "docs/results/physics_studio_current/gpu_wave";
    bool generated = false;
    for (int index = 1; index < argc; ++index) {
      const std::string option{argv[index]};
      if (option == "--width" && index + 1 < argc) width = std::stoul(argv[++index]);
      else if (option == "--height" && index + 1 < argc) height = std::stoul(argv[++index]);
      else if (option == "--output" && index + 1 < argc) output = argv[++index];
      else if (option == "--generated") generated = true;
      else throw std::invalid_argument("usage: vulkax-compute [--generated --width N --height N --output PATH]");
    }
    if (width == 0 || height == 0) throw std::invalid_argument("field extent must be positive");

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Vulkax Physics Compute";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    std::vector<const char*> extensions;
    if (hasPortabilityEnumeration()) {
      extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    VkInstance instance = VK_NULL_HANDLE;
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    uint32_t deviceCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");
    if (deviceCount == 0) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    check(vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data()), "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t family = 0;
    for (const auto candidate : physicalDevices) {
      uint32_t count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
      std::vector<VkQueueFamilyProperties> families(count);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
      for (uint32_t queue = 0; queue < count; ++queue) {
        if (families[queue].queueFlags & VK_QUEUE_COMPUTE_BIT) { physical = candidate; family = queue; break; }
      }
      if (physical) break;
    }
    if (!physical) throw std::runtime_error("no Vulkan compute queue");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device = VK_NULL_HANDLE;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    VkQueue queue{}; vkGetDeviceQueue(device, family, 0, &queue);

    const Parameters parameters{width, height, 0.5f, 1.0f, 2.0f, 3.0f, {0.0f, 0.0f}};
    const VkDeviceSize valuesBytes = static_cast<VkDeviceSize>(width) * height * sizeof(float);
    Buffer field = makeBuffer(physical, device, valuesBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer uniform = makeBuffer(physical, device, sizeof(Parameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    void* mapped = nullptr;
    check(vkMapMemory(device, uniform.memory, 0, sizeof(Parameters), 0, &mapped), "vkMapMemory uniform");
    std::memcpy(mapped, &parameters, sizeof(parameters));
    vkUnmapMemory(device, uniform.memory);

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2; layoutInfo.pBindings = bindings;
    VkDescriptorSetLayout descriptorLayout{};
    check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout), "vkCreateDescriptorSetLayout");
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1; pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    VkPipelineLayout pipelineLayout{};
    check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout");
    const auto shaderPath = generated
#ifdef VULKAX_GENERATED_WAVE_SPIRV
        ? std::filesystem::path{VULKAX_GENERATED_WAVE_SPIRV}
#else
        ? std::filesystem::path{ENGINE_DIR} / "shaders/vulkax_generated_wave_field.comp.spv"
#endif
        : std::filesystem::path{ENGINE_DIR} / "shaders/vulkax_wave_field.comp.spv";
    const auto code = readFile(shaderPath);
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = code.size(); shaderInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shader{}; check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule");
    VkComputePipelineCreateInfo computeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    computeInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", nullptr};
    computeInfo.layout = pipelineLayout;
    VkPipeline pipeline{}; check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &pipeline), "vkCreateComputePipelines");

    VkDescriptorPoolSize poolSizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1; poolInfo.poolSizeCount = 2; poolInfo.pPoolSizes = poolSizes;
    VkDescriptorPool pool{}; check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = pool; setInfo.descriptorSetCount = 1; setInfo.pSetLayouts = &descriptorLayout;
    VkDescriptorSet set{}; check(vkAllocateDescriptorSets(device, &setInfo, &set), "vkAllocateDescriptorSets");
    VkDescriptorBufferInfo fieldInfo{field.handle, 0, valuesBytes};
    VkDescriptorBufferInfo uniformInfo{uniform.handle, 0, sizeof(Parameters)};
    VkWriteDescriptorSet writes[2]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &fieldInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uniformInfo, nullptr};
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = family;
    VkCommandPool commandPool{}; check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool; commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandInfo.commandBufferCount = 1;
    VkCommandBuffer command{}; check(vkAllocateCommandBuffers(device, &commandInfo, &command), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
    vkCmdDispatch(command, (width + 15) / 16, (height + 15) / 16, 1);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence{}; check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
    check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    std::vector<float> values(static_cast<size_t>(width) * height);
    check(vkMapMemory(device, field.memory, 0, valuesBytes, 0, &mapped), "vkMapMemory field");
    std::memcpy(values.data(), mapped, static_cast<size_t>(valuesBytes)); vkUnmapMemory(device, field.memory);
    double mse = 0.0, maxError = 0.0;
    for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
      const double coordinate = (static_cast<double>(x) / std::max(1u, width - 1)) * 8.0 - 4.0;
      const double reference = std::sin(2.0 * coordinate - 3.0 * 0.5);
      const double error = std::abs(values[static_cast<size_t>(y) * width + x] - reference);
      mse += error * error; maxError = std::max(maxError, error);
    }
    mse /= values.size();
    std::filesystem::create_directories(output);
    std::ofstream report{output / "gpu_wave_agreement.json"};
    report << "{\n  \"measurement_class\": \"" <<
           (generated ? "vulkan_ast_generated_compute_readback" : "vulkan_compute_readback") <<
           "\",\n  \"device\": \"" << properties.deviceName
           << "\",\n  \"width\": " << width << ",\n  \"height\": " << height
           << ",\n  \"mse\": " << mse << ",\n  \"max_error\": " << maxError << "\n}\n";
    std::cout << "Vulkan compute device: " << properties.deviceName << "\nMSE=" << mse << " max error=" << maxError << '\n';
    vkDestroyFence(device, fence, nullptr); vkDestroyCommandPool(device, commandPool, nullptr); vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr); vkDestroyShaderModule(device, shader, nullptr); vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr); vkDestroyBuffer(device, field.handle, nullptr); vkFreeMemory(device, field.memory, nullptr);
    vkDestroyBuffer(device, uniform.handle, nullptr); vkFreeMemory(device, uniform.memory, nullptr); vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    return maxError < 1e-5 ? 0 : 3;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-compute: " << error.what() << '\n'; return 1;
  }
}
