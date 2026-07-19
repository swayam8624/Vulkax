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
  std::filesystem::path manifestPath;
  std::filesystem::path navigationPath;
  bool installed = false;
  uint64_t installedBytes = 0;
};

std::vector<GeoCityDefinition> loadGeoCityRegistry(
    const std::filesystem::path& registryPath);

}  // namespace lve::geo
