#pragma once

#include "vulkax/core/math.hpp"

#include <cstddef>
#include <vector>

namespace vulkax::numerics {

enum class BoundaryMode { ZeroDirichlet, Periodic, NeumannZero };

class ScalarGrid3D {
public:
    ScalarGrid3D() = default;
    ScalarGrid3D(std::size_t nx, std::size_t ny, std::size_t nz, math::Vec3 spacing,
                 double initial = 0.0);

    [[nodiscard]] std::size_t nx() const noexcept { return nx_; }
    [[nodiscard]] std::size_t ny() const noexcept { return ny_; }
    [[nodiscard]] std::size_t nz() const noexcept { return nz_; }
    [[nodiscard]] math::Vec3 spacing() const noexcept { return spacing_; }
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

    double& at(std::size_t x, std::size_t y, std::size_t z);
    [[nodiscard]] double at(std::size_t x, std::size_t y, std::size_t z) const;
    [[nodiscard]] const std::vector<double>& values() const noexcept { return values_; }
    [[nodiscard]] std::vector<double>& values() noexcept { return values_; }
    [[nodiscard]] double sample(long long x, long long y, long long z, BoundaryMode mode) const;

private:
    [[nodiscard]] std::size_t index(std::size_t x, std::size_t y, std::size_t z) const;

    std::size_t nx_{};
    std::size_t ny_{};
    std::size_t nz_{};
    math::Vec3 spacing_{1.0, 1.0, 1.0};
    std::vector<double> values_;
};

[[nodiscard]] double laplacianAt(const ScalarGrid3D& grid, std::size_t x, std::size_t y,
                                 std::size_t z, BoundaryMode mode);
[[nodiscard]] double integral(const ScalarGrid3D& grid);

} // namespace vulkax::numerics
