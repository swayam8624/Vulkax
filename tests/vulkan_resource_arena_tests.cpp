#include "vulkax/physics/physics_ir.hpp"
#include "vulkax/physics/vulkan_resource_arena.hpp"
#include "vulkax/physics/vulkan_resource_layout.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string{operation} + " failed: " + std::to_string(result));
  }
}

bool hasPortabilityEnumeration() {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
  return std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
    return std::string{extension.extensionName} ==
           VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
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

}  // namespace

int main() {
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  try {
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Vulkax Reflected Resource Arena Test";
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

    uint32_t physicalCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr),
          "vkEnumeratePhysicalDevices count");
    if (physicalCount == 0) {
      vkDestroyInstance(instance, nullptr);
      return 77;
    }
    std::vector<VkPhysicalDevice> devices(physicalCount);
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, devices.data()),
          "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    for (VkPhysicalDevice candidate : devices) {
      uint32_t queueCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
      std::vector<VkQueueFamilyProperties> queues(queueCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
      for (uint32_t index = 0; index < queues.size(); ++index) {
        if ((queues[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
          physical = candidate;
          queueFamily = index;
          break;
        }
      }
      if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) {
      vkDestroyInstance(instance, nullptr);
      return 77;
    }

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

    using namespace vulkax::physics;
    const VkMemoryPropertyFlags hostVisible =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VulkanResourcePlan sharedOwnerPlan{};
    sharedOwnerPlan.bindings.resize(1);
    sharedOwnerPlan.bindings[0].binding = 0;
    sharedOwnerPlan.bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sharedOwnerPlan.bindings[0].bufferBytes = 64;
    sharedOwnerPlan.bindings[0].memoryProperties = hostVisible;
    auto sharedOwner =
        std::make_unique<VulkanResourceArena>(physical, device, std::move(sharedOwnerPlan));
    VulkanResourcePlan sharedViewPlan{};
    sharedViewPlan.bindings.resize(1);
    sharedViewPlan.bindings[0].binding = 0;
    sharedViewPlan.bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sharedViewPlan.bindings[0].bufferBytes = 64;
    const VulkanResourceImport sharedImport{0, 0, sharedOwner->buffer(0)};
    auto sharedView = std::make_unique<VulkanResourceArena>(
        physical, device, std::move(sharedViewPlan), std::span{&sharedImport, 1u});
    assert(sharedView->buffer(0) == sharedOwner->buffer(0));
    bool rejectedImportedUpload = false;
    try {
      const std::array<std::byte, 4> bytes{};
      sharedView->uploadBuffer(0, bytes);
    } catch (const std::invalid_argument&) {
      rejectedImportedUpload = true;
    }
    assert(rejectedImportedUpload);

    // Multiple compatible reflected buffers should share one Vulkan device-memory
    // block while retaining independent aligned offsets and CPU mapping behavior.
    VulkanResourcePlan pooledPlan{};
    pooledPlan.bindings.resize(3);
    for (uint32_t index = 0; index < pooledPlan.bindings.size(); ++index) {
      pooledPlan.bindings[index].binding = index;
      pooledPlan.bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      pooledPlan.bindings[index].bufferBytes = 64 + index * 64;
      pooledPlan.bindings[index].memoryProperties = hostVisible;
    }
    auto pooledArena =
        std::make_unique<VulkanResourceArena>(physical, device, std::move(pooledPlan));
    const VulkanMemoryArenaStats pooledStats = pooledArena->memoryStats();
    assert(pooledStats.suballocationCount == 3);
    assert(pooledStats.blockCount == 1);
    assert(pooledStats.reservedBytes >= pooledStats.resourceBytes);
    const std::array<std::byte, 8> pooledUpload{
        std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
        std::byte{0x55}, std::byte{0x66}, std::byte{0x77}, std::byte{0x7f}};
    pooledArena->uploadBuffer(1, pooledUpload);
    std::array<std::byte, pooledUpload.size()> pooledDownload{};
    pooledArena->downloadBuffer(1, pooledDownload);
    assert(pooledDownload == pooledUpload);

    PhysicsModel model{};
    model.name = "reflected-resource-runtime";
    model.domain.resolution = {8, 6, 4};
    model.fields = {
        {"source", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter},
        {"state", ValueType::Scalar, Dimension::dimensionless(), FieldPlacement::CellCenter}};
    model.initialConditions = {{"source", 0.0}, {"state", 0.0}};
    model.boundaries = {{"state", BoundaryKind::Periodic, std::nullopt}};
    model.passes = {
        {SolverPassKind::AdvectScalar, "write_state", {"source"}, {"state"}},
        {SolverPassKind::DeriveOpticalProperties, "read_state", {"state"}, {"state"}}};
    const auto graph = lowerToSolverGraph(model);
    assert(graph.has_value());
    const ResourceLayout layout = reflectResourceLayout(model, *graph);
    VulkanResourcePlan plan = makeVulkanResourcePlan(layout);
    plan.descriptorSetCount = 2;
    auto arena = std::make_unique<VulkanResourceArena>(physical, device, std::move(plan));
    assert(arena->descriptorSetLayout() != VK_NULL_HANDLE);
    assert(arena->pipelineLayout() != VK_NULL_HANDLE);
    assert(arena->descriptorSet(0) != VK_NULL_HANDLE);
    assert(arena->descriptorSet(1) != VK_NULL_HANDLE);

    const auto stateResource = std::find_if(
        layout.resources.begin(), layout.resources.end(),
        [](const ReflectedResource& resource) { return resource.name == "state"; });
    assert(stateResource != layout.resources.end());
    const uint32_t stateBinding = stateResource->binding;
    assert(stateResource->historyLength == 2);
    assert(arena->image(stateBinding, 0) != VK_NULL_HANDLE);
    assert(arena->imageView(stateBinding, 1) != VK_NULL_HANDLE);
    const uint32_t initialCurrentSlot = arena->physicalHistorySlot(stateBinding, 0);
    const uint32_t initialPreviousSlot = arena->physicalHistorySlot(stateBinding, 1);
    assert(initialCurrentSlot != initialPreviousSlot);

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = queueFamily;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    check(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
          "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device, &commandInfo, &command),
          "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    arena->recordInitialTransitions(command);
    arena->beginPassSequence();
    const VulkanBarrierSummary first = arena->recordPassBarrier(command, layout, 0);
    const VulkanBarrierSummary second = arena->recordPassBarrier(command, layout, 1);
    assert(first.bufferBarriers == 0 && first.imageBarriers == 0);
    assert(second.bufferBarriers == 0 && second.imageBarriers == 1);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
    check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    arena->rotateHistory(1);
    assert(arena->physicalHistorySlot(stateBinding, 0) == initialPreviousSlot);
    assert(arena->physicalHistorySlot(stateBinding, 1) == initialCurrentSlot);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    std::cout << "reflected Vulkan resources device=" << properties.deviceName
              << " bindings=" << layout.resources.size()
              << " history=" << stateResource->historyLength
              << " image_barriers=" << second.imageBarriers
              << " memory_blocks=" << arena->memoryStats().blockCount
              << " suballocations=" << arena->memoryStats().suballocationCount << '\n';

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDeviceWaitIdle(device);
    arena.reset();
    pooledArena.reset();
    sharedView.reset();
    sharedOwner.reset();
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
    vkDestroyInstance(instance, nullptr);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "vulkan-resource-arena-tests: " << error.what() << '\n';
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    return 1;
  }
}
