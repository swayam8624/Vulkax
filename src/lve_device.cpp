#include "lve_device.hpp"
#include "vulkax/gpu/vk_result.hpp"

// std headers
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <set>
#include <unordered_set>

namespace lve {

// local callback functions
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData) {
  std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

  return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance,
      "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks *pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance,
      "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

// class member functions
namespace {
using vulkax::gpu::checkVk;

bool hasInstanceExtension(const char* name) {
  uint32_t count = 0;
  checkVk(
      vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
      "vkEnumerateInstanceExtensionProperties(count)");
  std::vector<VkExtensionProperties> extensions(count);
  checkVk(
      vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()),
      "vkEnumerateInstanceExtensionProperties(data)");
  return std::any_of(extensions.begin(), extensions.end(), [&](const VkExtensionProperties& extension) {
    return std::strcmp(extension.extensionName, name) == 0;
  });
}

bool hasDeviceExtension(VkPhysicalDevice device, const char* name) {
  uint32_t count = 0;
  checkVk(
      vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
      "vkEnumerateDeviceExtensionProperties(count)");
  std::vector<VkExtensionProperties> extensions(count);
  checkVk(
      vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()),
      "vkEnumerateDeviceExtensionProperties(data)");
  return std::any_of(extensions.begin(), extensions.end(), [&](const VkExtensionProperties& extension) {
    return std::strcmp(extension.extensionName, name) == 0;
  });
}

std::string uuidString(const uint8_t* bytes) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (uint32_t i = 0; i < VK_UUID_SIZE; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out << '-';
    out << std::setw(2) << static_cast<uint32_t>(bytes[i]);
  }
  return out.str();
}

std::string physicalDeviceUuid(VkPhysicalDevice device) {
  VkPhysicalDeviceIDProperties id{};
  id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
  VkPhysicalDeviceProperties2 properties{};
  properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  properties.pNext = &id;
  vkGetPhysicalDeviceProperties2(device, &properties);
  return uuidString(id.deviceUUID);
}
}  // namespace

LveDevice::LveDevice(VulkanSurfaceHost &surfaceHost, const beacon::BenchmarkConfig& config)
    : selectionConfig{config}, surfaceHost{surfaceHost} {
  createInstance();
  setupDebugMessenger();
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  createCommandPool();
}

LveDevice::~LveDevice() {
  // The device owns the command pool and every legacy resource ultimately
  // submitted through it. Destruction must not race outstanding GPU work.
  if (device_ != VK_NULL_HANDLE) {
    const VkResult idleResult = vkDeviceWaitIdle(device_);
    if (idleResult != VK_SUCCESS) {
      std::cerr << "vkDeviceWaitIdle during shutdown failed: "
                << vulkax::gpu::vkResultName(idleResult) << " ("
                << static_cast<int>(idleResult) << ")" << std::endl;
    }
  }

  vkDestroyFence(device_, immediateFence, nullptr);
  vkDestroyCommandPool(device_, commandPool, nullptr);
  vkDestroyDevice(device_, nullptr);

  if (enableValidationLayers) {
    DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
  }

  vkDestroySurfaceKHR(instance, surface_, nullptr);
  vkDestroyInstance(instance, nullptr);
}

void LveDevice::createInstance() {
  if (enableValidationLayers && !checkValidationLayerSupport()) {
    throw std::runtime_error("validation layers requested, but not available!");
  }

  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Vulkax";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 24, 0);
  appInfo.pEngineName = "Vulkax GPU Runtime";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 24, 0);
  uint32_t loaderVersion = VK_API_VERSION_1_0;
  auto enumerateVersion =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
  if (enumerateVersion != nullptr) {
    checkVk(enumerateVersion(&loaderVersion), "vkEnumerateInstanceVersion");
  }
  appInfo.apiVersion = std::min(loaderVersion, VK_API_VERSION_1_2);

  VkInstanceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = getRequiredExtensions();
  bool portabilityEnumeration = hasInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
  if (portabilityEnumeration) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
  if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    populateDebugMessengerCreateInfo(debugCreateInfo);
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
  } else {
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;
  }

  checkVk(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");
  validateRequiredInstanceExtensions();
}

