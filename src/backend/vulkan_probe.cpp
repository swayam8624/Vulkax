#include "vulkax/backend/backend.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace vulkax::backend {
namespace {

bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [&](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

std::vector<VkExtensionProperties> instanceExtensions() {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) {
        return {};
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (count != 0 &&
        vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return {};
    }
    extensions.resize(count);
    return extensions;
}

std::vector<VkExtensionProperties> deviceExtensions(VkPhysicalDevice device) {
    std::uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) {
        return {};
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (count != 0 &&
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return {};
    }
    extensions.resize(count);
    return extensions;
}

std::uint64_t deviceLocalMemory(VkPhysicalDevice device) {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(device, &memory);
    std::uint64_t bytes = 0;
    for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u) {
            bytes += memory.memoryHeaps[i].size;
        }
    }
    return bytes;
}

} // namespace

std::vector<BackendCapabilities> probeVulkanBackends() {
    const auto availableInstanceExtensions = instanceExtensions();
    std::vector<const char*> enabledExtensions;
    VkInstanceCreateFlags flags = 0;

#if defined(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) && \
    defined(VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR)
    if (hasExtension(availableInstanceExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Vulkax Next backend probe";
    application.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    application.pEngineName = "Vulkax";
    application.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    application.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.flags = flags;
    createInfo.pApplicationInfo = &application;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        return {};
    }

    std::uint32_t deviceCount = 0;
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0) {
        vkDestroyInstance(instance, nullptr);
        return {};
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()) != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return {};
    }
    devices.resize(deviceCount);

    std::vector<BackendCapabilities> result;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceProperties(device, &properties);
        vkGetPhysicalDeviceFeatures(device, &features);

        std::uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        if (queueCount != 0) {
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());
        }

        bool hasComputeQueue = false;
        bool hasComputeTimestamps = false;
        for (const auto& queue : queues) {
            if (queue.queueCount != 0 && (queue.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u) {
                hasComputeQueue = true;
                hasComputeTimestamps = hasComputeTimestamps || queue.timestampValidBits != 0u;
            }
        }
        if (!hasComputeQueue) {
            continue;
        }

        const auto extensions = deviceExtensions(device);
        BackendCapabilities capability;
        capability.kind = BackendKind::Vulkan;
        capability.available = true;
        capability.nativePlatformBackend = currentPlatform() != PlatformKind::MacOS;
        capability.dedicatedGpu = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        capability.driverQuality = capability.nativePlatformBackend ? 0.9 : 0.75;
        capability.deviceMemoryBytes = deviceLocalMemory(device);
        capability.deviceName = properties.deviceName;
        capability.features = {Feature::Compute, Feature::StorageBuffers, Feature::Atomics,
                               Feature::Headless};

        if (properties.limits.maxPerStageDescriptorStorageImages != 0) {
            capability.features.push_back(Feature::StorageImages);
        }
        if (features.shaderFloat64 == VK_TRUE) {
            capability.features.push_back(Feature::Float64);
        }
        if (hasComputeTimestamps) {
            capability.features.push_back(Feature::TimestampQueries);
        }
#ifdef VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
        if (VK_VERSION_MAJOR(properties.apiVersion) > 1 ||
            (VK_VERSION_MAJOR(properties.apiVersion) == 1 &&
             VK_VERSION_MINOR(properties.apiVersion) >= 2) ||
            hasExtension(extensions, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME)) {
            capability.features.push_back(Feature::DescriptorIndexing);
        }
#endif
#ifdef VK_KHR_RAY_QUERY_EXTENSION_NAME
        if (hasExtension(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
            capability.features.push_back(Feature::RayQuery);
        }
#endif
        result.push_back(std::move(capability));
    }

    vkDestroyInstance(instance, nullptr);
    return result;
}

} // namespace vulkax::backend
