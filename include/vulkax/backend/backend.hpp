#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vulkax::backend {

enum class BackendKind : std::uint8_t { Vulkan, Metal, OpenGL };
enum class PlatformKind : std::uint8_t { MacOS, Windows, Linux, Other };

enum class Feature : std::uint8_t {
    Compute,
    StorageBuffers,
    StorageImages,
    Atomics,
    Subgroups,
    Float16,
    Float64,
    TimestampQueries,
    DescriptorIndexing,
    Headless,
    RayQuery,
};

struct BackendCapabilities {
    BackendKind kind{BackendKind::Vulkan};
    bool available{false};
    bool nativePlatformBackend{false};
    bool dedicatedGpu{false};
    double driverQuality{0.5}; // normalized [0, 1], supplied by the probe layer
    std::uint64_t deviceMemoryBytes{};
    std::string deviceName;
    std::vector<Feature> features;
};

struct WorkloadRequirements {
    bool requireCompute{true};
    std::vector<Feature> requiredFeatures;
};

struct BackendSelection {
    std::optional<BackendKind> kind;
    double score{};
    std::vector<std::string> reasons;
};

[[nodiscard]] PlatformKind currentPlatform() noexcept;
[[nodiscard]] std::string_view toString(BackendKind kind) noexcept;
[[nodiscard]] bool supports(const BackendCapabilities& capability, Feature feature) noexcept;
[[nodiscard]] BackendSelection selectBackend(const std::vector<BackendCapabilities>& candidates,
                                             const WorkloadRequirements& requirements,
                                             PlatformKind platform);

} // namespace vulkax::backend
