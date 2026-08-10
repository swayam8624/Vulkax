#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vulkax::field {

struct Vec3 { double x{}, y{}, z{}; };
struct Mat3 { std::array<double, 9> v{}; };

[[nodiscard]] Vec3 operator+(Vec3 a, Vec3 b) noexcept;
[[nodiscard]] Vec3 operator-(Vec3 a, Vec3 b) noexcept;
[[nodiscard]] Vec3 operator*(Vec3 a, double s) noexcept;
[[nodiscard]] Vec3 operator/(Vec3 a, double s);
[[nodiscard]] double dot(Vec3 a, Vec3 b) noexcept;
[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b) noexcept;
[[nodiscard]] double length(Vec3 a) noexcept;
[[nodiscard]] Vec3 normalized(Vec3 a) noexcept;

struct GridShape {
    std::uint32_t nx{1}, ny{1}, nz{1};
    Vec3 origin{};
    Vec3 spacing{1.0, 1.0, 1.0};
    [[nodiscard]] std::size_t cellCount() const noexcept;
    [[nodiscard]] std::size_t index(std::uint32_t x, std::uint32_t y, std::uint32_t z) const;
};

struct ScalarField { GridShape grid; std::vector<double> values; };
struct VectorField { GridShape grid; std::vector<Vec3> values; };
struct TensorField { GridShape grid; std::vector<Mat3> values; };

struct ParticleSet {
    std::vector<Vec3> positions;
    std::vector<Vec3> velocities;
    std::vector<double> radii;
    std::vector<double> masses;
};

struct TriangleMesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

[[nodiscard]] VectorField gradient(const ScalarField& input);
[[nodiscard]] ScalarField divergence(const VectorField& input);
[[nodiscard]] ScalarField laplacian(const ScalarField& input);

} // namespace vulkax::field
