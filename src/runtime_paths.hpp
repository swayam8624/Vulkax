#pragma once

#include <filesystem>

namespace lve {

std::filesystem::path runtimeResourceRoot();
std::filesystem::path resolveRuntimeResource(
    const std::filesystem::path& path);

}  // namespace lve
