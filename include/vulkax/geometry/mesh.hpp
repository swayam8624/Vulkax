#pragma once

#include "vulkax/core/math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vulkax::geometry {

struct Bounds3 {
    math::Vec3 minimum{};
    math::Vec3 maximum{};
};

struct TriangleMesh {
    std::vector<math::Vec3> positions;
    std::vector<std::array<std::uint32_t,3>> triangles;
};

struct TopologyReport {
    std::size_t boundaryEdges{};
    std::size_t nonManifoldEdges{};
    std::size_t degenerateTriangles{};
    bool watertight{false};
};

[[nodiscard]] TriangleMesh loadObj(const std::string& path);
[[nodiscard]] TriangleMesh parseObj(const std::string& source);
[[nodiscard]] Bounds3 bounds(const TriangleMesh& mesh);
[[nodiscard]] TopologyReport analyzeTopology(const TriangleMesh& mesh);
[[nodiscard]] TriangleMesh normalizedToUnitBox(const TriangleMesh& mesh);

} // namespace vulkax::geometry
