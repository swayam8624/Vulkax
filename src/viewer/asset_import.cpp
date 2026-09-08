#include "vulkax/viewer/asset_import.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vulkax::viewer {
namespace {

constexpr double kSh0 = 0.28209479177387814;
constexpr std::uint32_t kObjNamespace = 0x4f424a01U; // "OBJ" presentation namespace.

struct ObjVertex {
    math::Vec3 position{};
    std::array<float, 3> color{};
    bool hasColor{};
};

struct ObjTriangle {
    std::uint32_t a{};
    std::uint32_t b{};
    std::uint32_t c{};
    double area{};
    math::Vec3 normal{};
};

[[nodiscard]] float clamp01(double value) noexcept {
    return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

[[nodiscard]] double fract(double value) noexcept {
    return value - std::floor(value);
}

[[nodiscard]] std::array<float, 3> sanitizeColor(
    double r, double g, double b, const std::array<float, 3>& fallback) {
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) return fallback;
    const double maximum = std::max({std::abs(r), std::abs(g), std::abs(b)});
    if (maximum > 1.0) {
        r /= 255.0;
        g /= 255.0;
        b /= 255.0;
    }
    return {clamp01(r), clamp01(g), clamp01(b)};
}

[[nodiscard]] std::uint32_t resolveObjIndex(std::string_view token, std::size_t vertexCount) {
    const auto slash = token.find('/');
    const std::string positionIndex(token.substr(0, slash));
    if (positionIndex.empty()) throw std::runtime_error("OBJ face contains an empty position index");
    const long long raw = std::stoll(positionIndex);
    if (raw == 0) throw std::runtime_error("OBJ indices are one-based; zero is invalid");
    long long resolved = 0;
    if (raw > 0) resolved = raw - 1;
    else resolved = static_cast<long long>(vertexCount) + raw;
    if (resolved < 0 || resolved >= static_cast<long long>(vertexCount))
        throw std::runtime_error("OBJ face position index is out of range");
    return static_cast<std::uint32_t>(resolved);
}

[[nodiscard]] std::array<float, 4> quaternionFromZToNormal(math::Vec3 normal) {
    normal = math::normalized(normal);
    if (math::length(normal) <= 1.0e-12) return {1.0F, 0.0F, 0.0F, 0.0F};
    const double dot = std::clamp(normal.z, -1.0, 1.0);
    if (dot < -0.999999) return {0.0F, 1.0F, 0.0F, 0.0F};
    const math::Vec3 axis{-normal.y, normal.x, 0.0};
    const double s = std::sqrt(std::max(2.0 * (1.0 + dot), 1.0e-18));
    const double inverse = 1.0 / s;
    std::array<float, 4> q{
        static_cast<float>(0.5 * s),
        static_cast<float>(axis.x * inverse),
        static_cast<float>(axis.y * inverse),
        static_cast<float>(axis.z * inverse),
    };
    const double length = std::sqrt(static_cast<double>(q[0])*q[0] + static_cast<double>(q[1])*q[1] +
                                    static_cast<double>(q[2])*q[2] + static_cast<double>(q[3])*q[3]);
    if (length <= 1.0e-12) return {1.0F, 0.0F, 0.0F, 0.0F};
    for (auto& value : q) value = static_cast<float>(value / length);
    return q;
}

[[nodiscard]] ViewerBounds boundsFromPositions(const std::vector<ObjVertex>& vertices) {
    if (vertices.empty()) return {{-0.5,-0.5,-0.5},{0.5,0.5,0.5},{0,0,0},1.0};
    math::Vec3 minimum{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    math::Vec3 maximum{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const auto& vertex : vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
    }
    const math::Vec3 center = (minimum + maximum) * 0.5;
    const double radius = std::max(0.05, 0.5 * math::length(maximum - minimum));
    return {minimum, maximum, center, radius};
}

[[nodiscard]] std::array<float, 3> vertexColor(const ObjVertex& vertex,
                                               const ObjImportSettings& settings) {
    return vertex.hasColor ? vertex.color : settings.defaultColor;
}

[[nodiscard]] ViewerGaussian gaussianFromSample(math::Vec3 position,
                                                math::Vec3 normal,
                                                std::array<float, 3> color,
                                                double tangentRadius,
                                                const ObjImportSettings& settings,
                                                std::uint32_t localId) {
    ViewerGaussian gaussian;
    gaussian.position = position;
    const float tangent = static_cast<float>(std::max(tangentRadius, 1.0e-7));
    const float thickness = static_cast<float>(std::max(tangentRadius * settings.normalThicknessRatio, 1.0e-7));
    gaussian.scale = {tangent, tangent, thickness};
    gaussian.rotation = quaternionFromZToNormal(normal);
    gaussian.color = color;
    gaussian.shDC = {
        static_cast<float>((static_cast<double>(color[0]) - 0.5) / kSh0),
        static_cast<float>((static_cast<double>(color[1]) - 0.5) / kSh0),
        static_cast<float>((static_cast<double>(color[2]) - 0.5) / kSh0),
    };
    gaussian.shCoefficientCount = 1U;
    gaussian.opacity = std::clamp(settings.opacity, 0.0F, 1.0F);
    gaussian.id = {kObjNamespace, localId};
    return gaussian;
}

} // namespace

ViewerScene loadObjAsGaussianScene(const std::filesystem::path& objPath,
                                   const ObjImportSettings& settings) {
    if (settings.maxSplats == 0U) throw std::invalid_argument("OBJ maxSplats must be positive");
    if (!(settings.splatRadiusMultiplier > 0.0) || !(settings.normalThicknessRatio > 0.0))
        throw std::invalid_argument("OBJ splat scale settings must be positive");

    std::ifstream input(objPath);
    if (!input) throw std::runtime_error("failed to open OBJ: " + objPath.string());

    std::vector<ObjVertex> vertices;
    std::vector<ObjTriangle> triangles;
    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::istringstream stream(line);
        std::string kind;
        if (!(stream >> kind) || kind.empty() || kind[0] == '#') continue;
        try {
            if (kind == "v") {
                double x = 0.0, y = 0.0, z = 0.0;
                if (!(stream >> x >> y >> z)) throw std::runtime_error("OBJ vertex is missing XYZ coordinates");
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                    throw std::runtime_error("OBJ vertex contains a non-finite coordinate");
                ObjVertex vertex;
                vertex.position = {x, y, z};
                double r = 0.0, g = 0.0, b = 0.0;
                if (stream >> r >> g >> b) {
                    vertex.color = sanitizeColor(r, g, b, settings.defaultColor);
                    vertex.hasColor = true;
                }
                vertices.push_back(vertex);
            } else if (kind == "f") {
                std::vector<std::uint32_t> polygon;
                std::string token;
                while (stream >> token) polygon.push_back(resolveObjIndex(token, vertices.size()));
                if (polygon.size() < 3U) throw std::runtime_error("OBJ face has fewer than three vertices");
                for (std::size_t i = 1U; i + 1U < polygon.size(); ++i)
                    triangles.push_back({polygon[0], polygon[i], polygon[i + 1U], 0.0, {}});
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("OBJ parse error at line " + std::to_string(lineNumber) + ": " + error.what());
        }
    }

    if (vertices.empty()) throw std::runtime_error("OBJ contains no vertices");
    const ViewerBounds bounds = boundsFromPositions(vertices);

    double totalArea = 0.0;
    std::vector<ObjTriangle> validTriangles;
    validTriangles.reserve(triangles.size());
    for (auto triangle : triangles) {
        const auto& a = vertices[triangle.a].position;
        const auto& b = vertices[triangle.b].position;
        const auto& c = vertices[triangle.c].position;
        const math::Vec3 cross = math::cross(b - a, c - a);
        const double twiceArea = math::length(cross);
        if (!std::isfinite(twiceArea) || twiceArea <= 1.0e-14) continue;
        triangle.area = 0.5 * twiceArea;
        triangle.normal = cross / twiceArea;
        totalArea += triangle.area;
        validTriangles.push_back(triangle);
    }

    ViewerScene scene;
    scene.bounds = bounds;
    scene.maxGaussianDisplacement = 0.0;
    scene.rewriteParticleCount = 0U;

    if (validTriangles.empty() || totalArea <= 0.0) {
        const std::size_t count = std::min(vertices.size(), settings.maxSplats);
        scene.before.reserve(count);
        const double radius = std::max(bounds.radius * 0.012, 1.0e-5);
        for (std::size_t i = 0; i < count; ++i) {
            const auto color = vertexColor(vertices[i], settings);
            scene.before.push_back(gaussianFromSample(vertices[i].position, {0,0,1}, color, radius, settings,
                                                      static_cast<std::uint32_t>(i + 1U)));
        }
        scene.after = scene.before;
        scene.surfaceKind = "obj_points";
        return scene;
    }

    scene.surfaceTriangles.reserve(validTriangles.size() * 3U);
    for (const auto& triangle : validTriangles) {
        const ObjVertex* triangleVertices[3] = {&vertices[triangle.a], &vertices[triangle.b], &vertices[triangle.c]};
        for (const auto* vertex : triangleVertices) {
            ViewerSurfaceVertex output;
            output.position = vertex->position;
            output.normal = triangle.normal;
            output.color = vertexColor(*vertex, settings);
            scene.surfaceTriangles.push_back(output);
        }
    }
    scene.surfaceKind = "obj_mesh";

    const std::size_t requested = settings.targetSplats == 0U ? validTriangles.size() : settings.targetSplats;
    const std::size_t sampleCount = std::max<std::size_t>(1U, std::min(requested, settings.maxSplats));
    scene.before.reserve(sampleCount);

    const double areaPerSample = totalArea / static_cast<double>(sampleCount);
    const double tangentRadius = std::clamp(
        std::sqrt(std::max(areaPerSample, 1.0e-20) / 3.14159265358979323846) * settings.splatRadiusMultiplier,
        bounds.radius * 1.0e-6,
        bounds.radius * 0.08);

    std::size_t triangleIndex = 0U;
    double cumulative = validTriangles[0].area;
    for (std::size_t sample = 0U; sample < sampleCount; ++sample) {
        const double targetArea = (static_cast<double>(sample) + 0.5) / static_cast<double>(sampleCount) * totalArea;
        while (triangleIndex + 1U < validTriangles.size() && targetArea > cumulative) {
            ++triangleIndex;
            cumulative += validTriangles[triangleIndex].area;
        }
        const auto& triangle = validTriangles[triangleIndex];
        const auto& a = vertices[triangle.a];
        const auto& b = vertices[triangle.b];
        const auto& c = vertices[triangle.c];

        // Two irrational rotations provide deterministic, well-distributed samples.
        const double u = fract((static_cast<double>(sample) + 1.0) * 0.7548776662466927);
        const double v = fract((static_cast<double>(sample) + 1.0) * 0.5698402909980532);
        const double root = std::sqrt(u);
        const double wa = 1.0 - root;
        const double wb = root * (1.0 - v);
        const double wc = root * v;
        const math::Vec3 position = wa * a.position + wb * b.position + wc * c.position;

        const auto ca = vertexColor(a, settings);
        const auto cb = vertexColor(b, settings);
        const auto cc = vertexColor(c, settings);
        std::array<float, 3> color{};
        for (std::size_t channel = 0U; channel < 3U; ++channel)
            color[channel] = clamp01(wa * ca[channel] + wb * cb[channel] + wc * cc[channel]);

        scene.before.push_back(gaussianFromSample(position, triangle.normal, color, tangentRadius, settings,
                                                  static_cast<std::uint32_t>(sample + 1U)));
    }
    scene.after = scene.before;
    return scene;
}

} // namespace vulkax::viewer
