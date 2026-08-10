#include "vulkax/sim/sparse_brick_storage.hpp"
#include "runtime_paths.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct alignas(16) SparseParameters {
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint32_t brickSize;
  uint32_t brickCountX;
  uint32_t brickCountY;
  uint32_t brickCountZ;
  uint32_t channels;
  float diffusion;
  float padding[3]{};
};
static_assert(sizeof(SparseParameters) == 48);

struct Buffer {
  VkBuffer handle = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize bytes = 0;
};

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
  }
}

bool supportsPortabilityEnumeration() {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
  return std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
    return std::string(extension.extensionName) ==
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
  });
}

uint32_t hostVisibleMemoryType(VkPhysicalDevice physical, uint32_t mask) {
  VkPhysicalDeviceMemoryProperties properties{};
  vkGetPhysicalDeviceMemoryProperties(physical, &properties);
  constexpr VkMemoryPropertyFlags required =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
    if ((mask & (1u << index)) != 0u &&
        (properties.memoryTypes[index].propertyFlags & required) == required) {
      return index;
    }
  }
  throw std::runtime_error("host-visible coherent Vulkan memory unavailable");
}

Buffer makeBuffer(
    VkPhysicalDevice physical,
    VkDevice device,
    VkDeviceSize bytes,
    VkBufferUsageFlags usage) {
  Buffer result{};
  result.bytes = bytes;
  VkBufferCreateInfo createInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  createInfo.size = bytes;
  createInfo.usage = usage;
  createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateBuffer(device, &createInfo, nullptr, &result.handle), "vkCreateBuffer");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, result.handle, &requirements);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = hostVisibleMemoryType(physical, requirements.memoryTypeBits);
  check(vkAllocateMemory(device, &allocation, nullptr, &result.memory), "vkAllocateMemory");
  check(vkBindBufferMemory(device, result.handle, result.memory, 0), "vkBindBufferMemory");
  return result;
}

void upload(VkDevice device, const Buffer& buffer, const void* source, size_t bytes) {
  if (bytes > buffer.bytes) throw std::invalid_argument("sparse Vulkan upload exceeds buffer");
  void* mapped = nullptr;
  check(vkMapMemory(device, buffer.memory, 0, buffer.bytes, 0, &mapped), "vkMapMemory");
  std::memcpy(mapped, source, bytes);
  if (bytes < buffer.bytes) {
    std::memset(static_cast<std::byte*>(mapped) + bytes, 0,
                static_cast<size_t>(buffer.bytes - bytes));
  }
  vkUnmapMemory(device, buffer.memory);
}

std::vector<float> download(VkDevice device, const Buffer& buffer) {
  std::vector<float> result(static_cast<size_t>(buffer.bytes / sizeof(float)));
  void* mapped = nullptr;
  check(vkMapMemory(device, buffer.memory, 0, buffer.bytes, 0, &mapped), "vkMapMemory");
  std::memcpy(result.data(), mapped, static_cast<size_t>(buffer.bytes));
  vkUnmapMemory(device, buffer.memory);
  return result;
}

std::vector<char> readBinary(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("shader missing: " + path.string());
  std::vector<char> result(static_cast<size_t>(file.tellg()));
  file.seekg(0);
  file.read(result.data(), static_cast<std::streamsize>(result.size()));
  return result;
}

void destroyBuffer(VkDevice device, const Buffer& buffer) {
  vkDestroyBuffer(device, buffer.handle, nullptr);
  vkFreeMemory(device, buffer.memory, nullptr);
}

size_t pageIndex(
    const vulkax::sim::SparseBrickStorage& storage,
    uint32_t bx,
    uint32_t by,
    uint32_t bz) {
  return (static_cast<size_t>(bz) * storage.brickCountY() + by) *
      storage.brickCountX() + bx;
}

uint32_t sparseIndex(
    const vulkax::sim::SparseBrickStorage& storage,
    uint32_t x,
    uint32_t y,
    uint32_t z,
    uint32_t channel) {
  const uint32_t brickSize = storage.config().brickSize;
  const uint32_t slot = storage.pageTable()[pageIndex(
      storage, x / brickSize, y / brickSize, z / brickSize)];
  if (slot == vulkax::sim::SparseBrickStorage::kInactiveSlot) return slot;
  const uint32_t localX = x % brickSize;
  const uint32_t localY = y % brickSize;
  const uint32_t localZ = z % brickSize;
  const uint32_t local = (localZ * brickSize + localY) * brickSize + localX;
  return (slot * storage.cellsPerBrick() + local) * storage.config().channels + channel;
}

