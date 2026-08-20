#include "vulkax/gaussian/hierarchy.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace vulkax::gaussian {
namespace {

[[nodiscard]] double component(math::Vec3 value, std::size_t axis) noexcept {
    if (axis == 0) return value.x;
    if (axis == 1) return value.y;
    return value.z;
}

[[nodiscard]] GaussianHierarchyBounds boundsForRange(
    const GaussianCloud& cloud,
    const std::vector<std::size_t>& permutation,
    std::size_t begin,
    std::size_t end) {
    const auto first = cloud.splats[permutation[begin]].position;
    GaussianHierarchyBounds bounds{first, first};
    for (std::size_t offset = begin + 1; offset < end; ++offset) {
        const auto position = cloud.splats[permutation[offset]].position;
        bounds.minimum.x = std::min(bounds.minimum.x, position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, position.z);
    }
    return bounds;
}

std::size_t buildNode(GaussianHierarchy& hierarchy,
                      const GaussianCloud& cloud,
                      std::size_t begin,
                      std::size_t end) {
    const std::size_t nodeIndex = hierarchy.nodes.size();
    const auto bounds = boundsForRange(cloud, hierarchy.permutation, begin, end);
    hierarchy.nodes.push_back({bounds, begin, end});

    if (end - begin <= hierarchy.leafSize) return nodeIndex;

    const auto extent = bounds.maximum - bounds.minimum;
    std::size_t axis = 0;
    if (extent.y > extent.x && extent.y >= extent.z) axis = 1;
    else if (extent.z > extent.x && extent.z > extent.y) axis = 2;

    const std::size_t middle = begin + (end - begin) / 2;
    auto first = hierarchy.permutation.begin() + static_cast<std::ptrdiff_t>(begin);
    auto mid = hierarchy.permutation.begin() + static_cast<std::ptrdiff_t>(middle);
    auto last = hierarchy.permutation.begin() + static_cast<std::ptrdiff_t>(end);
    std::nth_element(first, mid, last, [&](std::size_t lhs, std::size_t rhs) {
        const double a = component(cloud.splats[lhs].position, axis);
        const double b = component(cloud.splats[rhs].position, axis);
        return a != b ? a < b : lhs < rhs;
    });

    const std::size_t left = buildNode(hierarchy, cloud, begin, middle);
    const std::size_t right = buildNode(hierarchy, cloud, middle, end);
    hierarchy.nodes[nodeIndex].left = left;
    hierarchy.nodes[nodeIndex].right = right;
    return nodeIndex;
}

[[nodiscard]] bool overlaps(const GaussianHierarchyBounds& bounds,
                            math::Vec3 minimum,
                            math::Vec3 maximum) noexcept {
    return bounds.maximum.x >= minimum.x && bounds.minimum.x <= maximum.x &&
           bounds.maximum.y >= minimum.y && bounds.minimum.y <= maximum.y &&
           bounds.maximum.z >= minimum.z && bounds.minimum.z <= maximum.z;
}

[[nodiscard]] bool contains(math::Vec3 position,
                            math::Vec3 minimum,
                            math::Vec3 maximum) noexcept {
    return position.x >= minimum.x && position.x <= maximum.x &&
           position.y >= minimum.y && position.y <= maximum.y &&
           position.z >= minimum.z && position.z <= maximum.z;
}

void summarizeNode(const GaussianHierarchy& hierarchy,
                   std::size_t nodeIndex,
                   std::size_t depth,
                   GaussianHierarchyStats& stats) {
    const auto& node = hierarchy.nodes[nodeIndex];
    stats.maximumDepth = std::max(stats.maximumDepth, depth);
    if (node.leaf()) {
        ++stats.leafCount;
        stats.maximumLeafOccupancy = std::max(stats.maximumLeafOccupancy, node.count());
        return;
    }
    summarizeNode(hierarchy, node.left, depth + 1, stats);
    summarizeNode(hierarchy, node.right, depth + 1, stats);
}

} // namespace

GaussianHierarchy buildGaussianHierarchy(const GaussianCloud& cloud, std::size_t leafSize) {
    if (leafSize == 0) throw std::invalid_argument("Gaussian hierarchy leaf size must be positive");

    GaussianHierarchy hierarchy;
    hierarchy.leafSize = leafSize;
    hierarchy.permutation.resize(cloud.size());
    std::iota(hierarchy.permutation.begin(), hierarchy.permutation.end(), std::size_t{0});
    if (cloud.empty()) return hierarchy;

    hierarchy.nodes.reserve(cloud.size() * 2 / leafSize + 1);
    buildNode(hierarchy, cloud, 0, cloud.size());
    return hierarchy;
}

GaussianHierarchyStats summarizeGaussianHierarchy(const GaussianHierarchy& hierarchy) {
    GaussianHierarchyStats stats;
    stats.nodeCount = hierarchy.nodes.size();
    if (!hierarchy.empty()) summarizeNode(hierarchy, 0, 0, stats);
    return stats;
}

std::vector<std::size_t> queryGaussianHierarchyAabb(
    const GaussianHierarchy& hierarchy,
    const GaussianCloud& cloud,
    math::Vec3 minimum,
    math::Vec3 maximum) {
    if (minimum.x > maximum.x || minimum.y > maximum.y || minimum.z > maximum.z)
        throw std::invalid_argument("Gaussian hierarchy query has inverted bounds");
    if (hierarchy.permutation.size() != cloud.size())
        throw std::invalid_argument("Gaussian hierarchy does not match cloud size");

    std::vector<std::size_t> result;
    if (hierarchy.empty()) return result;

    std::vector<std::size_t> stack{0};
    while (!stack.empty()) {
        const std::size_t nodeIndex = stack.back();
        stack.pop_back();
        const auto& node = hierarchy.nodes[nodeIndex];
        if (!overlaps(node.bounds, minimum, maximum)) continue;

        if (!node.leaf()) {
            stack.push_back(node.right);
            stack.push_back(node.left);
            continue;
        }

        for (std::size_t offset = node.begin; offset < node.end; ++offset) {
            const std::size_t splatIndex = hierarchy.permutation[offset];
            if (contains(cloud.splats[splatIndex].position, minimum, maximum))
                result.push_back(splatIndex);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace vulkax::gaussian
