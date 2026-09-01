#include "vulkax/render/gaussian_projection.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef VULKAX_GAUSSIAN_PROJECT_SPV_PATH
#error "VULKAX_GAUSSIAN_PROJECT_SPV_PATH missing"
#endif
#ifndef VULKAX_GAUSSIAN_METADATA_SPV_PATH
#error "VULKAX_GAUSSIAN_METADATA_SPV_PATH missing"
#endif
#ifndef VULKAX_GAUSSIAN_DEPTH_ORDER_SPV_PATH
#error "VULKAX_GAUSSIAN_DEPTH_ORDER_SPV_PATH missing"
#endif
#ifndef VULKAX_GAUSSIAN_FUSED_SCHEDULE_SPV_PATH
#error "VULKAX_GAUSSIAN_FUSED_SCHEDULE_SPV_PATH missing"
#endif

namespace vulkax::render {
namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: " +
                                 std::to_string(static_cast<int>(result)));
}

[[nodiscard]] std::vector<std::uint32_t> readSpirv(const char* path, const char* label) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error(std::string("cannot open ") + label + " SPIR-V shader");
    const std::streamsize byteCount = stream.tellg();
    if (byteCount <= 0 || byteCount % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0)
        throw std::runtime_error(std::string("invalid ") + label + " SPIR-V size");
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint32_t> words(
        static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    if (!stream.read(reinterpret_cast<char*>(words.data()), byteCount))
        throw std::runtime_error(std::string(label) + " SPIR-V read failed");
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
    application.pApplicationName = "Vulkax Fused Gaussian";
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
    if (physicalCount == 0U) throw std::runtime_error("no Vulkan physical device for fused Gaussian execution");
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
        throw std::runtime_error("no Vulkan compute queue for fused Gaussian execution");

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

[[nodiscard]] HostBuffer makeHostBuffer(const VulkanContext& context, VkDeviceSize bytes) {
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
        throw std::runtime_error("no host-visible Vulkan memory for fused Gaussian execution");

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

struct PipelineBundle {
    VkDevice device{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptorLayout{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkShaderModule shader{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};

    PipelineBundle() = default;
    PipelineBundle(const PipelineBundle&) = delete;
    PipelineBundle& operator=(const PipelineBundle&) = delete;
    PipelineBundle(PipelineBundle&& other) noexcept
        : device(other.device), descriptorLayout(other.descriptorLayout),
          pipelineLayout(other.pipelineLayout), shader(other.shader), pipeline(other.pipeline),
          descriptorPool(other.descriptorPool), descriptorSet(other.descriptorSet) {
        other.descriptorLayout = VK_NULL_HANDLE;
        other.pipelineLayout = VK_NULL_HANDLE;
        other.shader = VK_NULL_HANDLE;
        other.pipeline = VK_NULL_HANDLE;
        other.descriptorPool = VK_NULL_HANDLE;
        other.descriptorSet = VK_NULL_HANDLE;
    }
    ~PipelineBundle() {
        if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        if (shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, shader, nullptr);
        if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (descriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    }
};

[[nodiscard]] PipelineBundle makePipeline(
    const VulkanContext& context,
    const char* spirvPath,
    const char* label,
    const std::vector<std::pair<VkBuffer, VkDeviceSize>>& buffers,
    std::uint32_t pushConstantBytes) {
    PipelineBundle result;
    result.device = context.device;
    std::vector<VkDescriptorSetLayoutBinding> bindings(buffers.size());
    for (std::uint32_t index = 0U; index < bindings.size(); ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[index].descriptorCount = 1U;
        bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &result.descriptorLayout),
          "vkCreateDescriptorSetLayout");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0U;
    pushRange.size = pushConstantBytes;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1U;
    pipelineLayoutInfo.pSetLayouts = &result.descriptorLayout;
    if (pushConstantBytes > 0U) {
        pipelineLayoutInfo.pushConstantRangeCount = 1U;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    }
    check(vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr, &result.pipelineLayout),
          "vkCreatePipelineLayout");

    const auto words = readSpirv(spirvPath, label);
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = words.size() * sizeof(std::uint32_t);
    shaderInfo.pCode = words.data();
    check(vkCreateShaderModule(context.device, &shaderInfo, nullptr, &result.shader), "vkCreateShaderModule");
    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = result.shader;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stage;
    pipelineInfo.layout = result.pipelineLayout;
    check(vkCreateComputePipelines(context.device, VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &result.pipeline),
          "vkCreateComputePipelines");

    VkDescriptorPoolSize poolSize{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        static_cast<std::uint32_t>(buffers.size())};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1U;
    poolInfo.poolSizeCount = 1U;
    poolInfo.pPoolSizes = &poolSize;
    check(vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &result.descriptorPool),
          "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = result.descriptorPool;
    allocateInfo.descriptorSetCount = 1U;
    allocateInfo.pSetLayouts = &result.descriptorLayout;
    check(vkAllocateDescriptorSets(context.device, &allocateInfo, &result.descriptorSet),
          "vkAllocateDescriptorSets");

    std::vector<VkDescriptorBufferInfo> infos(buffers.size());
    std::vector<VkWriteDescriptorSet> writes(buffers.size());
    for (std::uint32_t index = 0U; index < buffers.size(); ++index) {
        infos[index] = {buffers[index].first, 0U, buffers[index].second};
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = result.descriptorSet;
        writes[index].dstBinding = index;
        writes[index].descriptorCount = 1U;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[index].pBufferInfo = &infos[index];
    }
    vkUpdateDescriptorSets(context.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0U, nullptr);
    return result;
}

[[nodiscard]] double submitCommands(
    const VulkanContext& context,
    const std::function<void(VkCommandBuffer)>& record) {
    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = context.queueFamily;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool commandPool{VK_NULL_HANDLE};
    check(vkCreateCommandPool(context.device, &commandPoolInfo, nullptr, &commandPool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1U;
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    check(vkAllocateCommandBuffers(context.device, &allocateInfo, &commandBuffer), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    record(commandBuffer);
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
    vkDestroyFence(context.device, fence, nullptr);
    vkDestroyCommandPool(context.device, commandPool, nullptr);
    return std::chrono::duration<double, std::milli>(stop - start).count();
}

void computeBarrier(VkCommandBuffer commandBuffer) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0U,
        1U,
        &barrier,
        0U,
        nullptr,
        0U,
        nullptr);
}

[[nodiscard]] std::size_t nextPowerOfTwo(std::size_t value) {
    if (value <= 1U) return 1U;
    --value;
    for (std::size_t shift = 1U; shift < sizeof(std::size_t) * 8U; shift <<= 1U)
        value |= value >> shift;
    return value + 1U;
}

struct MetadataParameters {
    std::uint32_t projectedCount{};
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::uint32_t reserved{};
};

struct OrderParameters {
    std::uint32_t projectedCount{};
    std::uint32_t paddedCount{};
    std::uint32_t compareDistance{};
    std::uint32_t sequenceLength{};
};

struct ScheduleParameters {
    std::array<std::uint32_t, 4> counts{};
    std::array<std::uint32_t, 4> capacity{};
};

} // namespace

GaussianFusedProjectionScheduleResult projectScheduleGaussianSplatsVulkan(
    const std::vector<GaussianProjectionInput>& inputs,
    const GaussianProjectionParameters& parameters,
    std::uint32_t columns,
    std::uint32_t rows) {
    GaussianFusedProjectionScheduleResult result;
    result.stats.inputSplats = inputs.size();
    result.inputBytes = inputs.size() * sizeof(GaussianProjectionInput);
    result.outputBytes = inputs.size() * sizeof(GaussianProjectedSplat);
    result.intermediateReadbackBytes = 8U * sizeof(std::uint32_t);

    const std::size_t tileCount = static_cast<std::size_t>(columns) * rows;
    if (columns == 0U || rows == 0U || tileCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Vulkan fused Gaussian tile grid exceeds uint32 capacity");
    if (inputs.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Vulkan fused Gaussian input exceeds uint32 capacity");
    if (inputs.empty()) {
        result.tileOffsets.assign(tileCount + 1U, 0U);
        result.paddedOrderCount = 1U;
        return result;
    }

    auto context = makeContext();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physicalDevice, &properties);
    if (properties.limits.maxPushConstantsSize < sizeof(GaussianProjectionParameters))
        throw std::runtime_error("Vulkan push-constant limit is too small for fused Gaussian execution");

    const VkDeviceSize inputBytes = static_cast<VkDeviceSize>(result.inputBytes);
    const VkDeviceSize projectedBytes = static_cast<VkDeviceSize>(result.outputBytes);
    constexpr VkDeviceSize projectionMetadataBytes = 8U * sizeof(std::uint32_t);
    HostBuffer inputBuffer = makeHostBuffer(context, inputBytes);
    HostBuffer projectedBuffer = makeHostBuffer(context, projectedBytes);
    HostBuffer projectionMetadataBuffer = makeHostBuffer(context, projectionMetadataBytes);
    std::memcpy(inputBuffer.mapped, inputs.data(), result.inputBytes);
    std::memset(projectedBuffer.mapped, 0, result.outputBytes);
    std::memset(projectionMetadataBuffer.mapped, 0, static_cast<std::size_t>(projectionMetadataBytes));
    flushIfNeeded(inputBuffer);
    flushIfNeeded(projectedBuffer);
    flushIfNeeded(projectionMetadataBuffer);

    auto projectionPipeline = makePipeline(
        context,
        VULKAX_GAUSSIAN_PROJECT_SPV_PATH,
        "Gaussian projection",
        {{inputBuffer.buffer, inputBytes}, {projectedBuffer.buffer, projectedBytes}},
        sizeof(GaussianProjectionParameters));
    auto metadataPipeline = makePipeline(
        context,
        VULKAX_GAUSSIAN_METADATA_SPV_PATH,
        "Gaussian projection metadata",
        {{projectedBuffer.buffer, projectedBytes}, {projectionMetadataBuffer.buffer, projectionMetadataBytes}},
        sizeof(MetadataParameters));

    result.projectionMilliseconds = submitCommands(context, [&](VkCommandBuffer commandBuffer) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, projectionPipeline.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                projectionPipeline.pipelineLayout, 0U, 1U,
                                &projectionPipeline.descriptorSet, 0U, nullptr);
        vkCmdPushConstants(commandBuffer, projectionPipeline.pipelineLayout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(parameters), &parameters);
        const std::uint32_t projectionGroups =
            static_cast<std::uint32_t>((inputs.size() + 63U) / 64U);
        vkCmdDispatch(commandBuffer, projectionGroups, 1U, 1U);
        computeBarrier(commandBuffer);

        const MetadataParameters metadataParameters{
            static_cast<std::uint32_t>(inputs.size()), columns, rows, 0U};
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, metadataPipeline.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                metadataPipeline.pipelineLayout, 0U, 1U,
                                &metadataPipeline.descriptorSet, 0U, nullptr);
        vkCmdPushConstants(commandBuffer, metadataPipeline.pipelineLayout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0U,
                           sizeof(metadataParameters), &metadataParameters);
        vkCmdDispatch(commandBuffer, 1U, 1U, 1U);
    });

    invalidateIfNeeded(projectionMetadataBuffer);
    const auto* projectionMetadata = static_cast<const std::uint32_t*>(projectionMetadataBuffer.mapped);
    const std::uint32_t visibleCount = projectionMetadata[0];
    result.stats.visibleSplats = visibleCount;
    result.stats.culledOpacity = projectionMetadata[1];
    result.stats.culledBehindCamera = projectionMetadata[2];
    result.stats.culledOutsideImage = projectionMetadata[3];
    const std::uint32_t referenceCount = projectionMetadata[4];
    const std::uint32_t metadataError = projectionMetadata[5];
    if (metadataError != 0U)
        throw std::runtime_error("Vulkan fused Gaussian projection metadata validation failed with code " +
                                 std::to_string(metadataError));
    const std::size_t classified = static_cast<std::size_t>(visibleCount) +
        result.stats.culledOpacity + result.stats.culledBehindCamera + result.stats.culledOutsideImage;
    if (classified != inputs.size())
        throw std::runtime_error("Vulkan fused Gaussian projection metadata count is inconsistent");
    result.splatReferences = referenceCount;

    const std::size_t paddedCount = nextPowerOfTwo(inputs.size());
    if (paddedCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Vulkan fused Gaussian padded order exceeds uint32 capacity");
    result.paddedOrderCount = paddedCount;

    const VkDeviceSize orderBytes = static_cast<VkDeviceSize>(paddedCount * sizeof(std::uint32_t));
    const VkDeviceSize offsetsBytes = static_cast<VkDeviceSize>((tileCount + 1U) * sizeof(std::uint32_t));
    const VkDeviceSize cursorsBytes = static_cast<VkDeviceSize>(
        std::max<std::size_t>(tileCount, 1U) * sizeof(std::uint32_t));
    const VkDeviceSize referencesBytes = static_cast<VkDeviceSize>(
        std::max<std::size_t>(referenceCount, 1U) * sizeof(std::uint32_t));
    constexpr VkDeviceSize scheduleMetadataBytes = 4U * sizeof(std::uint32_t);
    HostBuffer orderBuffer = makeHostBuffer(context, orderBytes);
    HostBuffer offsetsBuffer = makeHostBuffer(context, offsetsBytes);
    HostBuffer cursorsBuffer = makeHostBuffer(context, cursorsBytes);
    HostBuffer referencesBuffer = makeHostBuffer(context, referencesBytes);
    HostBuffer scheduleMetadataBuffer = makeHostBuffer(context, scheduleMetadataBytes);
    std::memset(orderBuffer.mapped, 0xff, static_cast<std::size_t>(orderBytes));
    std::memset(offsetsBuffer.mapped, 0, static_cast<std::size_t>(offsetsBytes));
    std::memset(cursorsBuffer.mapped, 0, static_cast<std::size_t>(cursorsBytes));
    std::memset(referencesBuffer.mapped, 0, static_cast<std::size_t>(referencesBytes));
    std::memset(scheduleMetadataBuffer.mapped, 0, static_cast<std::size_t>(scheduleMetadataBytes));
    flushIfNeeded(orderBuffer);
    flushIfNeeded(offsetsBuffer);
    flushIfNeeded(cursorsBuffer);
    flushIfNeeded(referencesBuffer);
    flushIfNeeded(scheduleMetadataBuffer);

    auto orderPipeline = makePipeline(
        context,
        VULKAX_GAUSSIAN_DEPTH_ORDER_SPV_PATH,
        "Gaussian depth order",
        {{projectedBuffer.buffer, projectedBytes}, {orderBuffer.buffer, orderBytes}},
        sizeof(OrderParameters));
    auto schedulePipeline = makePipeline(
        context,
        VULKAX_GAUSSIAN_FUSED_SCHEDULE_SPV_PATH,
        "Gaussian fused CSR",
        {{projectedBuffer.buffer, projectedBytes},
         {orderBuffer.buffer, orderBytes},
         {offsetsBuffer.buffer, offsetsBytes},
         {cursorsBuffer.buffer, cursorsBytes},
         {referencesBuffer.buffer, referencesBytes},
         {scheduleMetadataBuffer.buffer, scheduleMetadataBytes}},
        sizeof(ScheduleParameters));

    result.schedulingMilliseconds = submitCommands(context, [&](VkCommandBuffer commandBuffer) {
        const std::uint32_t orderGroups =
            static_cast<std::uint32_t>((paddedCount + 63U) / 64U);
        OrderParameters initParameters{
            static_cast<std::uint32_t>(inputs.size()),
            static_cast<std::uint32_t>(paddedCount),
            0U,
            0U};
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, orderPipeline.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                orderPipeline.pipelineLayout, 0U, 1U,
                                &orderPipeline.descriptorSet, 0U, nullptr);
        vkCmdPushConstants(commandBuffer, orderPipeline.pipelineLayout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0U,
                           sizeof(initParameters), &initParameters);
        vkCmdDispatch(commandBuffer, orderGroups, 1U, 1U);
        computeBarrier(commandBuffer);

        for (std::uint32_t sequenceLength = 2U;
             sequenceLength <= static_cast<std::uint32_t>(paddedCount);
             sequenceLength <<= 1U) {
            for (std::uint32_t compareDistance = sequenceLength >> 1U;
                 compareDistance > 0U;
                 compareDistance >>= 1U) {
                const OrderParameters orderParameters{
                    static_cast<std::uint32_t>(inputs.size()),
                    static_cast<std::uint32_t>(paddedCount),
                    compareDistance,
                    sequenceLength};
                vkCmdPushConstants(commandBuffer, orderPipeline.pipelineLayout,
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0U,
                                   sizeof(orderParameters), &orderParameters);
                vkCmdDispatch(commandBuffer, orderGroups, 1U, 1U);
                computeBarrier(commandBuffer);
            }
            if (sequenceLength > static_cast<std::uint32_t>(paddedCount) / 2U) break;
        }

        ScheduleParameters scheduleParameters;
        scheduleParameters.counts = {
            static_cast<std::uint32_t>(inputs.size()), visibleCount, columns, rows};
        scheduleParameters.capacity = {referenceCount, 0U, 0U, 0U};
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, schedulePipeline.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                schedulePipeline.pipelineLayout, 0U, 1U,
                                &schedulePipeline.descriptorSet, 0U, nullptr);
        vkCmdPushConstants(commandBuffer, schedulePipeline.pipelineLayout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0U,
                           sizeof(scheduleParameters), &scheduleParameters);
        vkCmdDispatch(commandBuffer, 1U, 1U, 1U);
    });

    invalidateIfNeeded(projectedBuffer);
    invalidateIfNeeded(orderBuffer);
    invalidateIfNeeded(offsetsBuffer);
    invalidateIfNeeded(referencesBuffer);
    invalidateIfNeeded(scheduleMetadataBuffer);
    const auto* scheduleMetadata = static_cast<const std::uint32_t*>(scheduleMetadataBuffer.mapped);
    if (scheduleMetadata[2] != 0U)
        throw std::runtime_error("Vulkan fused Gaussian CSR validation failed with code " +
                                 std::to_string(scheduleMetadata[2]));
    if (scheduleMetadata[0] != referenceCount || scheduleMetadata[3] != visibleCount)
        throw std::runtime_error("Vulkan fused Gaussian CSR metadata is inconsistent");
    result.maximumSplatsPerTile = scheduleMetadata[1];

    const auto* rawProjected = static_cast<const GaussianProjectedSplat*>(projectedBuffer.mapped);
    const auto* order = static_cast<const std::uint32_t*>(orderBuffer.mapped);
    result.projected.reserve(visibleCount);
    for (std::uint32_t rank = 0U; rank < visibleCount; ++rank) {
        const std::uint32_t rawIndex = order[rank];
        if (rawIndex >= inputs.size())
            throw std::runtime_error("Vulkan fused Gaussian order references invalid raw record");
        if (std::abs(rawProjected[rawIndex].colorCull[3]) > 1.0e-4F)
            throw std::runtime_error("Vulkan fused Gaussian visible prefix contains a culled record");
        result.projected.push_back(rawProjected[rawIndex]);
    }

    result.tileOffsets.resize(tileCount + 1U);
    std::memcpy(result.tileOffsets.data(), offsetsBuffer.mapped, static_cast<std::size_t>(offsetsBytes));
    result.projectedSplatIndices.resize(referenceCount);
    if (referenceCount > 0U)
        std::memcpy(result.projectedSplatIndices.data(), referencesBuffer.mapped,
                    static_cast<std::size_t>(referenceCount) * sizeof(std::uint32_t));

    result.schedulerOutputBytes = static_cast<std::size_t>(orderBytes + offsetsBytes +
        static_cast<VkDeviceSize>(referenceCount) * sizeof(std::uint32_t) + scheduleMetadataBytes);
    result.schedulerWorkspaceBytes = static_cast<std::size_t>(orderBytes + cursorsBytes);
    return result;
}

} // namespace vulkax::render
