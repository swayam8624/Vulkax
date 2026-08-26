#include "vulkax/render/gaussian_projection.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef VULKAX_GAUSSIAN_PROJECT_SPV_PATH
#error "VULKAX_GAUSSIAN_PROJECT_SPV_PATH missing"
#endif

namespace vulkax::render {
namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: " +
                                 std::to_string(static_cast<int>(result)));
}

[[nodiscard]] std::vector<std::uint32_t> readSpirv() {
    std::ifstream stream(VULKAX_GAUSSIAN_PROJECT_SPV_PATH, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot open Gaussian projection SPIR-V shader");
    const std::streamsize byteCount = stream.tellg();
    if (byteCount <= 0 || byteCount % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0)
        throw std::runtime_error("invalid Gaussian projection SPIR-V size");
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint32_t> words(
        static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    if (!stream.read(reinterpret_cast<char*>(words.data()), byteCount))
        throw std::runtime_error("Gaussian projection SPIR-V read failed");
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
    application.pApplicationName = "Vulkax Gaussian Projection";
    application.applicationVersion = VK_MAKE_API_VERSION(0, 0, 50, 0);
    application.pEngineName = "Vulkax";
    application.engineVersion = VK_MAKE_API_VERSION(0, 0, 50, 0);
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
    if (physicalCount == 0U) throw std::runtime_error("no Vulkan physical device for Gaussian projection");
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
        throw std::runtime_error("no Vulkan compute queue for Gaussian projection");

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
    check(vkCreateDevice(context.physicalDevice, &deviceInfo, nullptr, &context.device),
          "vkCreateDevice");
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
    VkDeviceSize bytes,
    VkBufferUsageFlags usage) {
    HostBuffer result;
    result.device = context.device;

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = std::max<VkDeviceSize>(bytes, 4U);
    bufferInfo.usage = usage;
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
        throw std::runtime_error("no host-visible Vulkan memory for Gaussian projection");

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

} // namespace

std::pair<std::vector<GaussianProjectedSplat>, double> projectGaussianSplatsVulkan(
    const std::vector<GaussianProjectionInput>& inputs,
    const GaussianProjectionParameters& parameters) {
    if (inputs.empty()) return {{}, 0.0};
    if (inputs.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("Gaussian projection input exceeds Vulkan uint32 dispatch range");

    auto context = makeContext();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physicalDevice, &properties);
    if (properties.limits.maxPushConstantsSize < sizeof(GaussianProjectionParameters))
        throw std::runtime_error("Vulkan device push-constant limit is too small for Gaussian projection");

    const VkDeviceSize inputBytes =
        static_cast<VkDeviceSize>(inputs.size() * sizeof(GaussianProjectionInput));
    const VkDeviceSize outputBytes =
        static_cast<VkDeviceSize>(inputs.size() * sizeof(GaussianProjectedSplat));
    HostBuffer inputBuffer = makeHostBuffer(context, inputBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    HostBuffer outputBuffer = makeHostBuffer(context, outputBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    std::memcpy(inputBuffer.mapped, inputs.data(), static_cast<std::size_t>(inputBytes));
    std::memset(outputBuffer.mapped, 0, static_cast<std::size_t>(outputBytes));
    flushIfNeeded(inputBuffer);
    flushIfNeeded(outputBuffer);

    VkDescriptorSetLayoutBinding bindings[2]{};
    for (std::uint32_t index = 0U; index < 2U; ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[index].descriptorCount = 1U;
        bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2U;
    layoutInfo.pBindings = bindings;
    VkDescriptorSetLayout descriptorLayout{VK_NULL_HANDLE};
    check(vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &descriptorLayout),
          "vkCreateDescriptorSetLayout");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0U;
    pushRange.size = sizeof(GaussianProjectionParameters);
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

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2U};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1U;
    poolInfo.poolSizeCount = 1U;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    check(vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &descriptorPool),
          "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = descriptorPool;
    allocateInfo.descriptorSetCount = 1U;
    allocateInfo.pSetLayouts = &descriptorLayout;
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    check(vkAllocateDescriptorSets(context.device, &allocateInfo, &descriptorSet),
          "vkAllocateDescriptorSets");

    VkDescriptorBufferInfo bufferInfos[2]{};
    bufferInfos[0] = {inputBuffer.buffer, 0U, inputBytes};
    bufferInfos[1] = {outputBuffer.buffer, 0U, outputBytes};
    VkWriteDescriptorSet writes[2]{};
    for (std::uint32_t index = 0U; index < 2U; ++index) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = descriptorSet;
        writes[index].dstBinding = index;
        writes[index].descriptorCount = 1U;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[index].pBufferInfo = &bufferInfos[index];
    }
    vkUpdateDescriptorSets(context.device, 2U, writes, 0U, nullptr);

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = context.queueFamily;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool commandPool{VK_NULL_HANDLE};
    check(vkCreateCommandPool(context.device, &commandPoolInfo, nullptr, &commandPool),
          "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandAllocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandAllocateInfo.commandPool = commandPool;
    commandAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocateInfo.commandBufferCount = 1U;
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    check(vkAllocateCommandBuffers(context.device, &commandAllocateInfo, &commandBuffer),
          "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                            0U, 1U, &descriptorSet, 0U, nullptr);
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0U, sizeof(parameters), &parameters);
    const std::uint32_t groupCount =
        static_cast<std::uint32_t>((inputs.size() + 63U) / 64U);
    vkCmdDispatch(commandBuffer, groupCount, 1U, 1U);
    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence{VK_NULL_HANDLE};
    check(vkCreateFence(context.device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1U;
    submitInfo.pCommandBuffers = &commandBuffer;
    const auto start = std::chrono::steady_clock::now();
    check(vkQueueSubmit(context.queue, 1U, &submitInfo, fence), "vkQueueSubmit");
    check(vkWaitForFences(context.device, 1U, &fence, VK_TRUE, 10'000'000'000ULL),
          "vkWaitForFences");
    const auto stop = std::chrono::steady_clock::now();
    const double milliseconds =
        std::chrono::duration<double, std::milli>(stop - start).count();

    invalidateIfNeeded(outputBuffer);
    std::vector<GaussianProjectedSplat> outputs(inputs.size());
    std::memcpy(outputs.data(), outputBuffer.mapped, static_cast<std::size_t>(outputBytes));

    vkDestroyFence(context.device, fence, nullptr);
    vkDestroyCommandPool(context.device, commandPool, nullptr);
    vkDestroyDescriptorPool(context.device, descriptorPool, nullptr);
    vkDestroyPipeline(context.device, pipeline, nullptr);
    vkDestroyShaderModule(context.device, shader, nullptr);
    vkDestroyPipelineLayout(context.device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(context.device, descriptorLayout, nullptr);
    return {std::move(outputs), milliseconds};
}

} // namespace vulkax::render
