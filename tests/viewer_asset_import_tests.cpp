#include "vulkax/viewer/asset_import.hpp"
#include "vulkax/viewer/image_splat_card.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

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
    using vulkax::viewer::ImageSplatCardSettings;
    using vulkax::viewer::ObjImportSettings;
    using vulkax::viewer::loadObjAsGaussianScene;
    using vulkax::viewer::makeImageSplatCard;

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

    // RGBA images become explicitly flat 2.5D cards; transparent pixels are skipped.
    std::vector<std::uint8_t> rgba(4U * 2U * 4U, 255U);
    for (std::size_t pixel = 0U; pixel < 8U; ++pixel) {
        rgba[pixel * 4U + 0U] = static_cast<std::uint8_t>(pixel * 20U);
        rgba[pixel * 4U + 1U] = static_cast<std::uint8_t>(255U - pixel * 20U);
        rgba[pixel * 4U + 2U] = 64U;
        rgba[pixel * 4U + 3U] = 255U;
    }
    rgba[3U] = 0U; // top-left pixel transparent.
    ImageSplatCardSettings imageSettings;
    imageSettings.maxSplats = 8U;
    imageSettings.worldHeight = 2.0;
    const auto imageScene = makeImageSplatCard(4U, 2U, rgba, imageSettings);
    assert(imageScene.before.size() == 7U);
    assert(imageScene.after.size() == 7U);
    assert(imageScene.surfaceKind == "image_splat_card_2_5d");
    assert(!imageScene.hasSurface());
    assert(std::abs(imageScene.bounds.maximum.x - 2.0) < 1.0e-9);
    assert(std::abs(imageScene.bounds.minimum.x + 2.0) < 1.0e-9);
    assert(std::abs(imageScene.bounds.maximum.y - 1.0) < 1.0e-9);
    assert(imageScene.before.front().position.y > 0.0); // input row zero maps upward.
    for (const auto& splat : imageScene.before) {
        assert(std::abs(splat.position.z) < 1.0e-12);
        assert(splat.scale[0] > splat.scale[2]);
        assert(splat.opacity > 0.99F);
        assert(splat.shCoefficientCount == 1U);
    }

    // A strict budget selects a deterministic pixel stride instead of allocating every pixel.
    std::fill(rgba.begin(), rgba.end(), 255U);
    imageSettings.maxSplats = 2U;
    const auto budgetedImage = makeImageSplatCard(4U, 2U, rgba, imageSettings);
    assert(budgetedImage.before.size() <= 2U);
    assert(!budgetedImage.before.empty());

    fs::remove_all(root);
    return 0;
}
