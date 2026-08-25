#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace vulkax::core {

// Deterministic lowercase SHA-256 for evidence/provenance identity. This is an
// integrity primitive, not an authentication or signature API.
[[nodiscard]] std::string sha256Hex(std::string_view bytes);
[[nodiscard]] std::string sha256FileHex(const std::filesystem::path& path);
[[nodiscard]] bool isSha256Hex(std::string_view value) noexcept;

} // namespace vulkax::core
