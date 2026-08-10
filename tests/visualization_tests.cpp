#include "vulkax/numerics/grid.hpp"
#include "vulkax/visualization/scientific.hpp"

#include <cassert>
#include <cmath>

int main() {
    using namespace vulkax;

    constexpr std::size_t n = 24;
    numerics::ScalarGrid3D field(n, n, n, {0.08, 0.08, 0.08});
    const math::Vec3 center{0.92, 0.92, 0.92};
    for (std::size_t z = 0; z < n; ++z) {
        for (std::size_t y = 0; y < n; ++y) {
            for (std::size_t x = 0; x < n; ++x) {
                const math::Vec3 p{static_cast<double>(x) * 0.08,
                                   static_cast<double>(y) * 0.08,
                                   static_cast<double>(z) * 0.08};
                field.at(x, y, z) = 0.55 - math::length(p - center);
            }
        }
    }
    const auto mesh = visualization::extractIsoSurface(field, 0.0);
    assert(mesh.vertices.size() > 1000);
    assert(mesh.indices.size() == mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) {
        assert(std::isfinite(vertex.position.x));
        assert(std::abs(math::length(vertex.normal) - 1.0) < 1.0e-9);
    }

    visualization::VectorGrid3D vectorField(16, 16, 4, {0.1, 0.1, 0.1});
    for (std::size_t z = 0; z < vectorField.nz(); ++z) {
        for (std::size_t y = 0; y < vectorField.ny(); ++y) {
            for (std::size_t x = 0; x < vectorField.nx(); ++x) {
                const double px = static_cast<double>(x) * 0.1 - 0.75;
                const double py = static_cast<double>(y) * 0.1 - 0.75;
                vectorField.at(x, y, z) = {-py, px, 0.0};
            }
        }
    }
    const auto streamline = visualization::traceStreamline(vectorField, {1.1, 0.75, 0.1}, 0.02, 80);
    assert(streamline.points.size() > 20);
    return 0;
}
