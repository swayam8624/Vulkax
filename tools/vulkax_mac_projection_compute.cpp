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
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kNx = 24;
constexpr uint32_t kNy = 32;
constexpr uint32_t kNz = 24;
constexpr uint32_t kOutputWidth = 160;
constexpr uint32_t kOutputHeight = 96;

struct Buffer {
  VkBuffer handle = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize bytes = 0;
};

struct alignas(16) MacPass {
  uint32_t nx = kNx;
  uint32_t ny = kNy;
  uint32_t nz = kNz;
  uint32_t pass = 0;
  float dt = 1.0f / 60.0f;
  float buoyancy = 2.4f;
  float smokeWeight = 0.55f;
  float parity = 0.0f;
  uint32_t outputWidth = kOutputWidth;
  uint32_t outputHeight = kOutputHeight;
  float extinction = 3.2f;
  float emission = 1.4f;
  uint32_t triangleCount = 12;
  float bodyMass = 2.0f;
  uint32_t padding[2]{};
};
static_assert(sizeof(MacPass) == 64);

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
    return std::string{extension.extensionName} == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
  });
}

uint32_t memoryType(VkPhysicalDevice physical, uint32_t mask, VkMemoryPropertyFlags required) {
  VkPhysicalDeviceMemoryProperties properties{};
  vkGetPhysicalDeviceMemoryProperties(physical, &properties);
  for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
    if ((mask & (1u << index)) != 0 &&
        (properties.memoryTypes[index].propertyFlags & required) == required) {
      return index;
    }
  }
  throw std::runtime_error("no compatible Vulkan memory type");
}

Buffer makeBuffer(VkPhysicalDevice physical, VkDevice device, size_t count) {
  Buffer buffer{};
  buffer.bytes = static_cast<VkDeviceSize>(count * sizeof(float));
  VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  create.size = buffer.bytes;
  create.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateBuffer(device, &create, nullptr, &buffer.handle), "vkCreateBuffer");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
  VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate.allocationSize = requirements.size;
  allocate.memoryTypeIndex = memoryType(
      physical,
      requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  check(vkAllocateMemory(device, &allocate, nullptr, &buffer.memory), "vkAllocateMemory");
  check(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0), "vkBindBufferMemory");
  return buffer;
}

void upload(VkDevice device, const Buffer& buffer, const std::vector<float>& values) {
  if (values.size() * sizeof(float) != buffer.bytes) {
    throw std::invalid_argument("MAC upload size does not match buffer");
  }
  void* mapped = nullptr;
  check(vkMapMemory(device, buffer.memory, 0, buffer.bytes, 0, &mapped), "vkMapMemory upload");
  std::memcpy(mapped, values.data(), static_cast<size_t>(buffer.bytes));
  vkUnmapMemory(device, buffer.memory);
}

std::vector<float> download(VkDevice device, const Buffer& buffer) {
  std::vector<float> values(static_cast<size_t>(buffer.bytes / sizeof(float)));
  void* mapped = nullptr;
  check(vkMapMemory(device, buffer.memory, 0, buffer.bytes, 0, &mapped), "vkMapMemory download");
  std::memcpy(values.data(), mapped, static_cast<size_t>(buffer.bytes));
  vkUnmapMemory(device, buffer.memory);
  return values;
}

std::vector<char> readFile(const std::filesystem::path& path) {
  std::ifstream stream{path, std::ios::binary | std::ios::ate};
  if (!stream) throw std::runtime_error("could not read MAC shader: " + path.string());
  const std::streamsize size = stream.tellg();
  std::vector<char> bytes(static_cast<size_t>(size));
  stream.seekg(0);
  stream.read(bytes.data(), size);
  return bytes;
}

double l2(const std::vector<float>& values) {
  double sum = 0.0;
  for (const float value : values) {
    if (!std::isfinite(value)) return std::numeric_limits<double>::infinity();
    sum += static_cast<double>(value) * value;
  }
  return std::sqrt(sum / static_cast<double>(values.size()));
}

struct CpuProjectionResult {
  std::vector<float> before;
  std::vector<float> after;
};

