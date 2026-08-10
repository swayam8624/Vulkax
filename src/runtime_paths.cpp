#include "runtime_paths.hpp"

#include <cstdlib>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace lve {
namespace {

std::filesystem::path canonicalIfPossible(const std::filesystem::path& path) {
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(path, error);
  return error ? path : canonical;
}

std::filesystem::path executablePath() {
#if defined(_WIN32)
  std::vector<wchar_t> buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length > 0 && length < buffer.size()) {
    return canonicalIfPossible(std::filesystem::path{buffer.data(), buffer.data() + length});
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size + 1, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
    return canonicalIfPossible(std::filesystem::path{buffer.data()});
  }
#elif defined(__linux__)
  std::vector<char> buffer(4096, '\0');
  const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length > 0) {
    buffer[static_cast<size_t>(length)] = '\0';
    return canonicalIfPossible(std::filesystem::path{buffer.data()});
  }
#endif
  return {};
}

std::filesystem::path detectResourceRoot() {
  if (const char* overrideRoot = std::getenv("VULKAX_RESOURCE_ROOT");
      overrideRoot != nullptr && overrideRoot[0] != '\0') {
    return canonicalIfPossible(std::filesystem::path{overrideRoot});
  }

  const auto executable = executablePath();
  if (!executable.empty()) {
    const auto executableDirectory = executable.parent_path();
    const auto contentsDirectory = executableDirectory.parent_path();
    if (executableDirectory.filename() == "MacOS" &&
        contentsDirectory.filename() == "Contents") {
      const auto resources = contentsDirectory / "Resources";
      if (std::filesystem::is_directory(resources)) {
        return canonicalIfPossible(resources);
      }
    }

    // Build-tree and portable bundle layouts.
    const std::array candidates{
        executableDirectory / "resources",
        executableDirectory.parent_path() / "resources",
        executableDirectory / "share" / "vulkax",
        executableDirectory.parent_path() / "share" / "vulkax"};
    for (const auto& candidate : candidates) {
      if (std::filesystem::is_directory(candidate)) {
        return canonicalIfPossible(candidate);
      }
    }

    // Return the expected portable layout even before it exists so missing
    // resources produce a useful executable-relative path instead of a hidden
    // compile-time source checkout dependency.
    return executableDirectory / "resources";
  }

  return std::filesystem::current_path() / "resources";
}

}  // namespace

std::filesystem::path runtimeResourceRoot() {
  static const auto root = detectResourceRoot();
  return root;
}

std::filesystem::path resolveRuntimeResource(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return path;

  const auto bundled = runtimeResourceRoot() / path;
  if (std::filesystem::exists(bundled)) return bundled;

  // A working-directory fallback is useful to developers and tests while
  // remaining relocatable: it contains no build-machine source path.
  const auto workingDirectory = std::filesystem::current_path() / path;
  if (std::filesystem::exists(workingDirectory)) return workingDirectory;
  return bundled;
}

}  // namespace lve
