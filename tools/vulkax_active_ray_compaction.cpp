#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kRayCount = 1024;
constexpr uint32_t kMaximumLifetime = 17;

struct RayState {
  uint32_t remainingSteps;
  uint32_t completedSteps;
  uint32_t status;
  uint32_t padding;
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
struct Push {
  uint32_t phase;
  uint32_t sourceQueue;
  uint32_t padding[2];
};
static_assert(sizeof(RayState) == 16 && sizeof(Control) == 16 && sizeof(Push) == 16);

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(std::string{operation} + " failed: " + std::to_string(result));
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

uint32_t memoryType(VkPhysicalDevice device, uint32_t mask, VkMemoryPropertyFlags required) {
  VkPhysicalDeviceMemoryProperties properties{};
  vkGetPhysicalDeviceMemoryProperties(device, &properties);
  for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
    if ((mask & (1u << index)) != 0 &&
        (properties.memoryTypes[index].propertyFlags & required) == required)
      return index;
  }
  throw std::runtime_error("no host-visible Vulkan memory type for active-ray test");
}

struct Buffer {
  VkBuffer handle = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize bytes = 0;
};

Buffer makeBuffer(
    VkPhysicalDevice physical, VkDevice device, VkDeviceSize bytes, VkBufferUsageFlags usage) {
  Buffer result{};
  result.bytes = bytes;
  VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  create.size = bytes;
  create.usage = usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateBuffer(device, &create, nullptr, &result.handle), "vkCreateBuffer");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, result.handle, &requirements);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = memoryType(
      physical,
      requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  check(vkAllocateMemory(device, &allocation, nullptr, &result.memory), "vkAllocateMemory");
  check(vkBindBufferMemory(device, result.handle, result.memory, 0), "vkBindBufferMemory");
  return result;
}

template <typename T>
void upload(VkDevice device, const Buffer& buffer, const T* values, size_t count) {
  void* mapped = nullptr;
  check(vkMapMemory(device, buffer.memory, 0, sizeof(T) * count, 0, &mapped), "vkMapMemory upload");
  std::memcpy(mapped, values, sizeof(T) * count);
  vkUnmapMemory(device, buffer.memory);
}

template <typename T>
std::vector<T> download(VkDevice device, const Buffer& buffer, size_t count) {
  std::vector<T> result(count);
  void* mapped = nullptr;
  check(
      vkMapMemory(device, buffer.memory, 0, sizeof(T) * count, 0, &mapped),
      "vkMapMemory download");
  std::memcpy(result.data(), mapped, sizeof(T) * count);
  vkUnmapMemory(device, buffer.memory);
  return result;
}

std::vector<char> readFile(const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  if (!file) throw std::runtime_error("could not read shader " + path.string());
  const auto size = file.tellg();
  std::vector<char> result(static_cast<size_t>(size));
  file.seekg(0);
  file.read(result.data(), size);
  return result;
}

}  // namespace

