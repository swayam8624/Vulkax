#pragma once

#include "vulkax/core/math.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"

#include <cstddef>
#include <limits>
#include <vector>

namespace vulkax::gaussian {

struct GaussianHierarchyBounds {
    math::Vec3 minimum{};
    math::Vec3 maximum{};
};

struct GaussianHierarchyNode {
    GaussianHierarchyBounds bounds{};
    std::size_t begin{};
    std::size_t end{};
    std::size_t left{std::numeric_limits<std::size_t>::max()};
    std::size_t right{std::numeric_limits<std::size_t>::max()};

    [[nodiscard]] bool leaf() const noexcept {
        return left == std::numeric_limits<std::size_t>::max();
    }
    [[nodiscard]] std::size_t count() const noexcept { return end - begin; }
};

struct GaussianHierarchy {
    std::vector<std::size_t> permutation;
    std::vector<GaussianHierarchyNode> nodes;
    std::size_t leafSize{};

    [[nodiscard]] bool empty() const noexcept { return nodes.empty(); }
};

struct GaussianHierarchyStats {
    std::size_t nodeCount{};
    std::size_t leafCount{};
    std::size_t maximumDepth{};
    std::size_t maximumLeafOccupancy{};
};

[[nodiscard]] GaussianHierarchy buildGaussianHierarchy(
    const GaussianCloud& cloud, std::size_t leafSize = 64);

[[nodiscard]] GaussianHierarchyStats summarizeGaussianHierarchy(
    const GaussianHierarchy& hierarchy);

// Transient-storage form retained for rendering/spatial algorithms that operate
// immediately on the supplied cloud. Do not persist these indices as identity.
[[nodiscard]] std::vector<std::size_t> queryGaussianHierarchyAabb(
    const GaussianHierarchy& hierarchy,
    const GaussianCloud& cloud,
    math::Vec3 minimum,
    math::Vec3 maximum);

// Stable-identity form for correspondence, selection and evidence. Returned IDs
// are sorted by packed stable ID so the result is independent of storage order.
[[nodiscard]] std::vector<GaussianId> queryGaussianHierarchyAabbIds(
    const GaussianHierarchy& hierarchy,
    const GaussianCloud& cloud,
    math::Vec3 minimum,
    math::Vec3 maximum);

} // namespace vulkax::gaussian
