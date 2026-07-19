#include "runtime_paths.hpp"

#include <cstdlib>
#include <system_error>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#ifndef ENGINE_DIR
#define ENGINE_DIR "../"
#endif

namespace lve {
namespace {

std::filesystem::path executablePath() {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size + 1, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
    std::error_code error;
    auto path = std::filesystem::weakly_canonical(buffer.data(), error);
    return error ? std::filesystem::path{buffer.data()} : path;
  }
#elif defined(__linux__)
  std::vector<char> buffer(4096, '\0');
  const auto length =
      readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length > 0) {
    buffer[static_cast<size_t>(length)] = '\0';
    return std::filesystem::path{buffer.data()};
  }
#endif
  return {};
}

std::filesystem::path detectResourceRoot() {
  if (const char* overrideRoot = std::getenv("VULKAX_RESOURCE_ROOT");
      overrideRoot != nullptr && overrideRoot[0] != '\0') {
    return std::filesystem::path{overrideRoot};
  }

  const auto executable = executablePath();
  const auto macOsDirectory = executable.parent_path();
  const auto contentsDirectory = macOsDirectory.parent_path();
  if (macOsDirectory.filename() == "MacOS" &&
      contentsDirectory.filename() == "Contents") {
    const auto resources = contentsDirectory / "Resources";
    if (std::filesystem::is_directory(resources)) {
      return resources;
    }
  }

  return std::filesystem::path{ENGINE_DIR};
}

}  // namespace

std::filesystem::path runtimeResourceRoot() {
  static const auto root = detectResourceRoot();
  return root;
}

std::filesystem::path resolveRuntimeResource(
    const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return path;

  const auto bundled = runtimeResourceRoot() / path;
  if (std::filesystem::exists(bundled)) return bundled;

  const auto source = std::filesystem::path{ENGINE_DIR} / path;
  if (std::filesystem::exists(source)) return source;
  return bundled;
}

}  // namespace lve