void LveDevice::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  checkVk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices(count)");
  if (deviceCount == 0) {
    throw std::runtime_error("failed to find GPUs with Vulkan support!");
  }
  std::cout << "Device count: " << deviceCount << std::endl;
  std::vector<VkPhysicalDevice> devices(deviceCount);
  checkVk(
      vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()),
      "vkEnumeratePhysicalDevices(data)");

  for (uint32_t index = 0; index < devices.size(); ++index) {
    VkPhysicalDeviceProperties candidate{};
    vkGetPhysicalDeviceProperties(devices[index], &candidate);
    std::string uuid = physicalDeviceUuid(devices[index]);
    std::cout << "  [" << index << "] " << candidate.deviceName << " uuid=" << uuid
              << " api=" << VK_VERSION_MAJOR(candidate.apiVersion) << "."
              << VK_VERSION_MINOR(candidate.apiVersion) << "." << VK_VERSION_PATCH(candidate.apiVersion)
              << std::endl;
  }

  for (uint32_t index = 0; index < devices.size(); ++index) {
    VkPhysicalDeviceProperties candidate{};
    vkGetPhysicalDeviceProperties(devices[index], &candidate);
    std::string uuid = physicalDeviceUuid(devices[index]);
    bool indexMatches = selectionConfig.deviceIndex < 0 ||
                        static_cast<uint32_t>(selectionConfig.deviceIndex) == index;
    bool nameMatches = selectionConfig.deviceName.empty() ||
                       std::string{candidate.deviceName}.find(selectionConfig.deviceName) != std::string::npos;
    bool uuidMatches = selectionConfig.deviceUuid.empty() || uuid == selectionConfig.deviceUuid;
    if (indexMatches && nameMatches && uuidMatches && isDeviceSuitable(devices[index])) {
      physicalDevice = devices[index];
      deviceUuid = uuid;
      break;
    }
  }

  if (physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("failed to find a suitable GPU!");
  }

  vkGetPhysicalDeviceProperties(physicalDevice, &properties);
  capabilities.properties = properties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &capabilities.memory);
  vkGetPhysicalDeviceFeatures(physicalDevice, &capabilities.features);
  capabilities.timestampQueries = properties.limits.timestampComputeAndGraphics == VK_TRUE;
  capabilities.shaderInt64 = capabilities.features.shaderInt64 == VK_TRUE;
  capabilities.multiDrawIndirect = capabilities.features.multiDrawIndirect == VK_TRUE;
  capabilities.drawIndirectFirstInstance = capabilities.features.drawIndirectFirstInstance == VK_TRUE;
  std::cout << "physical device: " << properties.deviceName << std::endl;
  std::cout << "BEACON capabilities:"
            << " timestampQueries=" << capabilities.timestampQueries
            << " multiDrawIndirect=" << capabilities.multiDrawIndirect
            << " drawIndirectFirstInstance=" << capabilities.drawIndirectFirstInstance
            << " shaderInt64=" << capabilities.shaderInt64 << std::endl;
}

void LveDevice::createLogicalDevice() {
  QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily, indices.presentFamily};

  float queuePriority = 1.0f;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  VkPhysicalDeviceFeatures deviceFeatures = {};
  deviceFeatures.samplerAnisotropy = VK_TRUE;

  VkDeviceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

  createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();

  createInfo.pEnabledFeatures = &deviceFeatures;
  std::vector<const char*> enabledExtensions = deviceExtensions;
  if (hasDeviceExtension(physicalDevice, "VK_KHR_portability_subset")) {
    enabledExtensions.push_back("VK_KHR_portability_subset");
  }
  createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
  createInfo.ppEnabledExtensionNames = enabledExtensions.data();

  // might not really be necessary anymore because device specific validation layers
  // have been deprecated
  if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
  } else {
    createInfo.enabledLayerCount = 0;
  }

  checkVk(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device_), "vkCreateDevice");

  vkGetDeviceQueue(device_, indices.graphicsFamily, 0, &graphicsQueue_);
  vkGetDeviceQueue(device_, indices.presentFamily, 0, &presentQueue_);
}

void LveDevice::createCommandPool() {
  QueueFamilyIndices queueFamilyIndices = findPhysicalQueueFamilies();

  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
  poolInfo.flags =
      VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  checkVk(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool");

  VkCommandBufferAllocateInfo commandInfo{};
  commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandInfo.commandPool = commandPool;
  commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandInfo.commandBufferCount = 1;
  checkVk(
      vkAllocateCommandBuffers(device_, &commandInfo, &immediateCommandBuffer),
      "vkAllocateCommandBuffers(immediate)");

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  checkVk(vkCreateFence(device_, &fenceInfo, nullptr, &immediateFence), "vkCreateFence(immediate)");
}

void LveDevice::createSurface() { surface_ = surfaceHost.createVulkanSurface(instance); }

bool LveDevice::isDeviceSuitable(VkPhysicalDevice device) {
  QueueFamilyIndices indices = findQueueFamilies(device);

  bool extensionsSupported = checkDeviceExtensionSupport(device);

  bool swapChainAdequate = false;
  if (extensionsSupported) {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
    swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
  }

  VkPhysicalDeviceFeatures supportedFeatures;
  vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

  return indices.isComplete() && extensionsSupported && swapChainAdequate &&
         supportedFeatures.samplerAnisotropy;
}

void LveDevice::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
  createInfo.pUserData = nullptr;  // Optional
}

