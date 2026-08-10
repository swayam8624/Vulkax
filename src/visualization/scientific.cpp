#include "vulkax/visualization/scientific.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace vulkax::visualization {

namespace {

Color interpolateColor(Color lhs, Color rhs, double t) {
    const float u = static_cast<float>(std::clamp(t, 0.0, 1.0));
    return {lhs.r + (rhs.r - lhs.r) * u,
            lhs.g + (rhs.g - lhs.g) * u,
            lhs.b + (rhs.b - lhs.b) * u,
            lhs.a + (rhs.a - lhs.a) * u};
}

Color sampleAnchors(const std::array<Color, 5>& anchors, double value) {
    const double scaled = std::clamp(value, 0.0, 1.0) * 4.0;
    const std::size_t index = std::min<std::size_t>(3, static_cast<std::size_t>(scaled));
    return interpolateColor(anchors[index], anchors[index + 1], scaled - static_cast<double>(index));
}

struct SamplePoint {
    math::Vec3 position;
    double value{};
};

math::Vec3 interpolateIso(const SamplePoint& a, const SamplePoint& b, double isoValue) {
    const double denominator = b.value - a.value;
    const double t = std::abs(denominator) > 1.0e-14 ? (isoValue - a.value) / denominator : 0.5;
    return a.position + (b.position - a.position) * std::clamp(t, 0.0, 1.0);
}

void appendTriangle(TriangleMesh& mesh, math::Vec3 a, math::Vec3 b, math::Vec3 c, Color color) {
    math::Vec3 normal = math::normalized(math::cross(b - a, c - a));
    if (math::length(normal) <= 1.0e-14) {
        return;
    }
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({a, normal, color});
    mesh.vertices.push_back({b, normal, color});
    mesh.vertices.push_back({c, normal, color});
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 1u);
    mesh.indices.push_back(base + 2u);
}

void polygonizeTetra(TriangleMesh& mesh, const std::array<SamplePoint, 4>& tetra,
                     double isoValue, Color color) {
    std::array<int, 4> inside{};
    int insideCount = 0;
    for (int index = 0; index < 4; ++index) {
        inside[index] = tetra[static_cast<std::size_t>(index)].value >= isoValue ? 1 : 0;
        insideCount += inside[index];
    }
    if (insideCount == 0 || insideCount == 4) {
        return;
    }

    std::vector<math::Vec3> intersections;
    constexpr std::array<std::array<int, 2>, 6> edges{{{{0, 1}}, {{0, 2}}, {{0, 3}}, {{1, 2}}, {{1, 3}}, {{2, 3}}}};
    for (const auto& edge : edges) {
        if (inside[edge[0]] == inside[edge[1]]) {
            continue;
        }
        intersections.push_back(interpolateIso(tetra[static_cast<std::size_t>(edge[0])],
                                                tetra[static_cast<std::size_t>(edge[1])], isoValue));
    }
    if (intersections.size() == 3) {
        if (insideCount == 1) {
            appendTriangle(mesh, intersections[0], intersections[1], intersections[2], color);
        } else {
            appendTriangle(mesh, intersections[0], intersections[2], intersections[1], color);
        }
    } else if (intersections.size() == 4) {
        appendTriangle(mesh, intersections[0], intersections[1], intersections[2], color);
        appendTriangle(mesh, intersections[0], intersections[2], intersections[3], color);
    }
}

} // namespace

Color sampleColorMap(ColorMap map, double normalizedValue) {
    static constexpr std::array<Color, 5> viridis{{
        {0.267F, 0.005F, 0.329F, 1.0F}, {0.230F, 0.322F, 0.546F, 1.0F},
        {0.128F, 0.567F, 0.551F, 1.0F}, {0.369F, 0.789F, 0.383F, 1.0F},
        {0.993F, 0.906F, 0.144F, 1.0F}}};
    static constexpr std::array<Color, 5> inferno{{
        {0.001F, 0.000F, 0.014F, 1.0F}, {0.341F, 0.062F, 0.429F, 1.0F},
        {0.735F, 0.216F, 0.330F, 1.0F}, {0.978F, 0.557F, 0.035F, 1.0F},
        {0.988F, 0.998F, 0.645F, 1.0F}}};
    static constexpr std::array<Color, 5> coolWarm{{
        {0.230F, 0.299F, 0.754F, 1.0F}, {0.554F, 0.690F, 0.996F, 1.0F},
        {0.865F, 0.865F, 0.865F, 1.0F}, {0.957F, 0.598F, 0.477F, 1.0F},
        {0.706F, 0.016F, 0.150F, 1.0F}}};
    switch (map) {
    case ColorMap::Viridis: return sampleAnchors(viridis, normalizedValue);
    case ColorMap::Inferno: return sampleAnchors(inferno, normalizedValue);
    case ColorMap::CoolWarm: return sampleAnchors(coolWarm, normalizedValue);
    }
    return {};
}

