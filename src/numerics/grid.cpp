#include "vulkax/numerics/grid.hpp"

#include <algorithm>
#include <stdexcept>

namespace vulkax::numerics {

namespace {

std::size_t periodicIndex(long long value, std::size_t extent) {
    const long long n = static_cast<long long>(extent);
    long long wrapped = value % n;
    if (wrapped < 0) {
        wrapped += n;
    }
    return static_cast<std::size_t>(wrapped);
}

} // namespace

ScalarGrid3D::ScalarGrid3D(std::size_t nx, std::size_t ny, std::size_t nz, math::Vec3 spacing,
                           double initial)
    : nx_(nx), ny_(ny), nz_(nz), spacing_(spacing), values_(nx * ny * nz, initial) {
    if (nx == 0 || ny == 0 || nz == 0 || spacing.x <= 0.0 || spacing.y <= 0.0 ||
        spacing.z <= 0.0) {
        throw std::invalid_argument("grid dimensions and spacing must be positive");
    }
}

std::size_t ScalarGrid3D::index(std::size_t x, std::size_t y, std::size_t z) const {
    if (x >= nx_ || y >= ny_ || z >= nz_) {
        throw std::out_of_range("ScalarGrid3D index out of range");
    }
    return (z * ny_ + y) * nx_ + x;
}

double& ScalarGrid3D::at(std::size_t x, std::size_t y, std::size_t z) {
    return values_[index(x, y, z)];
}

double ScalarGrid3D::at(std::size_t x, std::size_t y, std::size_t z) const {
    return values_[index(x, y, z)];
}

double ScalarGrid3D::sample(long long x, long long y, long long z, BoundaryMode mode) const {
    const bool inside = x >= 0 && y >= 0 && z >= 0 && x < static_cast<long long>(nx_) &&
                        y < static_cast<long long>(ny_) && z < static_cast<long long>(nz_);
    if (inside) {
        return at(static_cast<std::size_t>(x), static_cast<std::size_t>(y),
                  static_cast<std::size_t>(z));
    }
    if (mode == BoundaryMode::ZeroDirichlet) {
        return 0.0;
    }
    if (mode == BoundaryMode::Periodic) {
        return at(periodicIndex(x, nx_), periodicIndex(y, ny_), periodicIndex(z, nz_));
    }
    const auto clampIndex = [](long long value, std::size_t extent) {
        const long long maximum = static_cast<long long>(extent) - 1;
        return static_cast<std::size_t>(std::clamp(value, 0LL, maximum));
    };
    return at(clampIndex(x, nx_), clampIndex(y, ny_), clampIndex(z, nz_));
}

double laplacianAt(const ScalarGrid3D& grid, std::size_t x, std::size_t y, std::size_t z,
                   BoundaryMode mode) {
    const math::Vec3 h = grid.spacing();
    const double center = grid.at(x, y, z);
    const long long ix = static_cast<long long>(x);
    const long long iy = static_cast<long long>(y);
    const long long iz = static_cast<long long>(z);
    const double ddx = (grid.sample(ix - 1, iy, iz, mode) - 2.0 * center +
                        grid.sample(ix + 1, iy, iz, mode)) /
                       (h.x * h.x);
    const double ddy = (grid.sample(ix, iy - 1, iz, mode) - 2.0 * center +
                        grid.sample(ix, iy + 1, iz, mode)) /
                       (h.y * h.y);
    const double ddz = (grid.sample(ix, iy, iz - 1, mode) - 2.0 * center +
                        grid.sample(ix, iy, iz + 1, mode)) /
                       (h.z * h.z);
    return ddx + ddy + ddz;
}

double integral(const ScalarGrid3D& grid) {
    const math::Vec3 h = grid.spacing();
    double sum = 0.0;
    for (double value : grid.values()) {
        sum += value;
    }
    return sum * h.x * h.y * h.z;
}

} // namespace vulkax::numerics
