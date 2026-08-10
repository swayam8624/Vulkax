#include "vulkax/compute/runtime.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VULKAX_GLSLANG_VALIDATOR_PATH
#define VULKAX_GLSLANG_VALIDATOR_PATH "glslangValidator"
#endif

namespace vulkax::compute {
namespace {

void vkCheck(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " failed with VkResult " + std::to_string(r));
}

std::vector<std::uint32_t> compileShader() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto base = std::filesystem::temp_directory_path() / ("vulkax_compute_ir_" + std::to_string(stamp));
    const auto source = base.string() + ".comp";
    const auto binary = base.string() + ".spv";
    {
        std::ofstream out(source, std::ios::binary);
        if (!out) throw std::runtime_error("unable to create temporary GLSL source");
        out << vulkanInterpreterGlsl();
    }
    std::ostringstream command;
    command << '"' << VULKAX_GLSLANG_VALIDATOR_PATH << "\" -V \"" << source << "\" -o \"" << binary << '"';
    const int code = std::system(command.str().c_str());
    std::filesystem::remove(source);
    if (code != 0) {
        std::filesystem::remove(binary);
        throw std::runtime_error("glslangValidator failed to compile Vulkax ComputeIR shader");
    }
    std::ifstream in(binary, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("unable to read compiled SPIR-V");
    const auto size = static_cast<std::size_t>(in.tellg());
    if (size == 0 || size % sizeof(std::uint32_t) != 0) throw std::runtime_error("invalid SPIR-V byte count");
    in.seekg(0);
    std::vector<std::uint32_t> words(size / sizeof(std::uint32_t));
    in.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));
    std::filesystem::remove(binary);
    return words;
}

std::uint32_t memoryType(VkPhysicalDevice physical, std::uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    throw std::runtime_error("no Vulkan memory type satisfies required host-visible coherent flags");
}

struct Buffer {
    VkBuffer buffer{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    void* mapped{};
};

Buffer makeBuffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize bytes) {
    Buffer out;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = bytes;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateBuffer(device, &info, nullptr, &out.buffer), "vkCreateBuffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, out.buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryType(physical, req.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkCheck(vkAllocateMemory(device, &alloc, nullptr, &out.memory), "vkAllocateMemory");
    vkCheck(vkBindBufferMemory(device, out.buffer, out.memory, 0), "vkBindBufferMemory");
    vkCheck(vkMapMemory(device, out.memory, 0, bytes, 0, &out.mapped), "vkMapMemory");
    return out;
}

void destroyBuffer(VkDevice device, Buffer& buffer) {
    if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
    if (buffer.buffer) vkDestroyBuffer(device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = {};
}

} // namespace

