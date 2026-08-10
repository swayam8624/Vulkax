#include "vulkax/compute/conformance.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VULKAX_CONFORMANCE_SPV_PATH
#error "VULKAX_CONFORMANCE_SPV_PATH must be defined when Vulkan conformance is built"
#endif

namespace vulkax::compute {

namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(static_cast<int>(result)));
    }
}

std::vector<std::uint32_t> readSpirv() {
    std::ifstream stream(VULKAX_CONFORMANCE_SPV_PATH, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("failed to open conformance SPIR-V shader");
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0 || size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) {
        throw std::runtime_error("invalid conformance SPIR-V byte length");
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
    if (!stream.read(reinterpret_cast<char*>(words.data()), size)) {
        throw std::runtime_error("failed to read conformance SPIR-V shader");
    }
    return words;
}

struct MemoryChoice {
    std::uint32_t index{};
    bool coherent{false};
};

MemoryChoice chooseHostMemory(const VkPhysicalDeviceMemoryProperties& properties,
                              std::uint32_t typeBits) {
    std::optional<MemoryChoice> fallback;
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((typeBits & (1u << index)) == 0u) {
            continue;
        }
        const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
        if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0u) {
            continue;
        }
        const bool coherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;
        if (coherent) {
            return {index, true};
        }
        fallback = MemoryChoice{index, false};
    }
    if (fallback) {
        return *fallback;
    }
    throw std::runtime_error("no host-visible Vulkan memory type found");
}

struct PushParameters {
    float scale;
    float bias;
    std::uint32_t count;
};

} // namespace

ConformanceResult runVulkanConformance(std::size_t elementCount) {
    if (elementCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument("Vulkan conformance element count exceeds uint32 range");
    }

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Vulkax Conformance";
    application.applicationVersion = VK_MAKE_API_VERSION(0, 0, 4, 0);
    application.pEngineName = "Vulkax";
    application.engineVersion = VK_MAKE_API_VERSION(0, 0, 4, 0);
    application.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application;
    VkInstance instance = VK_NULL_HANDLE;
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    std::uint32_t physicalCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr), "vkEnumeratePhysicalDevices(count)");
    if (physicalCount == 0) {
        vkDestroyInstance(instance, nullptr);
        throw std::runtime_error("no Vulkan physical device available");
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    check(vkEnumeratePhysicalDevices(instance, &physicalCount, physicalDevices.data()),
          "vkEnumeratePhysicalDevices(list)");

    VkPhysicalDevice physical = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    for (VkPhysicalDevice candidate : physicalDevices) {
        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (std::uint32_t index = 0; index < familyCount; ++index) {
            if ((families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u) {
                physical = candidate;
                queueFamily = index;
                break;
            }
        }
        if (physical != VK_NULL_HANDLE) {
            break;
        }
    }
    if (physical == VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        throw std::runtime_error("no Vulkan compute queue family available");
    }

    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physical, &deviceProperties);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);

    const float queuePriority = 1.0F;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device = VK_NULL_HANDLE;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    const VkDeviceSize bufferBytes = static_cast<VkDeviceSize>(elementCount * sizeof(float));
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bufferBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer = VK_NULL_HANDLE;
    check(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);
    const MemoryChoice memoryChoice = chooseHostMemory(memoryProperties, memoryRequirements.memoryTypeBits);
    VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocationInfo.allocationSize = memoryRequirements.size;
    allocationInfo.memoryTypeIndex = memoryChoice.index;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    check(vkAllocateMemory(device, &allocationInfo, nullptr, &memory), "vkAllocateMemory");
    check(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");

    void* mapped = nullptr;
    check(vkMapMemory(device, memory, 0, memoryRequirements.size, 0, &mapped), "vkMapMemory");
    auto* values = static_cast<float*>(mapped);
    std::vector<float> expected(elementCount);
    constexpr float scale = 1.75F;
    constexpr float bias = -0.375F;
    for (std::size_t index = 0; index < elementCount; ++index) {
        const float x = static_cast<float>(index % 257u) * 0.03125F - 3.0F;
        values[index] = x;
        expected[index] = scale * x + bias;
    }
    if (!memoryChoice.coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        check(vkFlushMappedMemoryRanges(device, 1, &range), "vkFlushMappedMemoryRanges");
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorLayoutInfo.bindingCount = 1;
    descriptorLayoutInfo.pBindings = &binding;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    check(vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &descriptorLayout),
          "vkCreateDescriptorSetLayout");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushParameters);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
          "vkCreatePipelineLayout");

    const auto shaderWords = readSpirv();
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = shaderWords.size() * sizeof(std::uint32_t);
    shaderInfo.pCode = shaderWords.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule");

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shader;
    stageInfo.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline),
          "vkCreateComputePipelines");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo setAllocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setAllocateInfo.descriptorPool = descriptorPool;
    setAllocateInfo.descriptorSetCount = 1;
    setAllocateInfo.pSetLayouts = &descriptorLayout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &setAllocateInfo, &descriptorSet), "vkAllocateDescriptorSets");
    VkDescriptorBufferInfo descriptorBuffer{};
    descriptorBuffer.buffer = buffer;
    descriptorBuffer.offset = 0;
    descriptorBuffer.range = bufferBytes;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &descriptorBuffer;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = queueFamily;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandAllocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandAllocateInfo.commandPool = commandPool;
    commandAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocateInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device, &commandAllocateInfo, &commandBuffer), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1,
                            &descriptorSet, 0, nullptr);
    const PushParameters parameters{scale, bias, static_cast<std::uint32_t>(elementCount)};
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(parameters), &parameters);
    const std::uint32_t groupCount = static_cast<std::uint32_t>((elementCount + 63u) / 64u);
    vkCmdDispatch(commandBuffer, groupCount, 1, 1);
    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    check(vkQueueSubmit(queue, 1, &submitInfo, fence), "vkQueueSubmit");
    check(vkWaitForFences(device, 1, &fence, VK_TRUE, 10'000'000'000ULL), "vkWaitForFences");

    if (!memoryChoice.coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        check(vkInvalidateMappedMemoryRanges(device, 1, &range), "vkInvalidateMappedMemoryRanges");
    }

    double maxAbsolute = 0.0;
    double maxRelative = 0.0;
    for (std::size_t index = 0; index < elementCount; ++index) {
        const double actual = static_cast<double>(values[index]);
        const double reference = static_cast<double>(expected[index]);
        const double absolute = std::abs(actual - reference);
        const double relative = absolute / std::max(1.0e-12, std::abs(reference));
        maxAbsolute = std::max(maxAbsolute, absolute);
        maxRelative = std::max(maxRelative, relative);
    }

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, shader, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    vkUnmapMemory(device, memory);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    return {backend::BackendKind::Vulkan, deviceProperties.deviceName, elementCount, maxAbsolute,
            maxRelative, maxAbsolute <= 1.0e-5};
}

} // namespace vulkax::compute
