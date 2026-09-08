#include "vulkax/viewer/scene.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_set>

namespace vulkax::viewer {
namespace {

constexpr double sh0 = 0.28209479177387814;

[[nodiscard]] float clamp01(double value) noexcept {
    return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

[[nodiscard]] std::vector<std::string> splitCsvLine(std::string_view line) {
    std::vector<std::string> fields;
    std::string current;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (ch == '"') {
            if (quoted && index + 1U < line.size() && line[index + 1U] == '"') {
                current.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (quoted) throw std::runtime_error("unterminated quoted CSV field");
    fields.push_back(current);
    return fields;
}

[[nodiscard]] std::size_t columnIndex(const std::vector<std::string>& header,
                                      std::initializer_list<std::string_view> names,
                                      const std::string& description) {
    for (const auto name : names) {
        const auto it = std::find(header.begin(), header.end(), name);
        if (it != header.end()) return static_cast<std::size_t>(std::distance(header.begin(), it));
    }
    throw std::runtime_error("CSV is missing " + description + " column");
}

[[nodiscard]] std::unordered_set<std::uint32_t> loadRewriteIds(const std::filesystem::path& path) {
    std::unordered_set<std::uint32_t> ids;
    if (!std::filesystem::exists(path)) return ids;

    std::ifstream input(path);
    if (!input) throw std::runtime_error("failed to open rewrite-region CSV: " + path.string());
    std::string line;
    if (!std::getline(input, line)) return ids;
    auto header = splitCsvLine(line);
    const auto idColumn = columnIndex(header, {"particle_id", "id"}, "particle ID");
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        auto fields = splitCsvLine(line);
        if (idColumn >= fields.size() || fields[idColumn].empty()) continue;
        const auto value = std::stoull(fields[idColumn]);
        if (value == 0U || value > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("rewrite-region particle ID is out of uint32 range");
        ids.insert(static_cast<std::uint32_t>(value));
    }
    return ids;
}

[[nodiscard]] std::vector<ViewerParticle> loadParticles(
    const std::filesystem::path& path,
    const std::unordered_set<std::uint32_t>& rewriteIds) {
    std::vector<ViewerParticle> particles;
    if (path.empty() || !std::filesystem::exists(path)) return particles;

    std::ifstream input(path);
    if (!input) throw std::runtime_error("failed to open particle CSV: " + path.string());
    std::string line;
    if (!std::getline(input, line)) return particles;
    auto header = splitCsvLine(line);
    const auto idColumn = columnIndex(header, {"particle_id", "id"}, "particle ID");
    const auto xColumn = columnIndex(header, {"rest_x", "x", "position_x"}, "particle x");
    const auto yColumn = columnIndex(header, {"rest_y", "y", "position_y"}, "particle y");
    const auto zColumn = columnIndex(header, {"rest_z", "z", "position_z"}, "particle z");
    const auto needed = std::max({idColumn, xColumn, yColumn, zColumn});

    while (std::getline(input, line)) {
        if (line.empty()) continue;
        auto fields = splitCsvLine(line);
        if (needed >= fields.size()) throw std::runtime_error("truncated particle CSV row");
        const auto rawId = std::stoull(fields[idColumn]);
        if (rawId == 0U || rawId > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("particle ID is out of uint32 range");
        const auto id = static_cast<std::uint32_t>(rawId);
        ViewerParticle particle;
        particle.id = id;
        particle.position = {std::stod(fields[xColumn]), std::stod(fields[yColumn]), std::stod(fields[zColumn])};
        particle.inRewriteRegion = rewriteIds.contains(id);
        particles.push_back(particle);
    }

    std::sort(particles.begin(), particles.end(), [](const ViewerParticle& lhs, const ViewerParticle& rhs) {
        return lhs.id < rhs.id;
    });
    return particles;
}

[[nodiscard]] std::vector<double> uniqueAxis(const std::vector<ViewerParticle>& particles, int component) {
    std::vector<double> values;
    values.reserve(particles.size());
    for (const auto& particle : particles) {
        const auto p = particle.position;
        values.push_back(component == 0 ? p.x : component == 1 ? p.y : p.z);
    }
    std::sort(values.begin(), values.end());
    std::vector<double> unique;
    for (const double value : values) {
        if (unique.empty() || std::abs(value - unique.back()) > 1.0e-8) unique.push_back(value);
    }
    return unique;
}

[[nodiscard]] std::size_t nearestIndex(double value, const std::vector<double>& axis) {
    if (axis.empty()) throw std::logic_error("empty lattice axis");
    std::size_t best = 0;
    double error = std::abs(value - axis.front());
    for (std::size_t index = 1; index < axis.size(); ++index) {
        const double candidate = std::abs(value - axis[index]);
        if (candidate < error) {
            error = candidate;
            best = index;
        }
    }
    return best;
}

void appendTriangle(std::vector<ViewerSurfaceVertex>& output,
                    const ViewerParticle& a,
                    const ViewerParticle& b,
                    const ViewerParticle& c,
                    math::Vec3 normal) {
    const auto makeVertex = [normal](const ViewerParticle& particle) {
        ViewerSurfaceVertex vertex;
        vertex.position = particle.position;
        vertex.normal = normal;
        vertex.color = particle.inRewriteRegion
                           ? std::array<float, 3>{1.0F, 0.42F, 0.10F}
                           : std::array<float, 3>{0.28F, 0.53F, 0.95F};
        return vertex;
    };
    output.push_back(makeVertex(a));
    output.push_back(makeVertex(b));
    output.push_back(makeVertex(c));
}

[[nodiscard]] std::pair<std::vector<ViewerSurfaceVertex>, std::string> buildPhysicalSurface(
    const std::vector<ViewerParticle>& particles) {
    if (particles.size() < 8U) return {{}, "none"};
    const auto xs = uniqueAxis(particles, 0);
    const auto ys = uniqueAxis(particles, 1);
    const auto zs = uniqueAxis(particles, 2);
    if (xs.size() * ys.size() * zs.size() != particles.size()) return {{}, "none"};

    using Key = std::tuple<std::size_t, std::size_t, std::size_t>;
    std::map<Key, const ViewerParticle*> lookup;
    for (const auto& particle : particles) {
        const auto key = Key{nearestIndex(particle.position.x, xs),
                             nearestIndex(particle.position.y, ys),
                             nearestIndex(particle.position.z, zs)};
        if (!lookup.emplace(key, &particle).second) return {{}, "none"};
    }

    auto require = [&lookup](std::size_t x, std::size_t y, std::size_t z) -> const ViewerParticle& {
        const auto it = lookup.find({x, y, z});
        if (it == lookup.end()) throw std::logic_error("regular particle lattice has a missing cell");
        return *it->second;
    };

    std::vector<ViewerSurfaceVertex> surface;
    const auto emitQuad = [&surface, &require](Key a, Key b, Key c, Key d, math::Vec3 normal) {
        const auto& pa = require(std::get<0>(a), std::get<1>(a), std::get<2>(a));
        const auto& pb = require(std::get<0>(b), std::get<1>(b), std::get<2>(b));
        const auto& pc = require(std::get<0>(c), std::get<1>(c), std::get<2>(c));
        const auto& pd = require(std::get<0>(d), std::get<1>(d), std::get<2>(d));
        appendTriangle(surface, pa, pb, pc, normal);
        appendTriangle(surface, pa, pc, pd, normal);
    };

    const auto nx = xs.size();
    const auto ny = ys.size();
    const auto nz = zs.size();
    surface.reserve(12U * ((ny - 1U) * (nz - 1U) + (nx - 1U) * (nz - 1U) + (nx - 1U) * (ny - 1U)));

    for (std::size_t y = 0; y + 1U < ny; ++y) {
        for (std::size_t z = 0; z + 1U < nz; ++z) {
            emitQuad({0, y, z}, {0, y, z + 1U}, {0, y + 1U, z + 1U}, {0, y + 1U, z}, {-1, 0, 0});
            emitQuad({nx - 1U, y, z}, {nx - 1U, y + 1U, z}, {nx - 1U, y + 1U, z + 1U},
                     {nx - 1U, y, z + 1U}, {1, 0, 0});
        }
    }
    for (std::size_t x = 0; x + 1U < nx; ++x) {
        for (std::size_t z = 0; z + 1U < nz; ++z) {
            emitQuad({x, 0, z}, {x + 1U, 0, z}, {x + 1U, 0, z + 1U}, {x, 0, z + 1U}, {0, -1, 0});
            emitQuad({x, ny - 1U, z}, {x, ny - 1U, z + 1U}, {x + 1U, ny - 1U, z + 1U},
                     {x + 1U, ny - 1U, z}, {0, 1, 0});
        }
    }
    for (std::size_t x = 0; x + 1U < nx; ++x) {
        for (std::size_t y = 0; y + 1U < ny; ++y) {
            emitQuad({x, y, 0}, {x, y + 1U, 0}, {x + 1U, y + 1U, 0}, {x + 1U, y, 0}, {0, 0, -1});
            emitQuad({x, y, nz - 1U}, {x + 1U, y, nz - 1U}, {x + 1U, y + 1U, nz - 1U},
                     {x, y + 1U, nz - 1U}, {0, 0, 1});
        }
    }

    std::ostringstream kind;
    kind << "physical_lattice_" << nx << 'x' << ny << 'x' << nz;
    return {std::move(surface), kind.str()};
}

[[nodiscard]] ViewerBounds computeBounds(const ViewerScene& scene) {
    math::Vec3 minimum{std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::infinity()};
    math::Vec3 maximum{-std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()};
    bool havePoint = false;
    const auto add = [&](math::Vec3 p) mutable {
        minimum.x = std::min(minimum.x, p.x);
        minimum.y = std::min(minimum.y, p.y);
        minimum.z = std::min(minimum.z, p.z);
        maximum.x = std::max(maximum.x, p.x);
        maximum.y = std::max(maximum.y, p.y);
        maximum.z = std::max(maximum.z, p.z);
        havePoint = true;
    };
    for (const auto& value : scene.before) add(value.position);
    for (const auto& value : scene.after) add(value.position);
    for (const auto& value : scene.particles) add(value.position);
    if (!havePoint) return {{-0.5, -0.5, -0.5}, {0.5, 0.5, 0.5}, {0, 0, 0}, 1.0};

    ViewerBounds bounds;
    bounds.minimum = minimum;
    bounds.maximum = maximum;
    bounds.center = (minimum + maximum) * 0.5;
    const auto extent = maximum - minimum;
    bounds.radius = std::max(0.05, 0.5 * std::sqrt(math::dot(extent, extent)));
    return bounds;
}

[[nodiscard]] std::filesystem::path defaultParticlesPath(const std::filesystem::path& runDirectory) {
    return runDirectory.parent_path() / "captured-example" / "particles.csv";
}

} // namespace

std::vector<ViewerGaussian> makeViewerGaussians(const gaussian::GaussianCloud& cloud) {
    std::vector<ViewerGaussian> output;
    output.reserve(cloud.size());
    for (const auto& source : cloud.splats) {
        ViewerGaussian value;
        value.position = source.position;
        const auto linearScale = source.linearScale();
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            const double safe = std::isfinite(linearScale[axis]) ? linearScale[axis] : 0.02;
            value.scale[axis] = static_cast<float>(std::clamp(safe, 1.0e-6, 1.0e4));
        }
        for (std::size_t axis = 0; axis < 4U; ++axis)
            value.rotation[axis] = static_cast<float>(source.rotation[axis]);
        for (std::size_t channel = 0; channel < 3U; ++channel)
            value.color[channel] = clamp01(0.5 + sh0 * source.shDC[channel]);
        value.opacity = clamp01(source.opacity());
        value.id = source.id;
        output.push_back(value);
    }
    return output;
}

ViewerScene loadStandaloneGaussianScene(const std::filesystem::path& plyPath) {
    const auto cloud = gaussian::load3dgsPly(plyPath);
    ViewerScene scene;
    scene.before = makeViewerGaussians(cloud);
    scene.after = scene.before;
    scene.bounds = computeBounds(scene);
    return scene;
}

ViewerScene loadCapturedWorldScene(const std::filesystem::path& runDirectory,
                                   const std::filesystem::path& particlesCsv) {
    const auto beforePath = runDirectory / "appearance" / "before.ply";
    const auto afterPath = runDirectory / "appearance" / "rewritten.ply";
    if (!std::filesystem::exists(beforePath))
        throw std::runtime_error("captured-world viewer is missing appearance/before.ply: " + beforePath.string());
    if (!std::filesystem::exists(afterPath))
        throw std::runtime_error("captured-world viewer is missing appearance/rewritten.ply: " + afterPath.string());

    const auto beforeCloud = gaussian::load3dgsPly(beforePath);
    const auto afterCloud = gaussian::load3dgsPly(afterPath);
    gaussian::GaussianIndexView afterIndex(afterCloud);

    ViewerScene scene;
    scene.before = makeViewerGaussians(beforeCloud);
    scene.after = makeViewerGaussians(afterCloud);
    for (const auto& splat : beforeCloud.splats) {
        const auto index = afterIndex.index(splat.id);
        if (!index.has_value()) continue;
        scene.maxGaussianDisplacement = std::max(
            scene.maxGaussianDisplacement,
            math::length(afterCloud.splats[*index].position - splat.position));
    }

    const auto rewriteIds = loadRewriteIds(runDirectory / "influence" / "selected_rewrite_region.csv");
    scene.rewriteParticleCount = rewriteIds.size();
    const auto resolvedParticles = particlesCsv.empty() ? defaultParticlesPath(runDirectory) : particlesCsv;
    scene.particles = loadParticles(resolvedParticles, rewriteIds);
    auto [surface, kind] = buildPhysicalSurface(scene.particles);
    scene.surfaceTriangles = std::move(surface);
    scene.surfaceKind = std::move(kind);
    scene.bounds = computeBounds(scene);
    return scene;
}

} // namespace vulkax::viewer
