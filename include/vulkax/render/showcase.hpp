#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace vulkax::render {

struct GaussianShowcaseSettings {
    bool enabled{false};
    std::string scenePreset{"studio_pedestal"};
    std::filesystem::path assetRoot;
    std::filesystem::path assetLock{"assets/demo/showcase_assets.lock.json"};
    std::uint32_t width{1280U};
    std::uint32_t height{720U};
    std::uint32_t turntableFrames{12U};
    bool closeup{true};
    bool summaryCard{true};
};

struct GaussianShowcaseResult {
    bool produced{};
    std::string scenePreset;
    std::size_t turntableFrames{};
    std::string assetLockSha256;
    std::string environmentAssetSha256;
};

// Presentation-only renderer layered on top of the native Gaussian renderer.
// It adds deterministic decorative Gaussian floor/pedestal props, hero/detail
// cameras and a turntable. The pinned HDRI is recorded as an environment
// reference only: Vulkax 0.80 does not claim physical relighting of stored SH
// Gaussian appearance.
[[nodiscard]] GaussianShowcaseResult renderGaussianShowcase(
    backend::BackendKind backendKind,
    const gaussian::GaussianCloud& before,
    const gaussian::GaussianCloud& finalState,
    const std::string& rewriteStatus,
    const std::filesystem::path& outputDirectory,
    const GaussianShowcaseSettings& settings);

} // namespace vulkax::render