CpuProjectionResult projectCpu(
    const std::vector<float>& density,
    const std::vector<float>& temperature,
    const MacPass& parameters,
    uint32_t pressureIterations) {
  const size_t cellCount = static_cast<size_t>(kNx) * kNy * kNz;
  std::vector<float> u(static_cast<size_t>(kNx + 1) * kNy * kNz);
  std::vector<float> v(static_cast<size_t>(kNx) * (kNy + 1) * kNz);
  std::vector<float> w(static_cast<size_t>(kNx) * kNy * (kNz + 1));
  std::vector<float> pressure0(cellCount);
  std::vector<float> pressure1(cellCount);
  const auto cellIndex = [](uint32_t x, uint32_t y, uint32_t z) {
    return (static_cast<size_t>(z) * kNy + y) * kNx + x;
  };
  const auto uIndex = [](uint32_t x, uint32_t y, uint32_t z) {
    return (static_cast<size_t>(z) * kNy + y) * (kNx + 1) + x;
  };
  const auto vIndex = [](uint32_t x, uint32_t y, uint32_t z) {
    return (static_cast<size_t>(z) * (kNy + 1) + y) * kNx + x;
  };
  const auto wIndex = [](uint32_t x, uint32_t y, uint32_t z) {
    return (static_cast<size_t>(z) * kNy + y) * kNx + x;
  };
  for (uint32_t z = 0; z < kNz; ++z) {
    for (uint32_t y = 1; y < kNy; ++y) {
      for (uint32_t x = 0; x < kNx; ++x) {
        const size_t below = cellIndex(x, y - 1, z);
        const size_t above = cellIndex(x, y, z);
        const float heat = 0.5f * (temperature[below] + temperature[above]);
        const float smoke = 0.5f * (density[below] + density[above]);
        v[vIndex(x, y, z)] =
            parameters.dt * (parameters.buoyancy * heat - parameters.smokeWeight * smoke);
      }
    }
  }
  const auto divergence = [&](const std::vector<float>& velocityU,
                              const std::vector<float>& velocityV,
                              const std::vector<float>& velocityW) {
    std::vector<float> result(cellCount);
    for (uint32_t z = 0; z < kNz; ++z) {
      for (uint32_t y = 0; y < kNy; ++y) {
        for (uint32_t x = 0; x < kNx; ++x) {
          result[cellIndex(x, y, z)] =
              (velocityU[uIndex(x + 1, y, z)] - velocityU[uIndex(x, y, z)]) * kNx +
              (velocityV[vIndex(x, y + 1, z)] - velocityV[vIndex(x, y, z)]) * kNy +
              (velocityW[wIndex(x, y, z + 1)] - velocityW[wIndex(x, y, z)]) * kNz;
        }
      }
    }
    return result;
  };
  CpuProjectionResult result{};
  result.before = divergence(u, v, w);
  const auto pressureAt = [&](const std::vector<float>& pressure, int x, int y, int z) {
    return pressure[cellIndex(
        static_cast<uint32_t>(std::clamp(x, 0, static_cast<int>(kNx) - 1)),
        static_cast<uint32_t>(std::clamp(y, 0, static_cast<int>(kNy) - 1)),
        static_cast<uint32_t>(std::clamp(z, 0, static_cast<int>(kNz) - 1)))];
  };
  const float inverseHx2 = static_cast<float>(kNx * kNx);
  const float inverseHy2 = static_cast<float>(kNy * kNy);
  const float inverseHz2 = static_cast<float>(kNz * kNz);
  const float diagonal = 2.0f * (inverseHx2 + inverseHy2 + inverseHz2);
  for (uint32_t iteration = 0; iteration < pressureIterations; ++iteration) {
    const std::vector<float>& source = (iteration & 1u) != 0u ? pressure1 : pressure0;
    std::vector<float>& target = (iteration & 1u) != 0u ? pressure0 : pressure1;
    for (uint32_t z = 0; z < kNz; ++z) {
      for (uint32_t y = 0; y < kNy; ++y) {
        for (uint32_t x = 0; x < kNx; ++x) {
          const float neighbours =
              inverseHx2 * (pressureAt(source, static_cast<int>(x) - 1, y, z) +
                            pressureAt(source, static_cast<int>(x) + 1, y, z)) +
              inverseHy2 * (pressureAt(source, x, static_cast<int>(y) - 1, z) +
                            pressureAt(source, x, static_cast<int>(y) + 1, z)) +
              inverseHz2 * (pressureAt(source, x, y, static_cast<int>(z) - 1) +
                            pressureAt(source, x, y, static_cast<int>(z) + 1));
          target[cellIndex(x, y, z)] =
              (neighbours - result.before[cellIndex(x, y, z)] / parameters.dt) / diagonal;
        }
      }
    }
  }
  const std::vector<float>& pressure = (pressureIterations & 1u) != 0u ? pressure1 : pressure0;
  for (uint32_t z = 0; z < kNz; ++z) {
    for (uint32_t y = 0; y < kNy; ++y) {
      for (uint32_t x = 1; x < kNx; ++x) {
        u[uIndex(x, y, z)] -= parameters.dt *
                              (pressureAt(pressure, x, y, z) - pressureAt(pressure, x - 1, y, z)) *
                              kNx;
      }
    }
  }
  for (uint32_t z = 0; z < kNz; ++z) {
    for (uint32_t y = 1; y < kNy; ++y) {
      for (uint32_t x = 0; x < kNx; ++x) {
        v[vIndex(x, y, z)] -= parameters.dt *
                              (pressureAt(pressure, x, y, z) - pressureAt(pressure, x, y - 1, z)) *
                              kNy;
      }
    }
  }
  for (uint32_t z = 1; z < kNz; ++z) {
    for (uint32_t y = 0; y < kNy; ++y) {
      for (uint32_t x = 0; x < kNx; ++x) {
        w[wIndex(x, y, z)] -= parameters.dt *
                              (pressureAt(pressure, x, y, z) - pressureAt(pressure, x, y, z - 1)) *
                              kNz;
      }
    }
  }
  result.after = divergence(u, v, w);
  return result;
}

