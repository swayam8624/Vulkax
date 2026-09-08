#include "vulkax/viewer/asset_import.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    assert(output);
    output << text;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using vulkax::viewer::ObjImportSettings;
    using vulkax::viewer::loadObjAsGaussianScene;

    const auto root = fs::temp_directory_path() / "vulkax-viewer-asset-import-tests";
    fs::remove_all(root);
    fs::create_directories(root);

    const auto quad = root / "colored_quad.obj";
    writeText(quad,
              "# vertex colors + polygon triangulation\n"
              "v -1 -1 0 1 0 0\n"
              "v  1 -1 0 0 1 0\n"
              "v  1  1 0 0 0 1\n"
              "v -1  1 0 1 1 1\n"
              "f 1 2 3 4\n");

    ObjImportSettings settings;
    settings.targetSplats = 64U;
    settings.maxSplats = 64U;
    const auto scene = loadObjAsGaussianScene(quad, settings);
    assert(scene.before.size() == 64U);
    assert(scene.after.size() == 64U);
    assert(scene.surfaceKind == "obj_mesh");
    assert(scene.surfaceTriangles.size() == 6U);
    assert(scene.bounds.radius > 1.4 && scene.bounds.radius < 1.5);
    for (std::size_t i = 0U; i < scene.before.size(); ++i) {
        const auto& splat = scene.before[i];
        assert(std::abs(splat.position.z) < 1.0e-9);
        assert(splat.scale[0] > splat.scale[2]);
        assert(splat.scale[1] > splat.scale[2]);
        assert(splat.id.local == i + 1U);
        assert(splat.shCoefficientCount == 1U);
        assert(splat.opacity > 0.9F);
        for (const auto channel : splat.color) assert(channel >= 0.0F && channel <= 1.0F);
    }

    // Negative OBJ indices must resolve relative to the currently defined vertices.
    const auto negative = root / "negative_indices.obj";
    writeText(negative,
              "v 0 0 0\n"
              "v 1 0 0\n"
              "v 0 1 0\n"
              "f -3 -2 -1\n");
    settings.targetSplats = 9U;
    settings.maxSplats = 9U;
    const auto negativeScene = loadObjAsGaussianScene(negative, settings);
    assert(negativeScene.before.size() == 9U);
    assert(negativeScene.surfaceTriangles.size() == 3U);

    // Vertex-only OBJ files remain useful as point-splat assets.
    const auto points = root / "points.obj";
    writeText(points,
              "v 0 0 0\n"
              "v 1 0 0\n"
              "v 0 1 0\n");
    settings.maxSplats = 10U;
    const auto pointScene = loadObjAsGaussianScene(points, settings);
    assert(pointScene.before.size() == 3U);
    assert(pointScene.after.size() == 3U);
    assert(pointScene.surfaceKind == "obj_points");
    assert(!pointScene.hasSurface());

    bool threw = false;
    const auto invalid = root / "invalid.obj";
    writeText(invalid, "v 0 0 0\nf 1 2 3\n");
    try {
        (void)loadObjAsGaussianScene(invalid, settings);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    fs::remove_all(root);
    return 0;
}