TriangleMesh extractIsoSurface(const numerics::ScalarGrid3D& field, double isoValue, ColorMap map) {
    TriangleMesh mesh;
    if (field.nx() < 2 || field.ny() < 2 || field.nz() < 2) {
        return mesh;
    }
    const auto [minimumIterator, maximumIterator] = std::minmax_element(field.values().begin(), field.values().end());
    const double range = *maximumIterator - *minimumIterator;
    const double normalizedIso = range > 0.0 ? (isoValue - *minimumIterator) / range : 0.5;
    const Color color = sampleColorMap(map, normalizedIso);
    const math::Vec3 h = field.spacing();

    constexpr std::array<std::array<int, 4>, 6> tetrahedra{{
        {{0, 5, 1, 6}}, {{0, 1, 2, 6}}, {{0, 2, 3, 6}},
        {{0, 3, 7, 6}}, {{0, 7, 4, 6}}, {{0, 4, 5, 6}}}};
    constexpr std::array<std::array<int, 3>, 8> offsets{{
        {{0, 0, 0}}, {{1, 0, 0}}, {{1, 1, 0}}, {{0, 1, 0}},
        {{0, 0, 1}}, {{1, 0, 1}}, {{1, 1, 1}}, {{0, 1, 1}}}};

    for (std::size_t z = 0; z + 1 < field.nz(); ++z) {
        for (std::size_t y = 0; y + 1 < field.ny(); ++y) {
            for (std::size_t x = 0; x + 1 < field.nx(); ++x) {
                std::array<SamplePoint, 8> cube{};
                for (std::size_t corner = 0; corner < cube.size(); ++corner) {
                    const auto offset = offsets[corner];
                    const std::size_t sx = x + static_cast<std::size_t>(offset[0]);
                    const std::size_t sy = y + static_cast<std::size_t>(offset[1]);
                    const std::size_t sz = z + static_cast<std::size_t>(offset[2]);
                    cube[corner] = {{static_cast<double>(sx) * h.x,
                                     static_cast<double>(sy) * h.y,
                                     static_cast<double>(sz) * h.z},
                                    field.at(sx, sy, sz)};
                }
                for (const auto& tetraIndices : tetrahedra) {
                    std::array<SamplePoint, 4> tetra{};
                    for (std::size_t vertex = 0; vertex < 4; ++vertex) {
                        tetra[vertex] = cube[static_cast<std::size_t>(tetraIndices[vertex])];
                    }
                    polygonizeTetra(mesh, tetra, isoValue, color);
                }
            }
        }
    }
    return mesh;
}

void writeObj(const TriangleMesh& mesh, const std::string& path) {
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("failed to open OBJ output: " + path);
    }
    for (const auto& vertex : mesh.vertices) {
        stream << "v " << vertex.position.x << ' ' << vertex.position.y << ' ' << vertex.position.z << '\n';
    }
    for (const auto& vertex : mesh.vertices) {
        stream << "vn " << vertex.normal.x << ' ' << vertex.normal.y << ' ' << vertex.normal.z << '\n';
    }
    for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
        const auto a = mesh.indices[index] + 1u;
        const auto b = mesh.indices[index + 1] + 1u;
        const auto c = mesh.indices[index + 2] + 1u;
        stream << "f " << a << "//" << a << ' ' << b << "//" << b << ' ' << c << "//" << c << '\n';
    }
}

VectorGrid3D::VectorGrid3D(std::size_t nx, std::size_t ny, std::size_t nz, math::Vec3 spacing,
                           math::Vec3 initial)
    : nx_(nx), ny_(ny), nz_(nz), spacing_(spacing), values_(nx * ny * nz, initial) {
    if (nx == 0 || ny == 0 || nz == 0 || spacing.x <= 0.0 || spacing.y <= 0.0 || spacing.z <= 0.0) {
        throw std::invalid_argument("vector grid dimensions and spacing must be positive");
    }
}