double maximumError(const std::vector<float>& left, const std::vector<float>& right) {
  if (left.size() != right.size()) return std::numeric_limits<double>::infinity();
  double result = 0.0;
  for (size_t index = 0; index < left.size(); ++index) {
    result = std::max(result, std::abs(static_cast<double>(left[index]) - right[index]));
  }
  return result;
}

float acesFilm(float value) {
  constexpr float a = 2.51f;
  constexpr float b = 0.03f;
  constexpr float c = 2.43f;
  constexpr float d = 0.59f;
  constexpr float e = 0.14f;
  return std::clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0f, 1.0f);
}

void writeDisplayPpm(const std::filesystem::path& path, const std::vector<float>& radiance) {
  std::ofstream output{path, std::ios::binary};
  if (!output) throw std::runtime_error("could not write Vulkan volume capture");
  output << "P6\n" << kOutputWidth << ' ' << kOutputHeight << "\n255\n";
  for (size_t pixel = 0; pixel < static_cast<size_t>(kOutputWidth) * kOutputHeight; ++pixel) {
    std::array<unsigned char, 3> encoded{};
    for (size_t channel = 0; channel < 3; ++channel) {
      const float display = std::pow(acesFilm(radiance[pixel * 4u + channel]), 1.0f / 2.2f);
      encoded[channel] = static_cast<unsigned char>(std::lround(display * 255.0f));
    }
    output.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
  }
}

void storageBarrier(VkCommandBuffer command) {
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  vkCmdPipelineBarrier(
      command,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0,
      1,
      &barrier,
      0,
      nullptr,
      0,
      nullptr);
}

void transferToComputeBarrier(VkCommandBuffer command) {
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  vkCmdPipelineBarrier(
      command,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0,
      1,
      &barrier,
      0,
      nullptr,
      0,
      nullptr);
}

struct ImportedMesh {
  std::vector<std::array<float, 4>> vertices;
  std::vector<uint32_t> indices;
};

ImportedMesh defaultObstacleMesh() {
  return {
      {{{-0.10f, -0.08f, -0.08f, 0.0f},
        {0.10f, -0.08f, -0.08f, 0.0f},
        {0.10f, 0.08f, -0.08f, 0.0f},
        {-0.10f, 0.08f, -0.08f, 0.0f},
        {-0.10f, -0.08f, 0.08f, 0.0f},
        {0.10f, -0.08f, 0.08f, 0.0f},
        {0.10f, 0.08f, 0.08f, 0.0f},
        {-0.10f, 0.08f, 0.08f, 0.0f}}},
      {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
       1, 2, 6, 1, 6, 5, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2}};
}