ExecutionResult executeVulkan(const ComputeProgram& program, std::vector<std::vector<float>> buffers) {
    ExecutionResult result;
    result.backend = backend::BackendKind::Vulkan;
    const auto start = std::chrono::steady_clock::now();
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::array<Buffer, 4> dataBuffers{};
    Buffer opBuffer{};
    try {
        const auto validation = validateProgram(program);
        if (!validation.ok()) throw std::invalid_argument(validation.errors.front());
        if (buffers.size() != program.bufferCount) throw std::invalid_argument("buffer count mismatch");
        for (const auto& b : buffers) if (b.size() != program.elementCount) throw std::invalid_argument("buffer length mismatch");

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Vulkax ComputeIR";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &app;
        vkCheck(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

        std::uint32_t physicalCount = 0;
        vkCheck(vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr), "vkEnumeratePhysicalDevices(count)");
        if (physicalCount == 0) throw std::runtime_error("no Vulkan physical devices");
        std::vector<VkPhysicalDevice> physicals(physicalCount);
        vkCheck(vkEnumeratePhysicalDevices(instance, &physicalCount, physicals.data()), "vkEnumeratePhysicalDevices");
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        int bestScore = -1;
        for (auto candidate : physicals) {
            std::uint32_t queueCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
            for (std::uint32_t q = 0; q < queueCount; ++q) {
                if ((queues[q].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) continue;
                VkPhysicalDeviceProperties props{};
                vkGetPhysicalDeviceProperties(candidate, &props);
                int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 :
                            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
                if (score > bestScore) { bestScore = score; physical = candidate; queueFamily = q; }
            }
        }
        if (!physical) throw std::runtime_error("no Vulkan compute queue");
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        result.deviceName = properties.deviceName;

        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        vkCheck(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, queueFamily, 0, &queue);

        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
        for (std::uint32_t i = 0; i < bindings.size(); ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dsl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dsl.bindingCount = static_cast<std::uint32_t>(bindings.size());
        dsl.pBindings = bindings.data();
        vkCheck(vkCreateDescriptorSetLayout(device, &dsl, nullptr, &descriptorLayout), "vkCreateDescriptorSetLayout");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 2u * sizeof(std::uint32_t)};
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount = 1; pli.pSetLayouts = &descriptorLayout; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &range;
        vkCheck(vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout), "vkCreatePipelineLayout");

        const auto spirv = compileShader();
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sm.codeSize = spirv.size() * sizeof(std::uint32_t); sm.pCode = spirv.data();
        vkCheck(vkCreateShaderModule(device, &sm, nullptr, &shader), "vkCreateShaderModule");
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; stage.module = shader; stage.pName = "main";
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.stage = stage; cp.layout = pipelineLayout;
        vkCheck(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &pipeline), "vkCreateComputePipelines");

        const VkDeviceSize dataBytes = static_cast<VkDeviceSize>(program.elementCount) * sizeof(float);
        for (std::uint32_t i = 0; i < 4; ++i) {
            dataBuffers[i] = makeBuffer(physical, device, dataBytes);
            std::memset(dataBuffers[i].mapped, 0, static_cast<std::size_t>(dataBytes));
            if (i < buffers.size()) std::memcpy(dataBuffers[i].mapped, buffers[i].data(), static_cast<std::size_t>(dataBytes));
        }
        const auto encoded = encodeGpuInstructions(program);
        const VkDeviceSize opBytes = static_cast<VkDeviceSize>(encoded.size() * sizeof(GpuInstruction));
        opBuffer = makeBuffer(physical, device, opBytes);
        std::memcpy(opBuffer.mapped, encoded.data(), static_cast<std::size_t>(opBytes));

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = 1; dp.poolSizeCount = 1; dp.pPoolSizes = &poolSize;
        vkCheck(vkCreateDescriptorPool(device, &dp, nullptr, &descriptorPool), "vkCreateDescriptorPool");
        VkDescriptorSetAllocateInfo dsa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsa.descriptorPool = descriptorPool; dsa.descriptorSetCount = 1; dsa.pSetLayouts = &descriptorLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        vkCheck(vkAllocateDescriptorSets(device, &dsa, &set), "vkAllocateDescriptorSets");
        std::array<VkDescriptorBufferInfo, 5> infos{};
        for (std::uint32_t i = 0; i < 4; ++i) infos[i] = {dataBuffers[i].buffer, 0, dataBytes};
        infos[4] = {opBuffer.buffer, 0, opBytes};
        std::array<VkWriteDescriptorSet, 5> writes{};
        for (std::uint32_t i = 0; i < writes.size(); ++i) {
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);

        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = queueFamily;
        vkCheck(vkCreateCommandPool(device, &pool, nullptr, &commandPool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo cba{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cba.commandPool = commandPool; cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cba.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        vkCheck(vkAllocateCommandBuffers(device, &cba, &command), "vkAllocateCommandBuffers");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkCheck(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
        for (std::uint32_t op = 0; op < program.instructions.size(); ++op) {
            const std::array<std::uint32_t, 2> push{program.elementCount, op};
            vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               static_cast<std::uint32_t>(sizeof(push)), push.data());
            vkCmdDispatch(command, (program.elementCount + 63u) / 64u, 1, 1);
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
        VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &hostBarrier, 0, nullptr, 0, nullptr);
        vkCheck(vkEndCommandBuffer(command), "vkEndCommandBuffer");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkCheck(vkCreateFence(device, &fi, nullptr, &fence), "vkCreateFence");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        vkCheck(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
        vkCheck(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

        result.buffers.resize(program.bufferCount);
        for (std::uint32_t i = 0; i < program.bufferCount; ++i) {
            result.buffers[i].resize(program.elementCount);
            std::memcpy(result.buffers[i].data(), dataBuffers[i].mapped, static_cast<std::size_t>(dataBytes));
        }
        result.ok = true;
    } catch (const std::exception& e) {
        result.diagnostic = e.what();
    }
    if (device) vkDeviceWaitIdle(device);
    if (fence) vkDestroyFence(device, fence, nullptr);
    if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
    if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    for (auto& b : dataBuffers) if (device) destroyBuffer(device, b);
    if (device) destroyBuffer(device, opBuffer);
    if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
    if (shader) vkDestroyShaderModule(device, shader, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorLayout) vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    if (device) vkDestroyDevice(device, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    const auto end = std::chrono::steady_clock::now();
    result.wallMilliseconds = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

} // namespace vulkax::compute
