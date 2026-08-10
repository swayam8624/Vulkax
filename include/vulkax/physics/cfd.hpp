#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vulkax::physics::cfd {

struct MacGrid2D {
    std::uint32_t nx{1}, ny{1};
    double dx{1.0}, dy{1.0};
    std::vector<double> u;        // (nx+1) * ny
    std::vector<double> v;        // nx * (ny+1)
    std::vector<double> pressure; // nx * ny
    std::vector<double> scalar;   // nx * ny
    std::vector<std::uint8_t> solid;

    MacGrid2D(std::uint32_t width, std::uint32_t height, double cellDx = 1.0, double cellDy = 1.0);
    [[nodiscard]] std::size_t cell(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] std::size_t uFace(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] std::size_t vFace(std::uint32_t x, std::uint32_t y) const;
};

struct ProjectionStats {
    double divergenceBefore{};
    double divergenceAfter{};
    std::size_t pressureIterations{};
};

[[nodiscard]] double divergenceL2(const MacGrid2D& grid);
[[nodiscard]] ProjectionStats projectIncompressible(MacGrid2D& grid, double dt,
                                                     std::size_t pressureIterations = 120);
void advectScalarSemiLagrangian(MacGrid2D& grid, double dt);

} // namespace vulkax::physics::cfd
