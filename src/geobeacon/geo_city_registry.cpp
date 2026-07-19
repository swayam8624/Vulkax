#include "geobeacon/geo_city_registry.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace lve::geo {
namespace {

uint64_t fileBytes(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) return 0;
  if (std::filesystem::is_regular_file(path, error)) {
    return std::filesystem::file_size(path, error);
  }
  uint64_t bytes = 0;
  for (std::filesystem::recursive_directory_iterator iterator{
           path,
           std::filesystem::directory_options::skip_permission_denied,
           error};
       !error && iterator != std::filesystem::recursive_directory_iterator{};
       iterator.increment(error)) {
    if (iterator->is_regular_file(error)) {
      bytes += iterator->file_size(error);
    }
  }
  return bytes;
}

}  // namespace

std::vector<GeoCityDefinition> loadGeoCityRegistry(
    const std::filesystem::path& registryPath) {
  std::ifstream input{registryPath};
  if (!input) {
    throw std::runtime_error(
        "failed to open city registry: " + registryPath.string());
  }
  nlohmann::json root;
  input >> root;
  if (root.value("format", "") != "Vulkax-city-registry-1") {
    throw std::runtime_error("unsupported Vulkax city registry");
  }

  const auto base = registryPath.parent_path();
  std::vector<GeoCityDefinition> cities;
  for (const auto& item : root.at("cities")) {
    GeoCityDefinition city{};
    city.id = item.at("id").get<std::string>();
    city.displayName = item.at("displayName").get<std::string>();
    const auto& center = item.at("center");
    city.centerWgs84 = {
        center.at(0).get<double>(),
        center.at(1).get<double>(),
        center.size() > 2 ? center.at(2).get<double>() : 0.0,
    };
    city.manifestPath = base / item.at("manifest").get<std::string>();
    city.navigationPath = base / item.at("navigation").get<std::string>();
    city.installed =
        std::filesystem::is_regular_file(city.manifestPath) &&
        std::filesystem::is_regular_file(city.navigationPath);
    if (city.installed) {
      city.installedBytes =
          fileBytes(city.manifestPath.parent_path()) +
          fileBytes(city.navigationPath);
    }
    cities.push_back(std::move(city));
  }
  if (cities.empty()) {
    throw std::runtime_error("city registry contains no cities");
  }
  return cities;
}

}  // namespace lve::geo
