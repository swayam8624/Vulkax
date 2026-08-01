#include "vulkax/physics/pipeline_cache.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace vulkax::physics {
namespace {

uint64_t hashText(uint64_t hash, const std::string& text) {
  for (const unsigned char value : text) {
    hash ^= value;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::filesystem::path pathFor(const std::filesystem::path& directory, const PipelineCacheKey& key) {
  return directory / (key.stableName() + ".vpc");
}

}  // namespace

std::string PipelineCacheKey::stableName() const {
  uint64_t hash = 1469598103934665603ull;
  hash ^= graphHash;
  hash *= 1099511628211ull;
  hash ^= shaderHash;
  hash *= 1099511628211ull;
  hash = hashText(hash, backend);
  hash = hashText(hash, deviceIdentity);
  hash = hashText(hash, compilerIdentity);
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << hash;
  return stream.str();
}

PipelineArtifactCache::PipelineArtifactCache(std::filesystem::path directory)
    : directory_(std::move(directory)) {}

std::optional<PipelineArtifact> PipelineArtifactCache::load(const PipelineCacheKey& key) const {
  std::ifstream input(pathFor(directory_, key), std::ios::binary);
  if (!input) return std::nullopt;
  std::array<char, 8> magic{};
  input.read(magic.data(), magic.size());
  if (!input || std::string_view(magic.data(), magic.size()) != "VULKAXP1") return std::nullopt;
  uint64_t byteCount = 0;
  input.read(reinterpret_cast<char*>(&byteCount), sizeof(byteCount));
  if (!input || byteCount > (1ull << 31u)) return std::nullopt;
  PipelineArtifact artifact{key, std::vector<std::byte>(byteCount)};
  input.read(
      reinterpret_cast<char*>(artifact.bytes.data()),
      static_cast<std::streamsize>(byteCount));
  if (!input) return std::nullopt;
  return artifact;
}

void PipelineArtifactCache::store(const PipelineArtifact& artifact) const {
  std::filesystem::create_directories(directory_);
  const std::filesystem::path destination = pathFor(directory_, artifact.key);
  const std::filesystem::path temporary = destination.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not open pipeline cache artifact for writing");
    output.write("VULKAXP1", 8);
    const uint64_t byteCount = artifact.bytes.size();
    output.write(reinterpret_cast<const char*>(&byteCount), sizeof(byteCount));
    output.write(
        reinterpret_cast<const char*>(artifact.bytes.data()),
        static_cast<std::streamsize>(artifact.bytes.size()));
    if (!output) throw std::runtime_error("could not write pipeline cache artifact");
  }
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(temporary, destination, error);
    if (error) throw std::runtime_error("could not publish pipeline cache artifact");
  }
}

void PipelineArtifactCache::erase(const PipelineCacheKey& key) const {
  std::error_code ignored;
  std::filesystem::remove(pathFor(directory_, key), ignored);
}

}  // namespace vulkax::physics
