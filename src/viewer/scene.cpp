#include "vulkax/viewer/scene.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_set>

namespace vulkax::viewer {
namespace {

constexpr double sh0 = 0.28209479177387814;
using RewriteIds = std::unordered_set<std::uint32_t>;
using Key = std::tuple<std::size_t, std::size_t, std::size_t>;

[[nodiscard]] float clamp01(double value) noexcept {
    return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

[[nodiscard]] std::vector<std::string> splitCsv(std::string_view line) {
    std::vector<std::string> out;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1U < line.size() && line[i + 1U] == '"') {
                field.push_back('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            out.push_back(field);
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    if (quoted) throw std::runtime_error("unterminated quoted CSV field");
    out.push_back(field);
    return out;
}

[[nodiscard]] std::size_t findColumn(const std::vector<std::string>& header,
                                     std::initializer_list<std::string_view> names,
                                     std::string_view description) {
    for (const auto name : names) {
        const auto it = std::find(header.begin(), header.end(), name);
        if (it != header.end()) return static_cast<std::size_t>(std::distance(header.begin(), it));
    }
    throw std::runtime_error("CSV is missing " + std::string(description) + " column");
}

[[nodiscard]] RewriteIds loadRewriteIds(const std::filesystem::path& path) {
    RewriteIds ids;
    if (!std::filesystem::exists(path)) return ids;
    std::ifstream input(path);
    if (!input) throw std::runtime_error("failed to open rewrite-region CSV: " + path.string());
    std::string line;
    if (!std::getline(input, line)) return ids;
    const auto header = splitCsv(line);
    const auto idCol = findColumn(header, {"particle_id", "id"}, "particle ID");
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto fields = splitCsv(line);
        if (idCol >= fields.size() || fields[idCol].empty()) continue;
        const auto raw = std::stoull(fields[idCol]);
        if (raw == 0U || raw > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("rewrite-region particle ID is out of uint32 range");
        ids.insert(static_cast<std::uint32_t>(raw));
    }
    return ids;
}

[[nodiscard]] std::vector<ViewerParticle> loadParticles(const std::filesystem::path& path,
                                                        const RewriteIds& rewriteIds) {
    std::vector<ViewerParticle> particles;
    if (path.empty() || !std::filesystem::exists(path)) return particles;
    std::ifstream input(path);
    if (!input) throw std::runtime_error("failed to open particle CSV: " + path.string());
    std::string line;
    if (!std::getline(input, line)) return particles;
    const auto header = splitCsv(line);
    const auto idCol = findColumn(header, {"particle_id", "id"}, "particle ID");
    const auto xCol = findColumn(header, {"rest_x", "x", "position_x"}, "particle x");
    const auto yCol = findColumn(header, {"rest_y", "y", "position_y"}, "particle y");
    const auto zCol = findColumn(header, {"rest_z", "z", "position_z"}, "particle z");
    const auto needed = std::max({idCol, xCol, yCol, zCol});
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto fields = splitCsv(line);
        if (needed >= fields.size()) throw std::runtime_error("truncated particle CSV row");
        const auto raw = std::stoull(fields[idCol]);
        if (raw == 0U || raw > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("particle ID is out of uint32 range");
        const auto id = static_cast<std::uint32_t>(raw);
        particles.push_back({id,
                             {std::stod(fields[xCol]), std::stod(fields[yCol]), std::stod(fields[zCol])},
                             rewriteIds.contains(id)});
    }
    std::sort(particles.begin(), particles.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return particles;
}

[[nodiscard]] std::vector<double> uniqueAxis(const std::vector<ViewerParticle>& particles, int component) {
    std::vector<double> axis;
    axis.reserve(particles.size());
    for (const auto& particle : particles) {
        if (component == 0) axis.push_back(particle.position.x);
        else if (component == 1) axis.push_back(particle.position.y);
        else axis.push_back(particle.position.z);
    }
    std::sort(axis.begin(), axis.end());
    axis.erase(std::unique(axis.begin(), axis.end(), [](double a, double b) { return std::abs(a - b) <= 1.0e-8; }), axis.end());
    return axis;
}

[[nodiscard]] std::size_t nearestIndex(double value, const std::vector<double>& axis) {
    if (axis.empty()) throw std::logic_error("empty lattice axis");
    std::size_t best = 0;
    for (std::size_t i = 1; i < axis.size(); ++i)
        if (std::abs(value - axis[i]) < std::abs(value - axis[best])) best = i;
    return best;
}

[[nodiscard]] ViewerSurfaceVertex surfaceVertex(const ViewerParticle& p, math::Vec3 normal) {
    ViewerSurfaceVertex v;
    v.position = p.position;
    v.normal = normal;
    v.color = p.inRewriteRegion ? std::array<float, 3>{1.0F, 0.42F, 0.10F}
                                : std::array<float, 3>{0.28F, 0.53F, 0.95F};
    return v;
}

void triangle(std::vector<ViewerSurfaceVertex>& out,
              const ViewerParticle& a, const ViewerParticle& b, const ViewerParticle& c,
              math::Vec3 normal) {
    out.push_back(surfaceVertex(a, normal));
    out.push_back(surfaceVertex(b, normal));
    out.push_back(surfaceVertex(c, normal));
}

[[nodiscard]] std::pair<std::vector<ViewerSurfaceVertex>, std::string> buildSurface(
    const std::vector<ViewerParticle>& particles) {
    if (particles.size() < 8U) return {{}, "none"};
    const auto xs = uniqueAxis(particles, 0);
    const auto ys = uniqueAxis(particles, 1);
    const auto zs = uniqueAxis(particles, 2);
    if (xs.size() * ys.size() * zs.size() != particles.size()) return {{}, "none"};

    std::map<Key, const ViewerParticle*> lookup;
    for (const auto& p : particles) {
        const Key key{nearestIndex(p.position.x, xs), nearestIndex(p.position.y, ys), nearestIndex(p.position.z, zs)};
        if (!lookup.emplace(key, &p).second) return {{}, "none"};
    }
    const auto at = [&lookup](Key key) -> const ViewerParticle& {
        const auto it = lookup.find(key);
        if (it == lookup.end()) throw std::logic_error("particle lattice unexpectedly has a hole");
        return *it->second;
    };

    std::vector<ViewerSurfaceVertex> out;
    const auto quad = [&](Key a, Key b, Key c, Key d, math::Vec3 n) {
        const auto& pa = at(a); const auto& pb = at(b); const auto& pc = at(c); const auto& pd = at(d);
        triangle(out, pa, pb, pc, n);
        triangle(out, pa, pc, pd, n);
    };
    const std::size_t nx = xs.size(), ny = ys.size(), nz = zs.size();
    for (std::size_t y = 0; y + 1U < ny; ++y) for (std::size_t z = 0; z + 1U < nz; ++z) {
        quad({0,y,z},{0,y,z+1U},{0,y+1U,z+1U},{0,y+1U,z},{-1,0,0});
        quad({nx-1U,y,z},{nx-1U,y+1U,z},{nx-1U,y+1U,z+1U},{nx-1U,y,z+1U},{1,0,0});
    }
    for (std::size_t x = 0; x + 1U < nx; ++x) for (std::size_t z = 0; z + 1U < nz; ++z) {
        quad({x,0,z},{x+1U,0,z},{x+1U,0,z+1U},{x,0,z+1U},{0,-1,0});
        quad({x,ny-1U,z},{x,ny-1U,z+1U},{x+1U,ny-1U,z+1U},{x+1U,ny-1U,z},{0,1,0});
    }
    for (std::size_t x = 0; x + 1U < nx; ++x) for (std::size_t y = 0; y + 1U < ny; ++y) {
        quad({x,y,0},{x,y+1U,0},{x+1U,y+1U,0},{x+1U,y,0},{0,0,-1});
        quad({x,y,nz-1U},{x+1U,y,nz-1U},{x+1U,y+1U,nz-1U},{x,y+1U,nz-1U},{0,0,1});
    }
    std::ostringstream kind;
    kind << "physical_lattice_" << nx << 'x' << ny << 'x' << nz;
    return {std::move(out), kind.str()};
}

[[nodiscard]] ViewerBounds computeBounds(const ViewerScene& scene) {
    math::Vec3 mn{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    math::Vec3 mx{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    bool any = false;
    const auto add = [&](math::Vec3 p) {
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
        any = true;
    };
    for (const auto& g : scene.before) add(g.position);
    for (const auto& g : scene.after) add(g.position);
    for (const auto& p : scene.particles) add(p.position);
    if (!any) return {{-0.5,-0.5,-0.5},{0.5,0.5,0.5},{0,0,0},1.0};
    const auto extent = mx - mn;
    return {mn, mx, (mn + mx) * 0.5, std::max(0.05, 0.5 * vulkax::math::length(extent))};
}

[[nodiscard]] std::filesystem::path defaultParticles(const std::filesystem::path& run) {
    return run.parent_path() / "captured-example" / "particles.csv";
}

} // namespace

std::vector<ViewerGaussian> makeViewerGaussians(const gaussian::GaussianCloud& cloud) {
    std::vector<ViewerGaussian> out;
    out.reserve(cloud.size());
    for (const auto& source : cloud.splats) {
        ViewerGaussian g;
        g.position = source.position;
        const auto scales = source.linearScale();
        for (std::size_t i = 0; i < 3U; ++i)
            g.scale[i] = static_cast<float>(std::clamp(std::isfinite(scales[i]) ? scales[i] : 0.02, 1.0e-6, 1.0e4));
        for (std::size_t i = 0; i < 4U; ++i) g.rotation[i] = static_cast<float>(source.rotation[i]);
        for (std::size_t i = 0; i < 3U; ++i) g.color[i] = clamp01(0.5 + sh0 * source.shDC[i]);
        g.opacity = clamp01(source.opacity());
        g.id = source.id;
        out.push_back(g);
    }
    return out;
}

ViewerScene loadStandaloneGaussianScene(const std::filesystem::path& plyPath) {
    ViewerScene scene;
    scene.before = makeViewerGaussians(gaussian::load3dgsPly(plyPath));
    scene.after = scene.before;
    scene.bounds = computeBounds(scene);
    return scene;
}

ViewerScene loadCapturedWorldScene(const std::filesystem::path& runDirectory,
                                   const std::filesystem::path& particlesCsv) {
    const auto beforePath = runDirectory / "appearance" / "before.ply";
    const auto afterPath = runDirectory / "appearance" / "rewritten.ply";
    if (!std::filesystem::exists(beforePath)) throw std::runtime_error("missing appearance/before.ply: " + beforePath.string());
    if (!std::filesystem::exists(afterPath)) throw std::runtime_error("missing appearance/rewritten.ply: " + afterPath.string());

    const auto beforeCloud = gaussian::load3dgsPly(beforePath);
    const auto afterCloud = gaussian::load3dgsPly(afterPath);
    const gaussian::GaussianIndexView afterIndex(afterCloud);
    ViewerScene scene;
    scene.before = makeViewerGaussians(beforeCloud);
    scene.after = makeViewerGaussians(afterCloud);
    for (const auto& before : beforeCloud.splats) {
        const auto index = afterIndex.index(before.id);
        if (index) scene.maxGaussianDisplacement = std::max(scene.maxGaussianDisplacement,
            vulkax::math::length(afterCloud.splats[*index].position - before.position));
    }

    const auto rewrite = loadRewriteIds(runDirectory / "influence" / "selected_rewrite_region.csv");
    scene.rewriteParticleCount = rewrite.size();
    scene.particles = loadParticles(particlesCsv.empty() ? defaultParticles(runDirectory) : particlesCsv, rewrite);
    auto [surface, kind] = buildSurface(scene.particles);
    scene.surfaceTriangles = std::move(surface);
    scene.surfaceKind = std::move(kind);
    scene.bounds = computeBounds(scene);
    return scene;
}

} // namespace vulkax::viewer
