#include "vulkax/render/gaussian_scheduler.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef VULKAX_GAUSSIAN_SCHEDULE_SPV_PATH
#error "VULKAX_GAUSSIAN_SCHEDULE_SPV_PATH missing"
#endif

namespace vulkax::render {
namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: " +
                                 std::to_string(static_cast<int>(result)));
}

[[nodiscard]] std::vector<std::uint32_t> readSpirv() {
    std::ifstream stream(VULKAX_GAUSSIAN_SCHEDULE_SPV_PATH, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot open Gaussian scheduler SPIR-V shader");
    const std::streamsize byteCount = stream.tellg();
    if (byteCount <= 0 || byteCount % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0)
        throw std::runtime_error("invalid Gaussian scheduler SPIR-V size");
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint32_t> words(
        static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    if (!stream.read(reinterpret_cast<char*>(words.data()), byteCount))
        throw std::runtime_error("Gaussian scheduler SPIR-V read failed");
    return words;
}

[[nodiscard]] bool hasInstanceExtension(const char* name) {
    std::uint32_t count = 0U;
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
          "vkEnumerateInstanceExtensionProperties(count)");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()),
          "vkEnumerateInstanceExtensionProperties(list)");
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

[[nodiscard]] bool hasDeviceExtension(VkPhysicalDevice device, const char* name) {
    std::uint32_t count = 0U;
    check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
          "vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()),
          "vkEnumerateDeviceExtensionProperties(list)");
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

struct VulkanContext {
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    std::uint32_t queueFamily{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};

    ~VulkanContext() {
        if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
        if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    }
};

