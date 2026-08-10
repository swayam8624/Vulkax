#pragma once

#include "vulkax/geometry/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vulkax::geometry {

struct VoxelMask {
    std::size_t nx{},ny{},nz{};
    Bounds3 bounds{};
    std::vector<std::uint8_t> solid;
    [[nodiscard]] bool at(std::size_t x,std::size_t y,std::size_t z)const;
    [[nodiscard]] std::size_t solidCount()const noexcept;
};

// Closed-mesh reference voxelizer. It performs +X parity ray tests at voxel centers; production GPU
// voxelization can replace this implementation without changing the physics-domain contract.
[[nodiscard]] VoxelMask voxelizeClosedMesh(const TriangleMesh& mesh,std::size_t nx,std::size_t ny,std::size_t nz,Bounds3 domainBounds);

} // namespace vulkax::geometry