int main() {
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  std::vector<Buffer> buffers;
  try {
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Vulkax Active Ray Compaction";
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application;
    std::vector<const char*> extensions;
    if (hasPortabilityEnumeration()) {
      extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    uint32_t deviceCount = 0;
    check(
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr),
        "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    check(
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()),
        "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t family = 0;
    for (VkPhysicalDevice candidate : devices) {
      uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
      for (uint32_t index = 0; index < familyCount; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
          physical = candidate;
          family = index;
          break;
        }
      }
      if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) throw std::runtime_error("no Vulkan compute device");
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, family, 0, &queue);

    buffers.push_back(makeBuffer(physical, device, sizeof(RayState) * kRayCount, 0));
    for (int index = 0; index < 4; ++index)
      buffers.push_back(makeBuffer(physical, device, sizeof(uint32_t) * kRayCount, 0));
    buffers.push_back(makeBuffer(physical, device, sizeof(Control), 0));
    buffers.push_back(
        makeBuffer(physical, device, sizeof(IndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));
    std::vector<RayState> initialRays(kRayCount);
    std::vector<uint32_t> active(kRayCount);
    uint64_t expectedSteps = 0;
    for (uint32_t index = 0; index < kRayCount; ++index) {
      initialRays[index].remainingSteps = 1 + index % kMaximumLifetime;
      active[index] = index;
      expectedSteps += initialRays[index].remainingSteps;
    }
    const Control control{kRayCount, 0, kRayCount, 0};
    const IndirectCommand indirect{4, 1, 1};
    upload(device, buffers[0], initialRays.data(), initialRays.size());
    upload(device, buffers[1], active.data(), active.size());
    upload(device, buffers[5], &control, 1);
    upload(device, buffers[6], &indirect, 1);

    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
    for (uint32_t index = 0; index < bindings.size(); ++index)
      bindings[index] =
          {index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptorInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    descriptorInfo.pBindings = bindings.data();
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    check(
        vkCreateDescriptorSetLayout(device, &descriptorInfo, nullptr, &descriptorLayout),
        "vkCreateDescriptorSetLayout");
    VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    check(
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
        "vkCreatePipelineLayout");
    const auto code = readFile(
        std::filesystem::path{ENGINE_DIR} / "shaders/vulkax_active_ray_compaction.comp.spv");
    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &moduleInfo, nullptr, &shader), "vkCreateShaderModule");
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        0,
        VK_SHADER_STAGE_COMPUTE_BIT,
        shader,
        "main",
        nullptr};
    pipelineInfo.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    check(
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline),
        "vkCreateComputePipelines");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &descriptorLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &allocate, &set), "vkAllocateDescriptorSets");
    std::array<VkDescriptorBufferInfo, 7> bufferInfo{};
    std::array<VkWriteDescriptorSet, 7> writes{};
    for (uint32_t index = 0; index < buffers.size(); ++index) {
      bufferInfo[index] = {buffers[index].handle, 0, buffers[index].bytes};
      writes[index] = {
          VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          nullptr,
          set,
          index,
          0,
          1,
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          nullptr,
          &bufferInfo[index],
          nullptr};
    }
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = family;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    check(
        vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool),
        "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocateCommand{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateCommand.commandPool = commandPool;
    allocateCommand.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateCommand.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device, &allocateCommand, &command), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
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
    const VkMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT};
    for (uint32_t iteration = 0; iteration < kMaximumLifetime; ++iteration) {
      Push push{0, iteration % 2, {0, 0}};
      vkCmdPushConstants(
          command,
          pipelineLayout,
          VK_SHADER_STAGE_COMPUTE_BIT,
          0,
          sizeof(push),
          &push);
      vkCmdDispatchIndirect(command, buffers[6].handle, 0);
      vkCmdPipelineBarrier(
          command,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          0,
          1,
          &barrier,
          0,
          nullptr,
          0,
          nullptr);
      push.phase = 1;
      vkCmdPushConstants(
          command,
          pipelineLayout,
          VK_SHADER_STAGE_COMPUTE_BIT,
          0,
          sizeof(push),
          &push);
      vkCmdDispatch(command, 1, 1, 1);
      vkCmdPipelineBarrier(
          command,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
          0,
          1,
          &barrier,
          0,
          nullptr,
          0,
          nullptr);
      push.phase = 2;
      vkCmdPushConstants(
          command,
          pipelineLayout,
          VK_SHADER_STAGE_COMPUTE_BIT,
          0,
          sizeof(push),
          &push);
      vkCmdDispatch(command, 4, 1, 1);
      vkCmdPipelineBarrier(
          command,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
          0,
          1,
          &barrier,
          0,
          nullptr,
          0,
          nullptr);
    }
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
    check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    const auto finalRays = download<RayState>(device, buffers[0], kRayCount);
    const auto finalControl = download<Control>(device, buffers[5], 1).front();
    uint64_t completedSteps = 0;
    const bool allFinished =
        std::all_of(finalRays.begin(), finalRays.end(), [&](const RayState& ray) {
          completedSteps += ray.completedSteps;
          return ray.remainingSteps == 0 && ray.status == 1;
        });
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    std::cout << "Vulkax active-ray GPU compaction passed on " << properties.deviceName
              << ": rays=" << kRayCount << " iterations=" << finalControl.iterations
              << " active=" << finalControl.activeCount << " integrated_steps=" << completedSteps
              << '\n';
    const bool valid = allFinished && finalControl.activeCount == 0 &&
                       finalControl.iterations == kMaximumLifetime &&
                       completedSteps == expectedSteps;

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, shader, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    for (const Buffer& buffer : buffers) {
      vkDestroyBuffer(device, buffer.handle, nullptr);
      vkFreeMemory(device, buffer.memory, nullptr);
    }
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return valid ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-active-rays: " << error.what() << '\n';
    if (device != VK_NULL_HANDLE) {
      for (const Buffer& buffer : buffers) {
        if (buffer.handle != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer.handle, nullptr);
        if (buffer.memory != VK_NULL_HANDLE) vkFreeMemory(device, buffer.memory, nullptr);
      }
      vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    return 1;
  }
}