std::vector<float> diffuseReference(
    const vulkax::sim::SparseBrickStorage& storage,
    std::vector<float> source,
    uint32_t steps,
    float coefficient) {
  std::vector<float> target(source.size());
  const auto config = storage.config();
  const std::array<std::array<int, 3>, 6> neighbours{{
      {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
      {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};
  for (uint32_t step = 0; step < steps; ++step) {
    std::fill(target.begin(), target.end(), 0.0f);
    for (uint32_t z = 0; z < config.depth; ++z) {
      for (uint32_t y = 0; y < config.height; ++y) {
        for (uint32_t x = 0; x < config.width; ++x) {
          for (uint32_t channel = 0; channel < config.channels; ++channel) {
            const uint32_t index = sparseIndex(storage, x, y, z, channel);
            if (index == vulkax::sim::SparseBrickStorage::kInactiveSlot) continue;
            const float center = source[index];
            float result = center;
            for (const auto& offset : neighbours) {
              const int nx = static_cast<int>(x) + offset[0];
              const int ny = static_cast<int>(y) + offset[1];
              const int nz = static_cast<int>(z) + offset[2];
              if (nx < 0 || ny < 0 || nz < 0 || nx >= static_cast<int>(config.width) ||
                  ny >= static_cast<int>(config.height) || nz >= static_cast<int>(config.depth)) {
                continue;
              }
              const uint32_t neighbour = sparseIndex(
                  storage, static_cast<uint32_t>(nx), static_cast<uint32_t>(ny),
                  static_cast<uint32_t>(nz), channel);
              if (neighbour != vulkax::sim::SparseBrickStorage::kInactiveSlot)
                result += coefficient * (source[neighbour] - center);
            }
            target[index] = result;
          }
        }
      }
    }
    source.swap(target);
  }
  return source;
}

double channelSum(const std::vector<float>& payload, uint32_t channels, uint32_t channel) {
  double result = 0.0;
  for (size_t index = channel; index < payload.size(); index += channels) result += payload[index];
  return result;
}

}  // namespace

int main() {
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  try {
    using vulkax::sim::SparseBrickStorage;
    SparseBrickStorage storage({16, 12, 8, 4, 2});
    for (uint32_t z = 0; z < 4; ++z) {
      for (uint32_t y = 4; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
          const float dx = static_cast<float>(x) - 3.5f;
          const float dy = static_cast<float>(y) - 5.5f;
          const float dz = static_cast<float>(z) - 1.5f;
          const float density = std::exp(-(dx * dx + dy * dy + dz * dz) / 5.0f);
          storage.setCell(x, y, z, 0, density);
          storage.setCell(x, y, z, 1, 0.5f * density);
        }
      }
    }
    if (!storage.validate() || storage.stats().residentBricks != 2u)
      throw std::runtime_error("sparse CPU fixture did not produce two compact bricks");
    constexpr uint32_t steps = 12;
    constexpr float diffusion = 0.07f;
    const std::vector<float> reference =
        diffuseReference(storage, storage.payload(), steps, diffusion);

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Vulkax Sparse Brick Compute";
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application;
    std::vector<const char*> extensions;
    if (supportsPortabilityEnumeration()) {
      extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      instanceInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    uint32_t physicalCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr),
          "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> physicals(physicalCount);
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, physicals.data()),
          "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    for (VkPhysicalDevice candidate : physicals) {
      uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
      for (uint32_t family = 0; family < familyCount; ++family) {
        if ((families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u) {
          physical = candidate;
          queueFamily = family;
          break;
        }
      }
      if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) throw std::runtime_error("Vulkan compute queue unavailable");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    const VkDeviceSize pageBytes = storage.pageTable().size() * sizeof(uint32_t);
    const VkDeviceSize payloadBytes = storage.payload().size() * sizeof(float);
    Buffer page = makeBuffer(physical, device, pageBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer payloadA = makeBuffer(physical, device, payloadBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer payloadB = makeBuffer(physical, device, payloadBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer uniform = makeBuffer(physical, device, sizeof(SparseParameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    upload(device, page, storage.pageTable().data(), static_cast<size_t>(pageBytes));
    upload(device, payloadA, storage.payload().data(), static_cast<size_t>(payloadBytes));
    std::vector<float> zero(storage.payload().size(), 0.0f);
    upload(device, payloadB, zero.data(), static_cast<size_t>(payloadBytes));
    const SparseParameters parameters{
        storage.config().width, storage.config().height, storage.config().depth,
        storage.config().brickSize, storage.brickCountX(), storage.brickCountY(),
        storage.brickCountZ(), storage.config().channels, diffusion};
    upload(device, uniform, &parameters, sizeof(parameters));

    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    for (uint32_t binding = 0; binding < 3; ++binding)
      bindings[binding] = {binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                           VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout),
          "vkCreateDescriptorSetLayout");
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
          "vkCreatePipelineLayout");
    const auto code = readBinary(
        lve::resolveRuntimeResource("shaders/vulkax_sparse_brick_diffusion.comp.spv"));
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = code.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule");
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                          VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", nullptr};
    pipelineInfo.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline),
          "vkCreateComputePipelines");

    const std::array<VkDescriptorPoolSize, 2> poolSizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool");
    const std::array<VkDescriptorSetLayout, 2> layouts{descriptorLayout, descriptorLayout};
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = pool;
    setInfo.descriptorSetCount = 2;
    setInfo.pSetLayouts = layouts.data();
    std::array<VkDescriptorSet, 2> sets{};
    check(vkAllocateDescriptorSets(device, &setInfo, sets.data()), "vkAllocateDescriptorSets");
    const auto writeSet = [&](VkDescriptorSet set, const Buffer& source, const Buffer& target) {
      const std::array<VkDescriptorBufferInfo, 4> infos{{
          {page.handle, 0, page.bytes}, {source.handle, 0, source.bytes},
          {target.handle, 0, target.bytes}, {uniform.handle, 0, uniform.bytes}}};
      std::array<VkWriteDescriptorSet, 4> writes{};
      for (uint32_t binding = 0; binding < writes.size(); ++binding) {
        writes[binding] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[binding].dstSet = set;
        writes[binding].dstBinding = binding;
        writes[binding].descriptorCount = 1;
        writes[binding].descriptorType = binding == 3u ?
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[binding].pBufferInfo = &infos[binding];
      }
      vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    };
    writeSet(sets[0], payloadA, payloadB);
    writeSet(sets[1], payloadB, payloadA);

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = queueFamily;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool),
          "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device, &commandInfo, &command), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(command, &beginInfo), "vkBeginCommandBuffer");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    const VkMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    for (uint32_t step = 0; step < steps; ++step) {
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1,
                              &sets[step & 1u], 0, nullptr);
      vkCmdDispatch(command, (storage.config().width + 3u) / 4u,
                    (storage.config().height + 3u) / 4u,
                    (storage.config().depth + 3u) / 4u);
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier,
                           0, nullptr, 0, nullptr);
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
    const std::vector<float> gpu = download(device, (steps & 1u) != 0u ? payloadB : payloadA);

    double maximumError = 0.0;
    for (size_t index = 0; index < gpu.size(); ++index)
      maximumError = std::max(maximumError,
          std::abs(static_cast<double>(gpu[index]) - reference[index]));
    double maximumMassError = 0.0;
    for (uint32_t channel = 0; channel < storage.config().channels; ++channel) {
      const double initialMass = channelSum(storage.payload(), storage.config().channels, channel);
      const double gpuMass = channelSum(gpu, storage.config().channels, channel);
      maximumMassError = std::max(maximumMassError, std::abs(gpuMass - initialMass));
    }
    const auto stats = storage.stats();
    const uint64_t denseBytes = stats.denseCells * storage.config().channels * sizeof(float);
    std::cout << "Vulkax Vulkan sparse brick diffusion: " << properties.deviceName
              << " resident=" << stats.residentBricks << '/' << stats.logicalBricks
              << " payload_bytes=" << stats.payloadBytes << " dense_bytes=" << denseBytes
              << " steps=" << steps << " max_error=" << maximumError
              << " mass_error=" << maximumMassError << '\n';

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, shader, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    for (const Buffer& buffer : {page, payloadA, payloadB, uniform}) destroyBuffer(device, buffer);
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
    return maximumError < 2e-6 && maximumMassError < 2e-5 && stats.payloadBytes < denseBytes ? 0 : 3;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-sparse-brick-compute: " << error.what() << '\n';
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    return 1;
  }
}