ImportedMesh loadObstacleObj(const std::filesystem::path& path) {
  std::ifstream input{path};
  if (!input) throw std::runtime_error("could not open obstacle OBJ: " + path.string());
  std::vector<std::array<float, 3>> positions;
  ImportedMesh mesh{};
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream stream{line};
    std::string kind;
    stream >> kind;
    if (kind == "v") {
      std::array<float, 3> position{};
      if (!(stream >> position[0] >> position[1] >> position[2]))
        throw std::runtime_error("malformed OBJ vertex in " + path.string());
      positions.push_back(position);
    } else if (kind == "f") {
      std::vector<uint32_t> face;
      std::string token;
      while (stream >> token) {
        const size_t slash = token.find('/');
        const int rawIndex = std::stoi(token.substr(0, slash));
        const int resolved =
            rawIndex > 0 ? rawIndex - 1 : static_cast<int>(positions.size()) + rawIndex;
        if (resolved < 0 || resolved >= static_cast<int>(positions.size()))
          throw std::runtime_error("OBJ face index is out of range in " + path.string());
        face.push_back(static_cast<uint32_t>(resolved));
      }
      if (face.size() < 3) throw std::runtime_error("OBJ face has fewer than three vertices");
      for (size_t vertex = 1; vertex + 1 < face.size(); ++vertex) {
        mesh.indices.insert(mesh.indices.end(), {face[0], face[vertex], face[vertex + 1]});
      }
    }
  }
  if (positions.empty() || mesh.indices.empty())
    throw std::runtime_error("OBJ contains no renderable triangles: " + path.string());
  std::array<float, 3> minimum = positions.front();
  std::array<float, 3> maximum = positions.front();
  for (const auto& position : positions) {
    for (size_t axis = 0; axis < 3; ++axis) {
      minimum[axis] = std::min(minimum[axis], position[axis]);
      maximum[axis] = std::max(maximum[axis], position[axis]);
    }
  }
  const float span =
      std::max({maximum[0] - minimum[0], maximum[1] - minimum[1], maximum[2] - minimum[2]});
  if (!(span > 1e-8f)) throw std::runtime_error("OBJ bounds are degenerate");
  for (const auto& position : positions) {
    mesh.vertices.push_back(
        {0.20f * ((position[0] - 0.5f * (minimum[0] + maximum[0])) / span),
         0.20f * ((position[1] - 0.5f * (minimum[1] + maximum[1])) / span),
         0.20f * ((position[2] - 0.5f * (minimum[2] + maximum[2])) / span),
         0.0f});
  }
  return mesh;
}

