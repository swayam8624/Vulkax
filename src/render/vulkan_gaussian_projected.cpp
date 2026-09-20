#include "vulkax/render/gaussian_projection.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VULKAX_GAUSSIAN_PROJECTED_VERT_SPV_PATH
#error "VULKAX_GAUSSIAN_PROJECTED_VERT_SPV_PATH missing"
#endif
#ifndef VULKAX_GAUSSIAN_FRAG_SPV_PATH
#error "VULKAX_GAUSSIAN_FRAG_SPV_PATH missing"
#endif

namespace vulkax::render {
namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: " +
                                 std::to_string(static_cast<int>(result)));
}

[[nodiscard]] std::vector<std::uint32_t> readSpirv(const char* path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error(std::string("cannot open shader ") + path);
    const std::streamsize byteCount = stream.tellg();
    if (byteCount <= 0 || byteCount % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0)
        throw std::runtime_error("invalid projected Gaussian SPIR-V size");
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint32_t> words(
        static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    if (!stream.read(reinterpret_cast<char*>(words.data()), byteCount))
        throw std::runtime_error("projected Gaussian SPIR-V read failed");
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

[[nodiscard]] std::uint32_t chooseMemoryType(
    const VkPhysicalDeviceMemoryProperties& properties,
    std::uint32_t allowedBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred = 0U) {
    std::uint32_t fallback = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t index = 0U; index < properties.memoryTypeCount; ++index) {
        if ((allowedBits & (1U << index)) == 0U) continue;
        const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
        if ((flags & required) != required) continue;
        if ((flags & preferred) == preferred) return index;
        if (fallback == std::numeric_limits<std::uint32_t>::max()) fallback = index;
    }
    if (fallback == std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("no compatible Vulkan memory type for projected Gaussian raster");
    return fallback;
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
    application.pApplicationName = "Vulkax Projected Gaussian Renderer";
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
    if (physicalCount == 0U)
        throw std::runtime_error("no Vulkan physical device for projected Gaussian raster");
    std::vector<VkPhysicalDevice> devices(physicalCount);
    check(vkEnumeratePhysicalDevices(context.instance, &physicalCount, devices.data()),
          "vkEnumeratePhysicalDevices(list)");
    for (VkPhysicalDevice device : devices) {
        std::uint32_t familyCount = 0U;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
        for (std::uint32_t family = 0U; family < familyCount; ++family) {
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
                context.physicalDevice = device;
                context.queueFamily = family;
                break;
            }
        }
        if (context.physicalDevice != VK_NULL_HANDLE) break;
    }
    if (context.physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("no Vulkan graphics queue for projected Gaussian raster");

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

struct BufferAllocation {
    VkBuffer buffer{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
};

[[nodiscard]] BufferAllocation makeHostBuffer(
    const VulkanContext& context,
    VkDeviceSize bytes,
    VkBufferUsageFlags usage) {
    BufferAllocation allocation;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = std::max<VkDeviceSize>(bytes, 4U);
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(context.device, &bufferInfo, nullptr, &allocation.buffer), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device, allocation.buffer, &requirements);
    VkMemoryAllocateInfo memoryInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memoryInfo.allocationSize = requirements.size;
    memoryInfo.memoryTypeIndex = chooseMemoryType(
        context.memoryProperties, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(context.device, &memoryInfo, nullptr, &allocation.memory), "vkAllocateMemory(buffer)");
    check(vkBindBufferMemory(context.device, allocation.buffer, allocation.memory, 0U), "vkBindBufferMemory");
    return allocation;
}

[[nodiscard]] VkShaderModule makeShader(VkDevice device, const char* path) {
    const auto words = readSpirv(path);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = words.size() * sizeof(std::uint32_t);
    info.pCode = words.data();
    VkShaderModule module{VK_NULL_HANDLE};
    check(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

} // namespace

ImageRGBA8 renderGaussianProjectedVulkan(
    const std::vector<GaussianProjectedSplat>& projected,
    const GaussianRenderSettings& settings) {
    if (projected.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / 6U))
        throw std::invalid_argument("projected Gaussian vertex count exceeds Vulkan uint32 range");

    auto context = makeContext();
    const VkDevice device = context.device;
    const VkDeviceSize pixelBytes = static_cast<VkDeviceSize>(settings.image.width) *
                                    static_cast<VkDeviceSize>(settings.image.height) * 4U;
    const VkDeviceSize projectedBytes = static_cast<VkDeviceSize>(
        std::max<std::size_t>(projected.size() * sizeof(GaussianProjectedSplat), 4U));

    const auto projectedAllocation =
        makeHostBuffer(context, projectedBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (!projected.empty()) {
        void* mapped = nullptr;
        check(vkMapMemory(device, projectedAllocation.memory, 0U, projectedBytes, 0U, &mapped),
              "vkMapMemory(projected Gaussian records)");
        std::memcpy(mapped, projected.data(), projected.size() * sizeof(GaussianProjectedSplat));
        vkUnmapMemory(device, projectedAllocation.memory);
    }

    VkImage image{VK_NULL_HANDLE};
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {settings.image.width, settings.image.height, 1U};
    imageInfo.mipLevels = 1U;
    imageInfo.arrayLayers = 1U;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device, &imageInfo, nullptr, &image), "vkCreateImage");

    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(device, image, &imageRequirements);
    VkMemoryAllocateInfo imageMemoryInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    imageMemoryInfo.allocationSize = imageRequirements.size;
    imageMemoryInfo.memoryTypeIndex = chooseMemoryType(
        context.memoryProperties, imageRequirements.memoryTypeBits, 0U,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory imageMemory{VK_NULL_HANDLE};
    check(vkAllocateMemory(device, &imageMemoryInfo, nullptr, &imageMemory), "vkAllocateMemory(image)");
    check(vkBindImageMemory(device, image, imageMemory, 0U), "vkBindImageMemory");

    VkImageView imageView{VK_NULL_HANDLE};
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1U;
    viewInfo.subresourceRange.layerCount = 1U;
    check(vkCreateImageView(device, &viewInfo, nullptr, &imageView), "vkCreateImageView");

    VkAttachmentDescription attachment{};
    attachment.format = imageInfo.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference colorReference{0U, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1U;
    subpass.pColorAttachments = &colorReference;
    VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassInfo.attachmentCount = 1U;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1U;
    renderPassInfo.pSubpasses = &subpass;
    VkRenderPass renderPass{VK_NULL_HANDLE};
    check(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass), "vkCreateRenderPass");

    VkFramebuffer framebuffer{VK_NULL_HANDLE};
    VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1U;
    framebufferInfo.pAttachments = &imageView;
    framebufferInfo.width = settings.image.width;
    framebufferInfo.height = settings.image.height;
    framebufferInfo.layers = 1U;
    check(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer), "vkCreateFramebuffer");

    const VkShaderModule vertexShader = makeShader(device, VULKAX_GAUSSIAN_PROJECTED_VERT_SPV_PATH);
    const VkShaderModule fragmentShader = makeShader(device, VULKAX_GAUSSIAN_FRAG_SPV_PATH);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0U,
                 VK_SHADER_STAGE_VERTEX_BIT, vertexShader, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0U,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main", nullptr};

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0.0F, 0.0F, static_cast<float>(settings.image.width),
                        static_cast<float>(settings.image.height), 0.0F, 1.0F};
    VkRect2D scissor{{0, 0}, {settings.image.width, settings.image.height}};
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1U;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1U;
    viewportState.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blendState.attachmentCount = 1U;
    blendState.pAttachments = &blendAttachment;

    VkDescriptorSetLayoutBinding projectedBinding{};
    projectedBinding.binding = 0U;
    projectedBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    projectedBinding.descriptorCount = 1U;
    projectedBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorLayoutInfo.bindingCount = 1U;
    descriptorLayoutInfo.pBindings = &projectedBinding;
    VkDescriptorSetLayout descriptorLayout{VK_NULL_HANDLE};
    check(vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &descriptorLayout),
          "vkCreateDescriptorSetLayout");
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0U;
    pushRange.size = sizeof(float);
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1U;
    layoutInfo.pSetLayouts = &descriptorLayout;
    layoutInfo.pushConstantRangeCount = 1U;
    layoutInfo.pPushConstantRanges = &pushRange;
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2U;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &blendState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    VkPipeline pipeline{VK_NULL_HANDLE};
    check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &pipeline),
          "vkCreateGraphicsPipelines");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1U;
    poolInfo.poolSizeCount = 1U;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo descriptorAllocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descriptorAllocateInfo.descriptorPool = descriptorPool;
    descriptorAllocateInfo.descriptorSetCount = 1U;
    descriptorAllocateInfo.pSetLayouts = &descriptorLayout;
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    check(vkAllocateDescriptorSets(device, &descriptorAllocateInfo, &descriptorSet), "vkAllocateDescriptorSets");
    VkDescriptorBufferInfo projectedInfo{projectedAllocation.buffer, 0U, projectedBytes};
    VkWriteDescriptorSet projectedWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    projectedWrite.dstSet = descriptorSet;
    projectedWrite.dstBinding = 0U;
    projectedWrite.descriptorCount = 1U;
    projectedWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    projectedWrite.pBufferInfo = &projectedInfo;
    vkUpdateDescriptorSets(device, 1U, &projectedWrite, 0U, nullptr);

    const auto readbackAllocation =
        makeHostBuffer(context, pixelBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = context.queueFamily;
    check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool), "vkCreateCommandPool");
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1U;
    check(vkAllocateCommandBuffers(device, &commandInfo, &commandBuffer), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    VkClearValue clear{};
    clear.color.float32[0] = settings.image.clearColor.r;
    clear.color.float32[1] = settings.image.clearColor.g;
    clear.color.float32[2] = settings.image.clearColor.b;
    clear.color.float32[3] = settings.image.clearColor.a;
    VkRenderPassBeginInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    passInfo.renderPass = renderPass;
    passInfo.framebuffer = framebuffer;
    passInfo.renderArea = {{0, 0}, {settings.image.width, settings.image.height}};
    passInfo.clearValueCount = 1U;
    passInfo.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    if (!projected.empty()) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0U, 1U, &descriptorSet, 0U, nullptr);
        const float sigmaCutoff = static_cast<float>(settings.sigmaCutoff);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0U, sizeof(sigmaCutoff), &sigmaCutoff);
        vkCmdDraw(commandBuffer, static_cast<std::uint32_t>(projected.size() * 6U), 1U, 0U, 0U);
    }
    vkCmdEndRenderPass(commandBuffer);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1U;
    copyRegion.imageExtent = {settings.image.width, settings.image.height, 1U};
    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackAllocation.buffer, 1U, &copyRegion);
    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence{VK_NULL_HANDLE};
    check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1U;
    submitInfo.pCommandBuffers = &commandBuffer;
    check(vkQueueSubmit(context.queue, 1U, &submitInfo, fence), "vkQueueSubmit");
    check(vkWaitForFences(device, 1U, &fence, VK_TRUE, 10'000'000'000ULL), "vkWaitForFences");

    ImageRGBA8 output{
        settings.image.width,
        settings.image.height,
        std::vector<std::uint8_t>(static_cast<std::size_t>(pixelBytes))};
    void* mappedPixels = nullptr;
    check(vkMapMemory(device, readbackAllocation.memory, 0U, pixelBytes, 0U, &mappedPixels),
          "vkMapMemory(readback)");
    std::memcpy(output.pixels.data(), mappedPixels, output.pixels.size());
    vkUnmapMemory(device, readbackAllocation.memory);

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyBuffer(device, readbackAllocation.buffer, nullptr);
    vkFreeMemory(device, readbackAllocation.memory, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    vkDestroyShaderModule(device, fragmentShader, nullptr);
    vkDestroyShaderModule(device, vertexShader, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroyImageView(device, imageView, nullptr);
    vkDestroyImage(device, image, nullptr);
    vkFreeMemory(device, imageMemory, nullptr);
    vkDestroyBuffer(device, projectedAllocation.buffer, nullptr);
    vkFreeMemory(device, projectedAllocation.memory, nullptr);
    return output;
}

} // namespace vulkax::render
