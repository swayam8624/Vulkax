#pragma once

#include "vulkax/viewer/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace vulkax::viewer {

struct ViewerCamera {
    math::Vec3 position{};
    math::Vec3 target{};
    math::Vec3 worldUp{0.0, 1.0, 0.0};
    double verticalFovRadians{0.7853981633974483}; // 45 degrees
    double aspect{16.0 / 9.0};
    double nearPlane{1.0e-3};
    double farPlane{1.0e6};
};

struct VisibilitySettings {
    std::size_t maxSplats{250000};
    double sigmaExtent{3.0};
    double minimumOpacity{0.003};
    // Prevent a single pathological source scale close to the camera from taking
    // effectively infinite importance and evicting the rest of the scene.
    double maximumProjectedRadiusNdc{4.0};
    // CPU/reference rendering needs a conventional back-to-front order. A
    // validated GPU sorter can disable this final O(N log N) stage while still
    // reusing the same conservative culling and importance-budget selection.
    bool sortBackToFront{true};
};

struct VisibilityResult {
    // Indices into the original ViewerGaussian array. With sortBackToFront=true
    // they are ordered for conventional alpha blending. With it disabled they
    // are the retained set only and must be ordered by the downstream renderer.
    std::vector<std::uint32_t> order;
    std::size_t sourceCount{};
    std::size_t opacityRejected{};
    std::size_t frustumRejected{};
    std::size_t budgetRejected{};
};

namespace detail {

struct VisibilityCandidate {
    std::uint32_t index{};
    double depth{};
    double importance{};
};

[[nodiscard]] inline double largestScale(const ViewerGaussian& gaussian) noexcept {
    return std::max({static_cast<double>(gaussian.scale[0]),
                     static_cast<double>(gaussian.scale[1]),
                     static_cast<double>(gaussian.scale[2])});
}

} // namespace detail

// Conservative sphere-frustum culling plus an importance budget. The Gaussian's
// three-sigma largest-axis sphere is used for visibility, so anisotropic ellipses
// crossing a frustum edge are retained rather than clipped away too aggressively.
//
// When the visible set exceeds maxSplats, importance is roughly opacity times
// projected footprint. By default the retained set is then sorted by depth. A
// renderer with a validated GPU sort can set sortBackToFront=false to receive the
// same retained set without paying for the final CPU depth sort.
[[nodiscard]] inline VisibilityResult selectVisibleGaussians(
    std::span<const ViewerGaussian> gaussians,
    const ViewerCamera& camera,
    const VisibilitySettings& settings = {}) {
    VisibilityResult result;
    result.sourceCount = gaussians.size();
    if (gaussians.empty()) return result;

    const math::Vec3 forward = math::normalized(camera.target - camera.position);
    if (math::length(forward) <= 1.0e-12) return result;
    math::Vec3 right = math::normalized(math::cross(forward, camera.worldUp));
    if (math::length(right) <= 1.0e-12) right = {1.0, 0.0, 0.0};
    const math::Vec3 up = math::normalized(math::cross(right, forward));

    const double halfVertical = std::clamp(camera.verticalFovRadians * 0.5, 1.0e-4, 1.55);
    const double tanY = std::tan(halfVertical);
    const double aspect = std::max(camera.aspect, 1.0e-6);
    const double tanX = tanY * aspect;
    const double nearPlane = std::max(camera.nearPlane, 1.0e-9);
    const double farPlane = std::max(camera.farPlane, nearPlane + 1.0e-6);
    const double sigma = std::max(settings.sigmaExtent, 0.0);

    std::vector<detail::VisibilityCandidate> candidates;
    candidates.reserve(gaussians.size());

    for (std::size_t rawIndex = 0; rawIndex < gaussians.size(); ++rawIndex) {
        const auto& gaussian = gaussians[rawIndex];
        const double opacity = static_cast<double>(gaussian.opacity);
        if (!std::isfinite(opacity) || opacity < settings.minimumOpacity) {
            ++result.opacityRejected;
            continue;
        }

        const math::Vec3 delta = gaussian.position - camera.position;
        const double depth = math::dot(delta, forward);
        const double radius = std::max(0.0, detail::largestScale(gaussian) * sigma);
        if (!std::isfinite(depth) || !std::isfinite(radius) || depth + radius < nearPlane || depth - radius > farPlane) {
            ++result.frustumRejected;
            continue;
        }

        const double projectionDepth = std::max(depth, nearPlane);
        const double horizontal = math::dot(delta, right);
        const double vertical = math::dot(delta, up);
        const double horizontalLimit = projectionDepth * tanX + radius;
        const double verticalLimit = projectionDepth * tanY + radius;
        if (std::abs(horizontal) > horizontalLimit || std::abs(vertical) > verticalLimit) {
            ++result.frustumRejected;
            continue;
        }

        const double projectedRadius = std::min(
            settings.maximumProjectedRadiusNdc,
            radius / std::max(projectionDepth * tanY, 1.0e-12));
        const double footprint = std::max(projectedRadius * projectedRadius, 1.0e-12);
        const double importance = opacity * footprint;
        candidates.push_back({static_cast<std::uint32_t>(rawIndex), depth, importance});
    }

    const std::size_t budget = std::min(settings.maxSplats, candidates.size());
    if (budget < candidates.size()) {
        const auto middle = candidates.begin() + static_cast<std::ptrdiff_t>(budget);
        std::nth_element(candidates.begin(), middle, candidates.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.importance != rhs.importance) return lhs.importance > rhs.importance;
            return lhs.index < rhs.index;
        });
        result.budgetRejected = candidates.size() - budget;
        candidates.resize(budget);
    }

    if (settings.sortBackToFront) {
        std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.depth != rhs.depth) return lhs.depth > rhs.depth;
            return lhs.index < rhs.index;
        });
    }
    result.order.reserve(candidates.size());
    for (const auto& candidate : candidates) result.order.push_back(candidate.index);
    return result;
}

} // namespace vulkax::viewer