void LveDevice::setupDebugMessenger() {
  if (!enableValidationLayers) return;
  VkDebugUtilsMessengerCreateInfoEXT createInfo;
  populateDebugMessengerCreateInfo(createInfo);
  checkVk(
      CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger),
      "vkCreateDebugUtilsMessengerEXT");
}

bool LveDevice::checkValidationLayerSupport() {
  uint32_t layerCount;
  checkVk(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "vkEnumerateInstanceLayerProperties(count)");

  std::vector<VkLayerProperties> availableLayers(layerCount);
  checkVk(
      vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data()),
      "vkEnumerateInstanceLayerProperties(data)");

  for (const char *layerName : validationLayers) {
    bool layerFound = false;

    for (const auto &layerProperties : availableLayers) {
      if (strcmp(layerName, layerProperties.layerName) == 0) {
        layerFound = true;
        break;
      }
    }

    if (!layerFound) {
      return false;
    }
  }

  return true;
}

std::vector<const char *> LveDevice::getRequiredExtensions() {
  auto extensions = surfaceHost.requiredInstanceExtensions();

  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return extensions;
}

void LveDevice::validateRequiredInstanceExtensions() {
  uint32_t extensionCount = 0;
  checkVk(
      vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr),
      "vkEnumerateInstanceExtensionProperties(required count)");
  std::vector<VkExtensionProperties> extensions(extensionCount);
  checkVk(
      vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()),
      "vkEnumerateInstanceExtensionProperties(required data)");

  std::cout << "available extensions:" << std::endl;
  std::unordered_set<std::string> available;
  for (const auto &extension : extensions) {
    std::cout << "\t" << extension.extensionName << std::endl;
    available.insert(extension.extensionName);
  }

  std::cout << "required extensions:" << std::endl;
  auto requiredExtensions = getRequiredExtensions();
  for (const auto &required : requiredExtensions) {
    std::cout << "\t" << required << std::endl;
    if (available.find(required) == available.end()) {
      throw std::runtime_error("missing required Vulkan surface-host extension");
    }
  }
}

bool LveDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) {
  uint32_t extensionCount;
  checkVk(
      vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr),
      "vkEnumerateDeviceExtensionProperties(required count)");

  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  checkVk(
      vkEnumerateDeviceExtensionProperties(
          device,
          nullptr,
          &extensionCount,
          availableExtensions.data()),
      "vkEnumerateDeviceExtensionProperties(required data)");

  std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

  for (const auto &extension : availableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  return requiredExtensions.empty();
}

QueueFamilyIndices LveDevice::findQueueFamilies(VkPhysicalDevice device) {
  QueueFamilyIndices indices{};

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

  uint32_t i = 0;
  for (const auto &queueFamily : queueFamilies) {
    if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphicsFamily = i;
      indices.graphicsFamilyHasValue = true;
    }
    VkBool32 presentSupport = false;
    checkVk(
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport),
        "vkGetPhysicalDeviceSurfaceSupportKHR");
    if (queueFamily.queueCount > 0 && presentSupport) {
      indices.presentFamily = i;
      indices.presentFamilyHasValue = true;
    }
    if (indices.isComplete()) {
      break;
    }

    i++;
  }

  return indices;
}

SwapChainSupportDetails LveDevice::querySwapChainSupport(VkPhysicalDevice device) {
  SwapChainSupportDetails details{};
  checkVk(
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities),
      "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

  uint32_t formatCount = 0;
  checkVk(
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr),
      "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");

  if (formatCount != 0) {
    details.formats.resize(formatCount);
    checkVk(
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data()),
        "vkGetPhysicalDeviceSurfaceFormatsKHR(data)");
  }

  uint32_t presentModeCount = 0;
  checkVk(
      vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr),
      "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");

  if (presentModeCount != 0) {
    details.presentModes.resize(presentModeCount);
    checkVk(
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device,
            surface_,
            &presentModeCount,
            details.presentModes.data()),
        "vkGetPhysicalDeviceSurfacePresentModesKHR(data)");
  }
  return details;
}

