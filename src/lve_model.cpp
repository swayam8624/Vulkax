#include "lve_model.hpp"

#include "runtime_paths.hpp"
#include "lve_utils.hpp"

// libs
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

// std
#include <cassert>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace std {
template <>
struct hash<lve::LveModel::Vertex> {
  size_t operator()(lve::LveModel::Vertex const &vertex) const {
    size_t seed = 0;
    lve::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.uv);
    return seed;
  }
};
}  // namespace std

namespace lve {

LveModel::LveModel(LveDevice &device, const LveModel::Builder &builder) : lveDevice{device} {
  createVertexBuffers(builder.vertices);
  createIndexBuffers(builder.indices);
}

LveModel::~LveModel() {}

std::unique_ptr<LveModel> LveModel::createModelFromFile(
    LveDevice &device, const std::string &filepath) {
  Builder builder{};
  builder.loadModel(resolveRuntimeResource(filepath).string());
  return std::make_unique<LveModel>(device, builder);
}

void LveModel::createVertexBuffers(const std::vector<Vertex> &vertices) {
  vertexCount = static_cast<uint32_t>(vertices.size());
  assert(vertexCount >= 3 && "Vertex count must be at least 3");
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
  uint32_t vertexSize = sizeof(vertices[0]);

  LveBuffer stagingBuffer{
      lveDevice,
      vertexSize,
      vertexCount,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  };

  stagingBuffer.map();
  stagingBuffer.writeToBuffer((void *)vertices.data());

  vertexBuffer = std::make_unique<LveBuffer>(
      lveDevice,
      vertexSize,
      vertexCount,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  lveDevice.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize);
}

void LveModel::createIndexBuffers(const std::vector<uint32_t> &indices) {
  indexCount = static_cast<uint32_t>(indices.size());
  hasIndexBuffer = indexCount > 0;

  if (!hasIndexBuffer) {
    return;
  }

  VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
  uint32_t indexSize = sizeof(indices[0]);

  LveBuffer stagingBuffer{
      lveDevice,
      indexSize,
      indexCount,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  };

  stagingBuffer.map();
  stagingBuffer.writeToBuffer((void *)indices.data());

  indexBuffer = std::make_unique<LveBuffer>(
      lveDevice,
      indexSize,
      indexCount,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  lveDevice.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
}

void LveModel::draw(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance) {
  if (hasIndexBuffer) {
    vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, 0, 0, firstInstance);
  } else {
    vkCmdDraw(commandBuffer, vertexCount, instanceCount, 0, firstInstance);
  }
}

void LveModel::bind(VkCommandBuffer commandBuffer) {
  VkBuffer buffers[] = {vertexBuffer->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

  if (hasIndexBuffer) {
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
  }
}

std::vector<VkVertexInputBindingDescription> LveModel::Vertex::getBindingDescriptions() {
  std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
  bindingDescriptions[0].binding = 0;
  bindingDescriptions[0].stride = sizeof(Vertex);
  bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  return bindingDescriptions;
}

std::vector<VkVertexInputAttributeDescription> LveModel::Vertex::getAttributeDescriptions() {
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

  attributeDescriptions.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)});
  attributeDescriptions.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)});
  attributeDescriptions.push_back({2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)});
  attributeDescriptions.push_back({3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});

  return attributeDescriptions;
}

void LveModel::Builder::loadModel(const std::string &filepath) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str(), nullptr, true)) {
    throw std::runtime_error(warn + err);
  }

  vertices.clear();
  indices.clear();

  std::unordered_map<Vertex, uint32_t> uniqueVertices{};
  for (const auto &shape : shapes) {
    for (size_t faceIndex = 0, indexOffset = 0; faceIndex < shape.mesh.num_face_vertices.size();
         indexOffset += shape.mesh.num_face_vertices[faceIndex], ++faceIndex) {
      int faceVertexCount = shape.mesh.num_face_vertices[faceIndex];
      if (faceVertexCount != 3) {
        continue;
      }
      uint32_t faceIndices[3]{};
      bool validFace = true;
      Vertex faceVertices[3]{};

      for (int vertexInFace = 0; vertexInFace < faceVertexCount; ++vertexInFace) {
        const auto &index = shape.mesh.indices[indexOffset + vertexInFace];
        Vertex vertex{};

        if (index.vertex_index < 0 ||
            3 * static_cast<size_t>(index.vertex_index) + 2 >= attrib.vertices.size()) {
          validFace = false;
          break;
        }

        vertex.position = {
            attrib.vertices[3 * index.vertex_index + 0],
            attrib.vertices[3 * index.vertex_index + 1],
            attrib.vertices[3 * index.vertex_index + 2],
        };

        if (3 * static_cast<size_t>(index.vertex_index) + 2 < attrib.colors.size()) {
          vertex.color = {
              attrib.colors[3 * index.vertex_index + 0],
              attrib.colors[3 * index.vertex_index + 1],
              attrib.colors[3 * index.vertex_index + 2],
          };
        } else {
          vertex.color = {1.f, 1.f, 1.f};
        }

        if (index.normal_index >= 0 &&
            3 * static_cast<size_t>(index.normal_index) + 2 < attrib.normals.size()) {
          vertex.normal = {
              attrib.normals[3 * index.normal_index + 0],
              attrib.normals[3 * index.normal_index + 1],
              attrib.normals[3 * index.normal_index + 2],
          };
        }

        if (index.texcoord_index >= 0 &&
            2 * static_cast<size_t>(index.texcoord_index) + 1 < attrib.texcoords.size()) {
          vertex.uv = {
              attrib.texcoords[2 * index.texcoord_index + 0],
              attrib.texcoords[2 * index.texcoord_index + 1],
          };
        }

        faceVertices[vertexInFace] = vertex;
      }

      if (!validFace) {
        continue;
      }

      glm::vec3 edge0 = faceVertices[1].position - faceVertices[0].position;
      glm::vec3 edge1 = faceVertices[2].position - faceVertices[0].position;
      if (glm::dot(glm::cross(edge0, edge1), glm::cross(edge0, edge1)) <= 1e-12f) {
        continue;
      }

      glm::vec3 generatedNormal = glm::normalize(glm::cross(edge0, edge1));
      for (int vertexInFace = 0; vertexInFace < faceVertexCount; ++vertexInFace) {
        Vertex vertex = faceVertices[vertexInFace];
        if (glm::dot(vertex.normal, vertex.normal) <= 1e-12f) {
          vertex.normal = generatedNormal;
        }

        if (uniqueVertices.count(vertex) == 0) {
          uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
          vertices.push_back(vertex);
        }
        faceIndices[vertexInFace] = uniqueVertices[vertex];
      }

      for (uint32_t faceIndexValue : faceIndices) {
        indices.push_back(faceIndexValue);
      }
    }
  }

  if (vertices.empty() || indices.empty()) {
    throw std::runtime_error("model contains no valid triangles: " + filepath);
  }
}

}  // namespace lve
