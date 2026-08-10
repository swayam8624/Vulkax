#pragma once

#include "vulkax/core/math.hpp"
#include "vulkax/numerics/grid.hpp"
#include "vulkax/solvers/dem.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vulkax::visualization {

struct Color {
    float r{};
    float g{};
    float b{};
    float a{1.0F};
};

enum class ColorMap { Viridis, Inferno, CoolWarm };

[[nodiscard]] Color sampleColorMap(ColorMap map, double normalizedValue);

struct MeshVertex {
    math::Vec3 position{};
    math::Vec3 normal{};
    Color color{};
};

struct TriangleMesh {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Converts an arbitrary scalar field to geometry. The implementation uses marching tetrahedra,
// avoiding ambiguous cube cases while keeping the representation backend-independent.
[[nodiscard]] TriangleMesh extractIsoSurface(const numerics::ScalarGrid3D& field,
                                             double isoValue,
                                             ColorMap map = ColorMap::Viridis);
void writeObj(const TriangleMesh& mesh, const std::string& path);

class VectorGrid3D {
public:
    VectorGrid3D(std::size_t nx, std::size_t ny, std::size_t nz, math::Vec3 spacing,
                 math::Vec3 initial = {});
    [[nodiscard]] std::size_t nx() const noexcept { return nx_; }
    [[nodiscard]] std::size_t ny() const noexcept { return ny_; }
    [[nodiscard]] std::size_t nz() const noexcept { return nz_; }
    [[nodiscard]] math::Vec3 spacing() const noexcept { return spacing_; }
    math::Vec3& at(std::size_t x, std::size_t y, std::size_t z);
    [[nodiscard]] math::Vec3 at(std::size_t x, std::size_t y, std::size_t z) const;
    [[nodiscard]] math::Vec3 sampleTrilinear(math::Vec3 position) const;
    [[nodiscard]] bool contains(math::Vec3 position) const noexcept;

private:
    [[nodiscard]] std::size_t index(std::size_t x, std::size_t y, std::size_t z) const;
    std::size_t nx_{};
    std::size_t ny_{};
    std::size_t nz_{};
    math::Vec3 spacing_{1.0, 1.0, 1.0};
    std::vector<math::Vec3> values_;
};

struct Streamline {
    std::vector<math::Vec3> points;
};

[[nodiscard]] Streamline traceStreamline(const VectorGrid3D& field, math::Vec3 seed,
                                         double stepLength, std::size_t maxSteps,
                                         double minimumSpeed = 1.0e-9);

struct ParticleInstance {
    math::Vec3 position{};
    double radius{};
    Color color{};
};

[[nodiscard]] std::vector<ParticleInstance> makeParticleInstances(
    const std::vector<solvers::DemParticle>& particles, double minimumSpeed, double maximumSpeed,
    ColorMap map = ColorMap::Inferno);

} // namespace vulkax::visualization
