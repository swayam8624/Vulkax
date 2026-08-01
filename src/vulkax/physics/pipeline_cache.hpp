#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vulkax::physics {

struct PipelineCacheKey {
  uint64_t graphHash = 0;
  uint64_t shaderHash = 0;
  std::string backend;
  std::string deviceIdentity;
  std::string compilerIdentity;

  [[nodiscard]] std::string stableName() const;
};

struct PipelineArtifact {
  PipelineCacheKey key;
  std::vector<std::byte> bytes;
};

class PipelineArtifactCache {
 public:
  explicit PipelineArtifactCache(std::filesystem::path directory);

  [[nodiscard]] std::optional<PipelineArtifact> load(const PipelineCacheKey& key) const;
  void store(const PipelineArtifact& artifact) const;
  void erase(const PipelineCacheKey& key) const;

 private:
  std::filesystem::path directory_;
};

}  // namespace vulkax::physics
