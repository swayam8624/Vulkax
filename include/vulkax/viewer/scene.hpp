#pragma once

#include "vulkax/core/math.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vulkax::viewer {

struct ViewerGaussian {
    math::Vec3 position{};
    std::array<float, 3> scale{0.02F, 0.02F, 0.02F};
    // Quaternion in Vulkax/3DGS storage order: w, x, y, z.
    std::array<float, 4> rotation{1.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 3> color{0.62F, 0.72F, 0.92F};
    float opacity{1.0F};
    gaussian::GaussianId id{};
};

struct ViewerParticle {
    std::uint32_t id{};
    math::Vec3 position{};
    bool inRewriteRegion{};
};

struct ViewerSurfaceVertex {
    math::Vec3 position{};
    math::Vec3 normal{};
    std::array<float, 3> color{0.28F, 0.53F, 0.95F};
};

struct ViewerBounds {
    math::Vec3 minimum{};
    math::Vec3 maximum{};
    math::Vec3 center{};
    double radius{1.0};
};

struct ViewerScene {
    std::vector<ViewerGaussian> before;
    std::vector<ViewerGaussian> after;
    std::vector<ViewerParticle> particles;
    // Non-indexed triangle list. Every consecutive group of three vertices is a triangle.
    std::vector<ViewerSurfaceVertex> surfaceTriangles;
    std::string surfaceKind{"none"};
    ViewerBounds bounds{};
    double maxGaussianDisplacement{};
    std::size_t rewriteParticleCount{};

    [[nodiscard]] bool hasSurface() const noexcept { return !surfaceTriangles.empty(); }
};

// Load the presentation/debug scene associated with a completed captured-world run.
// This function is read-only: it never edits the run directory or its evidence.
[[nodiscard]] ViewerScene loadCapturedWorldScene(
    const std::filesystem::path& runDirectory,
    const std::filesystem::path& particlesCsv = {});

// Load one Gaussian PLY as a standalone viewer scene. Before and after both point to
// the same immutable source representation; this is intended for authored/imported
// Gaussian assets rather than scientific rewrite comparison.
[[nodiscard]] ViewerScene loadStandaloneGaussianScene(const std::filesystem::path& plyPath);

// Convert the authoritative Gaussian representation into the compact viewer form.
[[nodiscard]] std::vector<ViewerGaussian> makeViewerGaussians(const gaussian::GaussianCloud& cloud);

} // namespace vulkax::viewer
