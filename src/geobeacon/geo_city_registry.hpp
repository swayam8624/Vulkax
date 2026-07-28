#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace lve::geo {

struct GeoCityDefinition {
  std::string id;
  std::string displayName;
  glm::dvec3 centerWgs84{};
  glm::vec3 cameraPosition{0.f, -130.f, -720.f};
  glm::vec3 cameraRotation{-0.18f, 0.f, 0.f};
  std::filesystem::path manifestPath;
  std::filesystem::path navigationPath;
  bool installed = false;
  uint64_t installedBytes = 0;
};

std::vector<GeoCityDefinition> loadGeoCityRegistry(
    const std::filesystem::path& registryPath);

}  // namespace lve::geo