std::size_t VectorGrid3D::index(std::size_t x, std::size_t y, std::size_t z) const {
    if (x >= nx_ || y >= ny_ || z >= nz_) {
        throw std::out_of_range("VectorGrid3D index out of range");
    }
    return (z * ny_ + y) * nx_ + x;
}

math::Vec3& VectorGrid3D::at(std::size_t x, std::size_t y, std::size_t z) { return values_[index(x, y, z)]; }
math::Vec3 VectorGrid3D::at(std::size_t x, std::size_t y, std::size_t z) const { return values_[index(x, y, z)]; }

bool VectorGrid3D::contains(math::Vec3 position) const noexcept {
    return position.x >= 0.0 && position.y >= 0.0 && position.z >= 0.0 &&
           position.x <= static_cast<double>(nx_ - 1) * spacing_.x &&
           position.y <= static_cast<double>(ny_ - 1) * spacing_.y &&
           position.z <= static_cast<double>(nz_ - 1) * spacing_.z;
}

math::Vec3 VectorGrid3D::sampleTrilinear(math::Vec3 position) const {
    if (!contains(position)) {
        return {};
    }
    const double gx = position.x / spacing_.x;
    const double gy = position.y / spacing_.y;
    const double gz = position.z / spacing_.z;
    const std::size_t x0 = std::min(nx_ - 1, static_cast<std::size_t>(std::floor(gx)));
    const std::size_t y0 = std::min(ny_ - 1, static_cast<std::size_t>(std::floor(gy)));
    const std::size_t z0 = std::min(nz_ - 1, static_cast<std::size_t>(std::floor(gz)));
    const std::size_t x1 = std::min(nx_ - 1, x0 + 1);
    const std::size_t y1 = std::min(ny_ - 1, y0 + 1);
    const std::size_t z1 = std::min(nz_ - 1, z0 + 1);
    const double tx = gx - static_cast<double>(x0);
    const double ty = gy - static_cast<double>(y0);
    const double tz = gz - static_cast<double>(z0);
    const auto lerp = [](math::Vec3 a, math::Vec3 b, double t) { return a + (b - a) * t; };
    const auto c00 = lerp(at(x0, y0, z0), at(x1, y0, z0), tx);
    const auto c10 = lerp(at(x0, y1, z0), at(x1, y1, z0), tx);
    const auto c01 = lerp(at(x0, y0, z1), at(x1, y0, z1), tx);
    const auto c11 = lerp(at(x0, y1, z1), at(x1, y1, z1), tx);
    return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
}

Streamline traceStreamline(const VectorGrid3D& field, math::Vec3 seed, double stepLength,
                           std::size_t maxSteps, double minimumSpeed) {
    if (stepLength <= 0.0 || minimumSpeed < 0.0) {
        throw std::invalid_argument("invalid streamline integration settings");
    }
    Streamline result;
    math::Vec3 position = seed;
    for (std::size_t step = 0; step < maxSteps && field.contains(position); ++step) {
        result.points.push_back(position);
        const math::Vec3 v1 = field.sampleTrilinear(position);
        if (math::length(v1) < minimumSpeed) break;
        const math::Vec3 d1 = math::normalized(v1) * stepLength;
        const math::Vec3 v2 = field.sampleTrilinear(position + d1 * 0.5);
        const math::Vec3 d2 = math::normalized(v2) * stepLength;
        const math::Vec3 v3 = field.sampleTrilinear(position + d2 * 0.5);
        const math::Vec3 d3 = math::normalized(v3) * stepLength;
        const math::Vec3 v4 = field.sampleTrilinear(position + d3);
        const math::Vec3 d4 = math::normalized(v4) * stepLength;
        position += (d1 + 2.0 * d2 + 2.0 * d3 + d4) / 6.0;
    }
    return result;
}

std::vector<ParticleInstance> makeParticleInstances(const std::vector<solvers::DemParticle>& particles,
                                                     double minimumSpeed, double maximumSpeed,
                                                     ColorMap map) {
    if (!(minimumSpeed < maximumSpeed)) {
        throw std::invalid_argument("particle speed range must have positive width");
    }
    std::vector<ParticleInstance> result;
    result.reserve(particles.size());
    for (const auto& particle : particles) {
        const double speed = math::length(particle.velocity);
        const double normalized = (speed - minimumSpeed) / (maximumSpeed - minimumSpeed);
        result.push_back({particle.position, particle.radius, sampleColorMap(map, normalized)});
    }
    return result;
}

} // namespace vulkax::visualization
