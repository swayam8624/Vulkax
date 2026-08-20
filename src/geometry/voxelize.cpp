#include "vulkax/geometry/voxelize.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

namespace vulkax::geometry {
namespace {

[[nodiscard]] std::optional<double> rayTriangleXDistance(math::Vec3 origin,
                                                          math::Vec3 a,
                                                          math::Vec3 b,
                                                          math::Vec3 c) {
    const math::Vec3 direction{1.0, 0.0, 0.0};
    const math::Vec3 edge1 = b - a;
    const math::Vec3 edge2 = c - a;
    const math::Vec3 h = math::cross(direction, edge2);
    const double determinant = math::dot(edge1, h);
    if (std::abs(determinant) < 1.0e-12) return std::nullopt;

    const double inverseDeterminant = 1.0 / determinant;
    const math::Vec3 s = origin - a;
    const double u = inverseDeterminant * math::dot(s, h);
    if (u < -1.0e-10 || u > 1.0 + 1.0e-10) return std::nullopt;

    const math::Vec3 q = math::cross(s, edge1);
    const double v = inverseDeterminant * math::dot(direction, q);
    if (v < -1.0e-10 || u + v > 1.0 + 1.0e-10) return std::nullopt;

    const double distance = inverseDeterminant * math::dot(edge2, q);
    if (distance <= 1.0e-10) return std::nullopt;
    return distance;
}

[[nodiscard]] std::size_t uniqueIntersectionCount(std::vector<double>& distances) {
    if (distances.empty()) return 0;
    std::sort(distances.begin(), distances.end());

    std::size_t uniqueCount = 1;
    double previous = distances.front();
    for (std::size_t index = 1; index < distances.size(); ++index) {
        const double current = distances[index];
        const double tolerance = 1.0e-9 * std::max({1.0, std::abs(previous), std::abs(current)});
        if (std::abs(current - previous) > tolerance) {
            ++uniqueCount;
            previous = current;
        }
    }
    return uniqueCount;
}

} // namespace

bool VoxelMask::at(std::size_t x, std::size_t y, std::size_t z) const {
    if (x >= nx || y >= ny || z >= nz) throw std::out_of_range("voxel index out of range");
    return solid[(z * ny + y) * nx + x] != 0;
}

std::size_t VoxelMask::solidCount() const noexcept {
    return static_cast<std::size_t>(
        std::count(solid.begin(), solid.end(), static_cast<std::uint8_t>(1)));
}

VoxelMask voxelizeClosedMesh(const TriangleMesh& mesh,
                              std::size_t nx,
                              std::size_t ny,
                              std::size_t nz,
                              Bounds3 domainBounds) {
    if (nx == 0 || ny == 0 || nz == 0)
        throw std::invalid_argument("voxel dimensions must be positive");

    const auto topology = analyzeTopology(mesh);
    if (!topology.watertight)
        throw std::invalid_argument("physics voxelization requires a watertight manifold mesh");

    const math::Vec3 extent = domainBounds.maximum - domainBounds.minimum;
    if (extent.x <= 0.0 || extent.y <= 0.0 || extent.z <= 0.0)
        throw std::invalid_argument("invalid voxel domain bounds");

    VoxelMask result{nx, ny, nz, domainBounds, std::vector<std::uint8_t>(nx * ny * nz, 0)};
    std::vector<double> intersections;
    intersections.reserve(mesh.triangles.size());

    for (std::size_t z = 0; z < nz; ++z) {
        for (std::size_t y = 0; y < ny; ++y) {
            for (std::size_t x = 0; x < nx; ++x) {
                const math::Vec3 point{
                    domainBounds.minimum.x + (static_cast<double>(x) + 0.5) * extent.x / static_cast<double>(nx),
                    domainBounds.minimum.y + (static_cast<double>(y) + 0.5) * extent.y / static_cast<double>(ny),
                    domainBounds.minimum.z + (static_cast<double>(z) + 0.5) * extent.z / static_cast<double>(nz)};

                intersections.clear();
                for (const auto& triangle : mesh.triangles) {
                    const auto distance = rayTriangleXDistance(
                        point,
                        mesh.positions[triangle[0]],
                        mesh.positions[triangle[1]],
                        mesh.positions[triangle[2]]);
                    if (distance) intersections.push_back(*distance);
                }

                if (uniqueIntersectionCount(intersections) % 2 == 1)
                    result.solid[(z * ny + y) * nx + x] = 1;
            }
        }
    }
    return result;
}

} // namespace vulkax::geometry