[[nodiscard]] VulkanContext makeContext() {
    VulkanContext context;
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Vulkax Gaussian Scheduler";
    application.applicationVersion = VK_MAKE_API_VERSION(0, 1, 1, 0);
    application.pEngineName = "Vulkax";
    application.engineVersion = VK_MAKE_API_VERSION(0, 1, 1, 0);
    application.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> instanceExtensions;
    VkInstanceCreateFlags instanceFlags = 0U;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (hasInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.flags = instanceFlags;
    instanceInfo.pApplicationInfo = &application;
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data();
    check(vkCreateInstance(&instanceInfo, nullptr, &context.instance), "vkCreateInstance");

    std::uint32_t physicalCount = 0U;
    check(vkEnumeratePhysicalDevices(context.instance, &physicalCount, nullptr),
          "vkEnumeratePhysicalDevices(count)");
    if (physicalCount == 0U) throw std::runtime_error("no Vulkan physical device for Gaussian scheduler");
    std::vector<VkPhysicalDevice> devices(physicalCount);
    check(vkEnumeratePhysicalDevices(context.instance, &physicalCount, devices.data()),
          "vkEnumeratePhysicalDevices(list)");
    for (VkPhysicalDevice device : devices) {
        std::uint32_t familyCount = 0U;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
        for (std::uint32_t family = 0U; family < familyCount; ++family) {
            if ((families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U) {
                context.physicalDevice = device;
                context.queueFamily = family;
                break;
            }
        }
        if (context.physicalDevice != VK_NULL_HANDLE) break;
    }
    if (context.physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("no Vulkan compute queue for Gaussian scheduler");

    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = context.queueFamily;
    queueInfo.queueCount = 1U;
    queueInfo.pQueuePriorities = &priority;
    std::vector<const char*> deviceExtensions;
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    if (hasDeviceExtension(context.physicalDevice, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
        deviceExtensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1U;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();
    check(vkCreateDevice(context.physicalDevice, &deviceInfo, nullptr, &context.device), "vkCreateDevice");
    vkGetDeviceQueue(context.device, context.queueFamily, 0U, &context.queue);
    vkGetPhysicalDeviceMemoryProperties(context.physicalDevice, &context.memoryProperties);
    return context;
}

struct HostBuffer {
    VkDevice device{VK_NULL_HANDLE};
    VkBuffer buffer{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkDeviceSize allocationSize{};
    bool coherent{false};
    void* mapped{nullptr};

    HostBuffer() = default;
    HostBuffer(const HostBuffer&) = delete;
    HostBuffer& operator=(const HostBuffer&) = delete;
    HostBuffer(HostBuffer&& other) noexcept
        : device(other.device), buffer(other.buffer), memory(other.memory),
          allocationSize(other.allocationSize), coherent(other.coherent), mapped(other.mapped) {
        other.buffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.mapped = nullptr;
    }
    ~HostBuffer() {
        if (mapped != nullptr && memory != VK_NULL_HANDLE) vkUnmapMemory(device, memory);
        if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    }
};

[[nodiscard]] HostBuffer makeHostBuffer(
    const VulkanContext& context,
    VkDeviceSize bytes) {
    HostBuffer result;
    result.device = context.device;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = std::max<VkDeviceSize>(bytes, 4U);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(context.device, &bufferInfo, nullptr, &result.buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device, result.buffer, &requirements);
    std::uint32_t fallback = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t selected = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t index = 0U; index < context.memoryProperties.memoryTypeCount; ++index) {
        if ((requirements.memoryTypeBits & (1U << index)) == 0U) continue;
        const VkMemoryPropertyFlags flags = context.memoryProperties.memoryTypes[index].propertyFlags;
        if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0U) continue;
        if (fallback == std::numeric_limits<std::uint32_t>::max()) fallback = index;
        if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U) {
            selected = index;
            result.coherent = true;
            break;
        }
    }
    if (selected == std::numeric_limits<std::uint32_t>::max()) selected = fallback;
    if (selected == std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("no host-visible Vulkan memory for Gaussian scheduler");

    VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = selected;
    check(vkAllocateMemory(context.device, &allocationInfo, nullptr, &result.memory), "vkAllocateMemory");
    result.allocationSize = requirements.size;
    check(vkBindBufferMemory(context.device, result.buffer, result.memory, 0U), "vkBindBufferMemory");
    check(vkMapMemory(context.device, result.memory, 0U, requirements.size, 0U, &result.mapped), "vkMapMemory");
    return result;
}

void flushIfNeeded(const HostBuffer& buffer) {
    if (buffer.coherent) return;
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = buffer.memory;
    range.offset = 0U;
    range.size = VK_WHOLE_SIZE;
    check(vkFlushMappedMemoryRanges(buffer.device, 1U, &range), "vkFlushMappedMemoryRanges");
}

void invalidateIfNeeded(const HostBuffer& buffer) {
    if (buffer.coherent) return;
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = buffer.memory;
    range.offset = 0U;
    range.size = VK_WHOLE_SIZE;
    check(vkInvalidateMappedMemoryRanges(buffer.device, 1U, &range), "vkInvalidateMappedMemoryRanges");
}

struct ScheduleParameters {
    std::uint32_t projectedCount{};
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::uint32_t referenceCapacity{};
};

} // namespace

std::pair<GaussianTileSchedule, double> scheduleGaussianProjectionVulkan(
    const GaussianNativeProjectionResult& projection) {
    const std::size_t tileCount =
        static_cast<std::size_t>(projection.tileColumns) * projection.tileRows;
    if (projection.projected.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        projection.splatReferences > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        tileCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Vulkan Gaussian scheduler exceeds uint32 capacity");

    auto context = makeContext();
    const VkDeviceSize projectedBytes = static_cast<VkDeviceSize>(
        std::max<std::size_t>(projection.projected.size() * sizeof(GaussianProjectedSplat), 4U));
    const VkDeviceSize offsetsBytes = static_cast<VkDeviceSize>((tileCount + 1U) * sizeof(std::uint32_t));
    const VkDeviceSize cursorsBytes = static_cast<VkDeviceSize>(
        std::max<std::size_t>(tileCount, 1U) * sizeof(std::uint32_t));
    const VkDeviceSize referencesBytes = static_cast<VkDeviceSize>(
        std::max<std::size_t>(projection.splatReferences, 1U) * sizeof(std::uint32_t));
    constexpr VkDeviceSize metadataBytes = 4U * sizeof(std::uint32_t);

    HostBuffer projectedBuffer = makeHostBuffer(context, projectedBytes);
    HostBuffer offsetsBuffer = makeHostBuffer(context, offsetsBytes);
    HostBuffer cursorsBuffer = makeHostBuffer(context, cursorsBytes);
    HostBuffer referencesBuffer = makeHostBuffer(context, referencesBytes);
    HostBuffer metadataBuffer = makeHostBuffer(context, metadataBytes);
    if (!projection.projected.empty()) {
        std::memcpy(projectedBuffer.mapped, projection.projected.data(),
                    projection.projected.size() * sizeof(GaussianProjectedSplat));
    }
    std::memset(offsetsBuffer.mapped, 0, static_cast<std::size_t>(offsetsBytes));
    std::memset(cursorsBuffer.mapped, 0, static_cast<std::size_t>(cursorsBytes));
    std::memset(referencesBuffer.mapped, 0, static_cast<std::size_t>(referencesBytes));
    std::memset(metadataBuffer.mapped, 0, static_cast<std::size_t>(metadataBytes));
    flushIfNeeded(projectedBuffer);
    flushIfNeeded(offsetsBuffer);
    flushIfNeeded(cursorsBuffer);
    flushIfNeeded(referencesBuffer);
    flushIfNeeded(metadataBuffer);

    VkDescriptorSetLayoutBinding bindings[5]{};
    for (std::uint32_t index = 0U; index < 5U; ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[index].descriptorCount = 1U;
        bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 5U;
    layoutInfo.pBindings = bindings;
    VkDescriptorSetLayout descriptorLayout{VK_NULL_HANDLE};
    check(vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &descriptorLayout),
          "vkCreateDescriptorSetLayout");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0U;
    pushRange.size = sizeof(ScheduleParameters);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1U;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1U;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    check(vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
          "vkCreatePipelineLayout");

    const auto words = readSpirv();
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = words.size() * sizeof(std::uint32_t);
    shaderInfo.pCode = words.data();
    VkShaderModule shader{VK_NULL_HANDLE};
    check(vkCreateShaderModule(context.device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule");
    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stage;
    pipelineInfo.layout = pipelineLayout;
    VkPipeline pipeline{VK_NULL_HANDLE};
    check(vkCreateComputePipelines(context.device, VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &pipeline),
          "vkCreateComputePipelines");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5U};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1U;
    poolInfo.poolSizeCount = 1U;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    check(vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = descriptorPool;
    allocateInfo.descriptorSetCount = 1U;
    allocateInfo.pSetLayouts = &descriptorLayout;
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    check(vkAllocateDescriptorSets(context.device, &allocateInfo, &descriptorSet), "vkAllocateDescriptorSets");

    VkDescriptorBufferInfo bufferInfos[5]{};
    bufferInfos[0] = {projectedBuffer.buffer, 0U, projectedBytes};
    bufferInfos[1] = {offsetsBuffer.buffer, 0U, offsetsBytes};
    bufferInfos[2] = {cursorsBuffer.buffer, 0U, cursorsBytes};
    bufferInfos[3] = {referencesBuffer.buffer, 0U, referencesBytes};
    bufferInfos[4] = {metadataBuffer.buffer, 0U, metadataBytes};
    VkWriteDescriptorSet writes[5]{};
    for (std::uint32_t index = 0U; index < 5U; ++index) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = descriptorSet;
        writes[index].dstBinding = index;
        writes[index].descriptorCount = 1U;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[index].pBufferInfo = &bufferInfos[index];
    }
    vkUpdateDescriptorSets(context.device, 5U, writes, 0U, nullptr);

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = context.queueFamily;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool commandPool{VK_NULL_HANDLE};
    check(vkCreateCommandPool(context.device, &commandPoolInfo, nullptr, &commandPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandAllocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandAllocateInfo.commandPool = commandPool;
    commandAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocateInfo.commandBufferCount = 1U;
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    check(vkAllocateCommandBuffers(context.device, &commandAllocateInfo, &commandBuffer), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                            0U, 1U, &descriptorSet, 0U, nullptr);
    const ScheduleParameters parameters{
        static_cast<std::uint32_t>(projection.projected.size()),
        projection.tileColumns,
        projection.tileRows,
        static_cast<std::uint32_t>(projection.splatReferences),
    };
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0U, sizeof(parameters), &parameters);
    vkCmdDispatch(commandBuffer, 1U, 1U, 1U);
    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence{VK_NULL_HANDLE};
    check(vkCreateFence(context.device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1U;
    submitInfo.pCommandBuffers = &commandBuffer;
    const auto start = std::chrono::steady_clock::now();
    check(vkQueueSubmit(context.queue, 1U, &submitInfo, fence), "vkQueueSubmit");
    check(vkWaitForFences(context.device, 1U, &fence, VK_TRUE, 10'000'000'000ULL), "vkWaitForFences");
    const auto stop = std::chrono::steady_clock::now();
    const double milliseconds = std::chrono::duration<double, std::milli>(stop - start).count();

    invalidateIfNeeded(offsetsBuffer);
    invalidateIfNeeded(referencesBuffer);
    invalidateIfNeeded(metadataBuffer);
    const auto* metadata = static_cast<const std::uint32_t*>(metadataBuffer.mapped);
    if (metadata[2] != 0U)
        throw std::runtime_error("Vulkan Gaussian scheduler kernel reported status " + std::to_string(metadata[2]));
    if (metadata[0] > projection.splatReferences)
        throw std::runtime_error("Vulkan Gaussian scheduler returned too many references");

    GaussianTileSchedule schedule;
    schedule.tileSize = projection.tileSize;
    schedule.columns = projection.tileColumns;
    schedule.rows = projection.tileRows;
    schedule.splatReferences = metadata[0];
    schedule.maximumSplatsPerTile = metadata[1];
    schedule.tileOffsets.resize(tileCount + 1U);
    std::memcpy(schedule.tileOffsets.data(), offsetsBuffer.mapped, static_cast<std::size_t>(offsetsBytes));
    schedule.projectedSplatIndices.resize(schedule.splatReferences);
    if (schedule.splatReferences > 0U) {
        std::memcpy(schedule.projectedSplatIndices.data(), referencesBuffer.mapped,
                    schedule.splatReferences * sizeof(std::uint32_t));
    }

    vkDestroyFence(context.device, fence, nullptr);
    vkDestroyCommandPool(context.device, commandPool, nullptr);
    vkDestroyDescriptorPool(context.device, descriptorPool, nullptr);
    vkDestroyPipeline(context.device, pipeline, nullptr);
    vkDestroyShaderModule(context.device, shader, nullptr);
    vkDestroyPipelineLayout(context.device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(context.device, descriptorLayout, nullptr);
    return {std::move(schedule), milliseconds};
}

static_assert(sizeof(ScheduleParameters) == 16U);

} // namespace vulkax::render