VkFormat LveDevice::findSupportedFormat(
    const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

    if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (
        tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }
  throw std::runtime_error("failed to find supported format!");
}

uint32_t LveDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void LveDevice::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer &buffer,
    VkDeviceMemory &bufferMemory) {
  buffer = VK_NULL_HANDLE;
  bufferMemory = VK_NULL_HANDLE;
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  checkVk(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device_, buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

  try {
    checkVk(vkAllocateMemory(device_, &allocInfo, nullptr, &bufferMemory), "vkAllocateMemory(buffer)");
    checkVk(vkBindBufferMemory(device_, buffer, bufferMemory, 0), "vkBindBufferMemory");
  } catch (...) {
    if (bufferMemory != VK_NULL_HANDLE) {
      vkFreeMemory(device_, bufferMemory, nullptr);
      bufferMemory = VK_NULL_HANDLE;
    }
    vkDestroyBuffer(device_, buffer, nullptr);
    buffer = VK_NULL_HANDLE;
    throw;
  }
}

VkCommandBuffer LveDevice::beginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  checkVk(
      vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer),
      "vkAllocateCommandBuffers(one-shot)");

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  try {
    checkVk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer(one-shot)");
  } catch (...) {
    vkFreeCommandBuffers(device_, commandPool, 1, &commandBuffer);
    throw;
  }
  return commandBuffer;
}

void LveDevice::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  checkVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(one-shot)");

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence completionFence = VK_NULL_HANDLE;
  checkVk(vkCreateFence(device_, &fenceInfo, nullptr, &completionFence), "vkCreateFence(one-shot)");

  try {
    checkVk(
        vkQueueSubmit(graphicsQueue_, 1, &submitInfo, completionFence),
        "vkQueueSubmit(one-shot)");
    checkVk(
        vkWaitForFences(device_, 1, &completionFence, VK_TRUE, UINT64_MAX),
        "vkWaitForFences(one-shot)");
  } catch (...) {
    vkDestroyFence(device_, completionFence, nullptr);
    vkFreeCommandBuffers(device_, commandPool, 1, &commandBuffer);
    throw;
  }

  vkDestroyFence(device_, completionFence, nullptr);
  vkFreeCommandBuffers(device_, commandPool, 1, &commandBuffer);
}

void LveDevice::submitImmediate(const std::function<void(VkCommandBuffer)>& record) {
  std::scoped_lock lock{immediateSubmitMutex};
  checkVk(vkResetFences(device_, 1, &immediateFence), "vkResetFences(immediate)");
  checkVk(vkResetCommandBuffer(immediateCommandBuffer, 0), "vkResetCommandBuffer(immediate)");

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  checkVk(vkBeginCommandBuffer(immediateCommandBuffer, &beginInfo), "vkBeginCommandBuffer(immediate)");
  record(immediateCommandBuffer);
  checkVk(vkEndCommandBuffer(immediateCommandBuffer), "vkEndCommandBuffer(immediate)");

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &immediateCommandBuffer;
  checkVk(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, immediateFence), "vkQueueSubmit(immediate)");
  checkVk(
      vkWaitForFences(device_, 1, &immediateFence, VK_TRUE, UINT64_MAX),
      "vkWaitForFences(immediate)");
}

void LveDevice::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
  submitImmediate([&](VkCommandBuffer commandBuffer) {
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
  });
}

void LveDevice::copyBufferToImage(
    VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount) {
  submitImmediate([&](VkCommandBuffer commandBuffer) {
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = layerCount;
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
  });
}

void LveDevice::createImageWithInfo(
    const VkImageCreateInfo &imageInfo,
    VkMemoryPropertyFlags properties,
    VkImage &image,
    VkDeviceMemory &imageMemory) {
  image = VK_NULL_HANDLE;
  imageMemory = VK_NULL_HANDLE;
  checkVk(vkCreateImage(device_, &imageInfo, nullptr, &image), "vkCreateImage");

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device_, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

  try {
    checkVk(vkAllocateMemory(device_, &allocInfo, nullptr, &imageMemory), "vkAllocateMemory(image)");
    checkVk(vkBindImageMemory(device_, image, imageMemory, 0), "vkBindImageMemory");
  } catch (...) {
    if (imageMemory != VK_NULL_HANDLE) {
      vkFreeMemory(device_, imageMemory, nullptr);
      imageMemory = VK_NULL_HANDLE;
    }
    vkDestroyImage(device_, image, nullptr);
    image = VK_NULL_HANDLE;
    throw;
  }
}

}  // namespace lve