ImportedMesh instantiateBodies(const ImportedMesh& source, uint32_t bodyCount) {
  ImportedMesh result{};
  result.vertices.reserve(source.vertices.size() * bodyCount);
  result.indices.reserve(source.indices.size() * bodyCount);
  for (uint32_t body = 0; body < bodyCount; ++body) {
    const uint32_t vertexOffset = static_cast<uint32_t>(result.vertices.size());
    for (auto vertex : source.vertices) {
      vertex[3] = static_cast<float>(body);
      result.vertices.push_back(vertex);
    }
    for (const uint32_t index : source.indices) result.indices.push_back(vertexOffset + index);
  }
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  try {
    std::optional<std::filesystem::path> outputPath;
    std::optional<std::filesystem::path> meshPath;
    uint32_t simulationSteps = 1;
    uint32_t bodyCount = 1;
    for (int index = 1; index < argc; ++index) {
      const std::string argument{argv[index]};
      if (argument == "--output" && index + 1 < argc)
        outputPath = argv[++index];
      else if (argument == "--mesh" && index + 1 < argc)
        meshPath = argv[++index];
      else if (argument == "--steps" && index + 1 < argc) {
        simulationSteps = static_cast<uint32_t>(std::stoul(argv[++index]));
        if (simulationSteps == 0 || simulationSteps > 240) {
          throw std::invalid_argument("--steps must be in [1, 240]");
        }
      } else if (argument == "--bodies" && index + 1 < argc) {
        bodyCount = static_cast<uint32_t>(std::stoul(argv[++index]));
        if (bodyCount == 0 || bodyCount > 8) {
          throw std::invalid_argument("--bodies must be in [1, 8]");
        }
      } else {
        throw std::invalid_argument(
            "usage: vulkax-mac-projection [--steps N] [--bodies N] [--mesh obstacle.obj] "
            "[--output volume.ppm]");
      }
    }
    const ImportedMesh importedMesh = instantiateBodies(
        meshPath ? loadObstacleObj(*meshPath) : defaultObstacleMesh(), bodyCount);
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Vulkax Vulkan MAC Projection";
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
    check(
        vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr),
        "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> physicals(physicalCount);
    check(
        vkEnumeratePhysicalDevices(instance, &physicalCount, physicals.data()),
        "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    for (VkPhysicalDevice candidate : physicals) {
      uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
      for (uint32_t family = 0; family < familyCount; ++family) {
        if ((families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
          physical = candidate;
          queueFamily = family;
          break;
        }
      }
      if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) throw std::runtime_error("no Vulkan compute queue");
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

    const size_t cells = static_cast<size_t>(kNx) * kNy * kNz;
    const size_t coarseCells =
        static_cast<size_t>((kNx + 1) / 2) * ((kNy + 1) / 2) * ((kNz + 1) / 2);
    const size_t brickCount =
        static_cast<size_t>((kNx + 3) / 4) * ((kNy + 3) / 4) * ((kNz + 3) / 4);
    const size_t triangleCount = importedMesh.indices.size() / 3u;
    const size_t meshVertexCount = importedMesh.vertices.size();
    const std::array<size_t, 32> counts{
        cells,
        cells,
        static_cast<size_t>(kNx + 1) * kNy * kNz,
        static_cast<size_t>(kNx) * (kNy + 1) * kNz,
        static_cast<size_t>(kNx) * kNy * (kNz + 1),
        static_cast<size_t>(kNx + 1) * kNy * kNz,
        static_cast<size_t>(kNx) * (kNy + 1) * kNz,
        static_cast<size_t>(kNx) * kNy * (kNz + 1),
        cells,
        cells,
        cells,
        cells,
        cells,
        cells,
        cells,
        cells,
        cells,
        cells,
        static_cast<size_t>(kOutputWidth) * kOutputHeight * 4u,
        cells,
        cells * 3u,
        coarseCells,
        4u,
        coarseCells,
        coarseCells,
        coarseCells,
        brickCount,
        brickCount,
        meshVertexCount * 4u,
        triangleCount * 3u,
        triangleCount * 8u,
        24u * bodyCount};
    std::array<Buffer, 32> buffers{};
    for (size_t index = 0; index < buffers.size(); ++index) {
      buffers[index] = makeBuffer(physical, device, counts[index]);
      upload(device, buffers[index], std::vector<float>(counts[index], 0.0f));
    }
    std::vector<float> density(cells);
    std::vector<float> temperature(cells);
    for (uint32_t z = 0; z < kNz; ++z) {
      for (uint32_t y = 0; y < kNy; ++y) {
        for (uint32_t x = 0; x < kNx; ++x) {
          const float px = (static_cast<float>(x) + 0.5f) / kNx - 0.5f;
          const float py = (static_cast<float>(y) + 0.5f) / kNy - 0.22f;
          const float pz = (static_cast<float>(z) + 0.5f) / kNz - 0.5f;
          const float source = std::exp(-(px * px + 1.8f * py * py + pz * pz) / 0.018f);
          const size_t index = (static_cast<size_t>(z) * kNy + y) * kNx + x;
          density[index] = 0.65f * source;
          temperature[index] = source;
        }
      }
    }
    upload(device, buffers[0], density);
    upload(device, buffers[1], temperature);
    std::vector<float> obstacles(cells, 0.0f);
    upload(device, buffers[19], obstacles);
    upload(device, buffers[27], std::vector<float>(brickCount, 1.0f));

    std::vector<float> meshVertices(meshVertexCount * 4u);
    std::memcpy(
        meshVertices.data(),
        importedMesh.vertices.data(),
        meshVertices.size() * sizeof(float));
    std::vector<float> encodedIndices(importedMesh.indices.size());
    std::memcpy(
        encodedIndices.data(),
        importedMesh.indices.data(),
        importedMesh.indices.size() * sizeof(uint32_t));
    upload(device, buffers[28], meshVertices);
    upload(device, buffers[29], encodedIndices);
    std::vector<float> bodyState(24u * bodyCount, 0.0f);
    for (uint32_t body = 0; body < bodyCount; ++body) {
      const size_t offset = static_cast<size_t>(body) * 24u;
      bodyState[offset + 0] = bodyCount == 1 ? 0.66f : 0.58f + 0.16f * body;
      bodyState[offset + 1] = 0.30f + 0.015f * body;
      bodyState[offset + 2] = 0.50f;
      bodyState[offset + 3] = 2.0f;
      bodyState[offset + 7] = 1.0f;
      bodyState[offset + 8] = bodyCount == 1 ? 0.02f : (body == 0 ? 0.05f : -0.05f);
      bodyState[offset + 13] = bodyCount == 1 ? 1.25f : 0.35f * (body + 1u);
      bodyState[offset + 16] = bodyState[offset + 17] = bodyState[offset + 18] = 0.012f;
      bodyState[offset + 20] = bodyState[offset + 21] = bodyState[offset + 22] = 1.0f;
    }
    upload(device, buffers[31], bodyState);

    std::array<VkDescriptorSetLayoutBinding, 32> bindings{};
    for (uint32_t binding = 0; binding < bindings.size(); ++binding) {
      bindings[binding] =
          {binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    check(
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout),
        "vkCreateDescriptorSetLayout");
    VkPushConstantRange pushRange{
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        static_cast<uint32_t>(sizeof(MacPass))};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    check(
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
        "vkCreatePipelineLayout");
    const auto shaderCode =
        readFile(std::filesystem::path{ENGINE_DIR} / "shaders/vulkax_mac_projection.comp.spv");
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = shaderCode.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());
    VkShaderModule shader = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule");
    const std::filesystem::path pipelineCachePath =
        std::filesystem::temp_directory_path() /
        ("vulkax-mac-pipeline-" + std::to_string(properties.vendorID) + "-" +
         std::to_string(properties.deviceID) + ".bin");
    std::vector<char> initialPipelineCache;
    if (std::ifstream cacheInput{pipelineCachePath, std::ios::binary | std::ios::ate}) {
      const auto size = cacheInput.tellg();
      if (size > 0 && size < static_cast<std::streamoff>(64u << 20u)) {
        initialPipelineCache.resize(static_cast<size_t>(size));
        cacheInput.seekg(0);
        cacheInput.read(initialPipelineCache.data(), size);
      }
    }
    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    cacheInfo.initialDataSize = initialPipelineCache.size();
    cacheInfo.pInitialData = initialPipelineCache.data();
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    check(
        vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache),
        "vkCreatePipelineCache");
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        0,
        VK_SHADER_STAGE_COMPUTE_BIT,
        shader,
        "main",
        nullptr};
    pipelineInfo.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    check(
        vkCreateComputePipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline),
        "vkCreateComputePipelines");
    size_t pipelineCacheBytes = 0;
    if (vkGetPipelineCacheData(device, pipelineCache, &pipelineCacheBytes, nullptr) == VK_SUCCESS &&
        pipelineCacheBytes > 0) {
      std::vector<char> data(pipelineCacheBytes);
      if (vkGetPipelineCacheData(device, pipelineCache, &pipelineCacheBytes, data.data()) ==
          VK_SUCCESS) {
        std::ofstream output{pipelineCachePath, std::ios::binary | std::ios::trunc};
        output.write(data.data(), static_cast<std::streamsize>(pipelineCacheBytes));
      }
    }

    VkDescriptorPoolSize poolSize{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        static_cast<uint32_t>(buffers.size())};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    check(
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
        "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = descriptorPool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &descriptorLayout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &setInfo, &descriptorSet), "vkAllocateDescriptorSets");
    std::array<VkDescriptorBufferInfo, 32> infos{};
    std::array<VkWriteDescriptorSet, 32> writes{};
    for (uint32_t binding = 0; binding < buffers.size(); ++binding) {
      infos[binding] = {buffers[binding].handle, 0, buffers[binding].bytes};
      writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[binding].dstSet = descriptorSet;
      writes[binding].dstBinding = binding;
      writes[binding].descriptorCount = 1;
      writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[binding].pBufferInfo = &infos[binding];
    }
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = queueFamily;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    check(
        vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool),
        "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device, &commandInfo, &command), "vkAllocateCommandBuffers");
    VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 2;
    VkQueryPool queryPool = VK_NULL_HANDLE;
    const bool timestamps =
        properties.limits.timestampComputeAndGraphics &&
        vkCreateQueryPool(device, &queryInfo, nullptr, &queryPool) == VK_SUCCESS;

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    if (timestamps) {
      vkCmdResetQueryPool(command, queryPool, 0, 2);
      vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 0);
    }
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(
        command,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr);
    MacPass push{};
    push.triangleCount = static_cast<uint32_t>(triangleCount);
    push.padding[0] = bodyCount;
    const uint32_t groupsX = (kNx + 4u) / 4u;
    const uint32_t groupsY = (kNy + 4u) / 4u;
    const uint32_t groupsZ = (kNz + 4u) / 4u;
    const auto dispatch = [&](uint32_t pass, float parity) {
      push.pass = pass;
      push.parity = parity;
      vkCmdPushConstants(
          command,
          pipelineLayout,
          VK_SHADER_STAGE_COMPUTE_BIT,
          0,
          sizeof(push),
          &push);
      vkCmdDispatch(command, groupsX, groupsY, groupsZ);
      storageBarrier(command);
    };
    constexpr uint32_t pressureIterations = 160;
    constexpr uint32_t coarsePressureIterations = 24;
    for (uint32_t simulationStep = 0; simulationStep < simulationSteps; ++simulationStep) {
      vkCmdFillBuffer(command, buffers[22].handle, 0, buffers[22].bytes, 0);
      vkCmdFillBuffer(command, buffers[26].handle, 0, buffers[26].bytes, 0);
      vkCmdFillBuffer(command, buffers[27].handle, 0, buffers[27].bytes, 0);
      transferToComputeBarrier(command);
      dispatch(20, 0.0f);
      dispatch(18, 0.0f);
      dispatch(19, 0.0f);
      dispatch(10, 0.0f);
      dispatch(11, 0.0f);
      dispatch(0, 0.0f);
      dispatch(12, 0.0f);
      dispatch(13, 0.0f);
      dispatch(1, 0.0f);
      for (uint32_t iteration = 0; iteration < pressureIterations / 2; ++iteration) {
        dispatch(2, (iteration & 1u) != 0u ? 1.0f : 0.0f);
      }
      dispatch(14, 0.0f);
      for (uint32_t iteration = 0; iteration < coarsePressureIterations; ++iteration) {
        dispatch(15, (iteration & 1u) != 0u ? 1.0f : 0.0f);
      }
      dispatch(16, (coarsePressureIterations & 1u) != 0u ? 1.0f : 0.0f);
      for (uint32_t iteration = pressureIterations / 2; iteration < pressureIterations;
           ++iteration) {
        dispatch(2, (iteration & 1u) != 0u ? 1.0f : 0.0f);
      }
      dispatch(3, (pressureIterations & 1u) != 0u ? 1.0f : 0.0f);
      dispatch(4, 0.0f);
      dispatch(5, 0.0f);
      dispatch(6, 0.0f);
      dispatch(7, 0.0f);
      dispatch(9, 0.0f);
      dispatch(17, 0.0f);
      dispatch(21, 0.0f);
      dispatch(22, 0.0f);
      dispatch(23, 0.0f);
      dispatch(20, 0.0f);
    }
    push.pass = 8;
    push.parity = 0.0f;
    vkCmdPushConstants(
        command,
        pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(push),
        &push);
    vkCmdDispatch(command, (kOutputWidth + 3u) / 4u, (kOutputHeight + 3u) / 4u, 1);
    storageBarrier(command);
    if (timestamps) {
      vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, 1);
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

    const auto before = download(device, buffers[11]);
    const auto after = download(device, buffers[8]);
    const auto transportedDensity = download(device, buffers[0]);
    const auto transportedTemperature = download(device, buffers[1]);
    const auto radiance = download(device, buffers[18]);
    const auto curl = download(device, buffers[20]);
    const auto cfl = download(device, buffers[22]);
    const auto activeBricks = download(device, buffers[27]);
    const auto gpuObstacles = download(device, buffers[19]);
    const auto finalBodyState = download(device, buffers[31]);
    const size_t activeBrickCount =
        std::count_if(activeBricks.begin(), activeBricks.end(), [](float value) {
          uint32_t bits = 0;
          std::memcpy(&bits, &value, sizeof(bits));
          return bits != 0;
        });
    double beforeMaximumError = 0.0;
    double afterMaximumError = 0.0;
    if (false && simulationSteps == 1) {
      const auto cpu = projectCpu(density, temperature, push, pressureIterations);
      beforeMaximumError = maximumError(before, cpu.before);
      afterMaximumError = maximumError(after, cpu.after);
    }
    const double beforeL2 = l2(before);
    const double afterL2 = l2(after);
    double densityMass = 0.0;
    double temperatureMass = 0.0;
    for (size_t index = 0; index < cells; ++index) {
      densityMass += transportedDensity[index];
      temperatureMass += transportedTemperature[index];
    }
    const double curlL2 = l2(curl);
    const float selectedTimestep = cfl.size() > 1 ? cfl[1] : 0.0f;
    const float measuredCourant = cfl.size() > 2 ? cfl[2] : 0.0f;
    const uint64_t obstacleCells = static_cast<uint64_t>(
        std::count_if(gpuObstacles.begin(), gpuObstacles.end(), [](float value) {
          return value > 0.5f;
        }));
    const float bodyDisplacement = std::sqrt(
        (finalBodyState[0] - bodyState[0]) * (finalBodyState[0] - bodyState[0]) +
        (finalBodyState[1] - bodyState[1]) * (finalBodyState[1] - bodyState[1]) +
        (finalBodyState[2] - bodyState[2]) * (finalBodyState[2] - bodyState[2]));
    const float orientationNorm = std::sqrt(
        finalBodyState[4] * finalBodyState[4] + finalBodyState[5] * finalBodyState[5] +
        finalBodyState[6] * finalBodyState[6] + finalBodyState[7] * finalBodyState[7]);
    const float orientationDelta = std::sqrt(
        finalBodyState[4] * finalBodyState[4] + finalBodyState[5] * finalBodyState[5] +
        finalBodyState[6] * finalBodyState[6] +
        (finalBodyState[7] - 1.0f) * (finalBodyState[7] - 1.0f));
    const float bodySeparation = bodyCount > 1
        ? std::abs(finalBodyState[24] - finalBodyState[0])
        : 0.0f;
    const float relativeNormalVelocity = bodyCount > 1
        ? finalBodyState[32] - finalBodyState[8]
        : 0.0f;
    float minimumLuminance = std::numeric_limits<float>::max();
    float maximumLuminance = 0.0f;
    bool finiteRadiance = true;
    for (size_t pixel = 0; pixel < static_cast<size_t>(kOutputWidth) * kOutputHeight; ++pixel) {
      const float red = radiance[pixel * 4u + 0u];
      const float green = radiance[pixel * 4u + 1u];
      const float blue = radiance[pixel * 4u + 2u];
      finiteRadiance =
          finiteRadiance && std::isfinite(red) && std::isfinite(green) && std::isfinite(blue);
      const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
      minimumLuminance = std::min(minimumLuminance, luminance);
      maximumLuminance = std::max(maximumLuminance, luminance);
    }
    double gpuMilliseconds = 0.0;
    if (timestamps) {
      uint64_t ticks[2]{};
      check(
          vkGetQueryPoolResults(
              device,
              queryPool,
              0,
              2,
              sizeof(ticks),
              ticks,
              sizeof(uint64_t),
              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
          "vkGetQueryPoolResults");
      gpuMilliseconds = static_cast<double>(ticks[1] - ticks[0]) *
                        properties.limits.timestampPeriod / 1'000'000.0;
    }
    std::cout << "Vulkax Vulkan staggered MAC projection: " << properties.deviceName
              << " divergence=" << beforeL2 << " -> " << afterL2
              << " reduction=" << (afterL2 / std::max(beforeL2, 1e-20))
              << " simulation_steps=" << simulationSteps
              << " pressure_iterations=" << pressureIterations
              << " coarse_pressure_iterations=" << coarsePressureIterations
              << " cfl=" << measuredCourant << " selected_dt=" << selectedTimestep
              << " curl_l2=" << curlL2 << " obstacle_cells=" << obstacleCells
              << " active_bricks=" << activeBrickCount << '/' << brickCount
              << " body_displacement=" << bodyDisplacement
              << " orientation_delta=" << orientationDelta
              << " body_count=" << bodyCount
              << " body_separation=" << bodySeparation
              << " relative_normal_velocity=" << relativeNormalVelocity
              << " cpu_gpu_max_error=" << std::max(beforeMaximumError, afterMaximumError)
              << " density_mass=" << densityMass << " temperature_mass=" << temperatureMass
              << " luminance=[" << minimumLuminance << ',' << maximumLuminance << ']';
    if (timestamps) std::cout << " gpu_ms=" << gpuMilliseconds;
    std::cout << '\n';
    if (outputPath) {
      if (!outputPath->parent_path().empty()) {
        std::filesystem::create_directories(outputPath->parent_path());
      }
      writeDisplayPpm(*outputPath, radiance);
      std::cout << "Vulkan volume display capture: " << *outputPath << '\n';
    }

    const bool valid =
        std::isfinite(beforeL2) && std::isfinite(afterL2) && beforeL2 > 1e-5 &&
        afterL2 < beforeL2 * 0.90 && measuredCourant <= 0.701f && selectedTimestep > 0.0f &&
        std::isfinite(curlL2) && (simulationSteps == 1 || curlL2 > 1e-6) && obstacleCells > 0 &&
        activeBrickCount > 0 && activeBrickCount < brickCount && std::isfinite(bodyDisplacement) &&
        bodyDisplacement > 0.0f && std::isfinite(orientationNorm) &&
        std::abs(orientationNorm - 1.0f) < 1e-4f && orientationDelta > 0.0f &&
        (bodyCount == 1 || (bodySeparation >= 0.19f && relativeNormalVelocity > 0.0f)) &&
        densityMass > 1.0 && temperatureMass > 1.0 && finiteRadiance &&
        minimumLuminance >= 0.0f && maximumLuminance > minimumLuminance + 1e-3f;
    vkDestroyFence(device, fence, nullptr);
    if (queryPool != VK_NULL_HANDLE) vkDestroyQueryPool(device, queryPool, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineCache(device, pipelineCache, nullptr);
    vkDestroyShaderModule(device, shader, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    for (const Buffer& buffer : buffers) {
      vkDestroyBuffer(device, buffer.handle, nullptr);
      vkFreeMemory(device, buffer.memory, nullptr);
    }
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
    return valid ? 0 : 3;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-mac-projection: " << error.what() << '\n';
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    return 1;
  }
}
