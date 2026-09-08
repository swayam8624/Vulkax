#include "vulkax/viewer/scene.hpp"
#include "vulkax/viewer/visibility.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    assert(out);
    out << text;
}

std::string gaussianPly(double secondX) {
    return "ply\n"
           "format ascii 1.0\n"
           "element vertex 2\n"
           "property float x\n"
           "property float y\n"
           "property float z\n"
           "property float f_dc_0\n"
           "property float f_dc_1\n"
           "property float f_dc_2\n"
           "property float opacity\n"
           "property float scale_0\n"
           "property float scale_1\n"
           "property float scale_2\n"
           "property float rot_0\n"
           "property float rot_1\n"
           "property float rot_2\n"
           "property float rot_3\n"
           "property uint vulkax_id_namespace\n"
           "property uint vulkax_id_local\n"
           "end_header\n"
           "0 0 0 0.4 0.0 -0.2 4 -3 -3 -3 1 0 0 0 7 1\n" +
           std::to_string(secondX) + " 1 1 -0.2 0.2 0.5 4 -2.8 -3.0 -3.2 1 0 0 0 7 2\n";
}

void testVisibility() {
    using vulkax::viewer::ViewerCamera;
    using vulkax::viewer::ViewerGaussian;
    using vulkax::viewer::VisibilitySettings;

    std::vector<ViewerGaussian> cloud(6);
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        cloud[i].id = {3U, static_cast<std::uint32_t>(i + 1U)};
        cloud[i].scale = {0.05F, 0.05F, 0.05F};
        cloud[i].opacity = 0.8F;
    }
    cloud[0].position = {0.0, 0.0, -2.0};
    cloud[1].position = {0.0, 0.0, -5.0};
    cloud[2].position = {0.0, 0.0, -8.0};
    cloud[3].position = {0.0, 0.0, 2.0};
    cloud[4].position = {100.0, 0.0, -3.0};
    cloud[5].position = {0.0, 0.0, -4.0};
    cloud[5].opacity = 0.0001F;

    ViewerCamera camera;
    camera.position = {0.0, 0.0, 0.0};
    camera.target = {0.0, 0.0, -1.0};
    camera.aspect = 1.0;
    camera.nearPlane = 0.1;
    camera.farPlane = 20.0;

    auto visible = vulkax::viewer::selectVisibleGaussians(cloud, camera);
    assert(visible.order.size() == 3U);
    assert(visible.order[0] == 2U);
    assert(visible.order[1] == 1U);
    assert(visible.order[2] == 0U);
    assert(visible.opacityRejected == 1U);
    assert(visible.frustumRejected == 2U);

    cloud[0].scale = {0.30F, 0.30F, 0.30F};
    cloud[1].scale = {0.05F, 0.05F, 0.05F};
    cloud[2].scale = {0.25F, 0.25F, 0.25F};
    VisibilitySettings limited;
    limited.maxSplats = 2U;
    const auto lod = vulkax::viewer::selectVisibleGaussians(cloud, camera, limited);
    assert(lod.order.size() == 2U);
    assert(lod.budgetRejected == 1U);
    assert((lod.order[0] == 2U && lod.order[1] == 0U));
}

void testShPreservation() {
    vulkax::gaussian::GaussianCloud cloud;
    vulkax::gaussian::GaussianSplat splat;
    splat.position = {0.0, 0.0, 0.0};
    splat.logScale = {-3.0, -3.0, -3.0};
    splat.opacityLogit = 4.0;
    splat.shDC = {0.1, 0.2, 0.3};
    splat.shRest.resize(vulkax::viewer::kViewerShRestValues);
    for (std::size_t i = 0; i < splat.shRest.size(); ++i)
        splat.shRest[i] = static_cast<double>(i + 1U) * 0.001;
    splat.id = {9U, 4U};
    cloud.splats.push_back(splat);
    cloud.shRestCoefficientsPerSplat = vulkax::viewer::kViewerShRestValues;

    const auto viewer = vulkax::viewer::makeViewerGaussians(cloud);
    assert(viewer.size() == 1U);
    assert(viewer.front().shCoefficientCount == 16U);
    assert(viewer.front().shDC[0] > 0.099F && viewer.front().shDC[0] < 0.101F);
    assert(viewer.front().shRest[0] > 0.0009F && viewer.front().shRest[0] < 0.0011F);
    assert(viewer.front().shRest[44] > 0.044F && viewer.front().shRest[44] < 0.046F);
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "vulkax-viewer-scene-tests";
    std::filesystem::remove_all(root);
    const auto run = root / "captured-world-run";
    writeText(run / "appearance" / "before.ply", gaussianPly(1.0));
    writeText(run / "appearance" / "rewritten.ply", gaussianPly(1.2));
    writeText(run / "influence" / "selected_rewrite_region.csv",
              "region_id,particle_id\nrewrite,1\nrewrite,8\n");

    std::string particles = "particle_id,rest_x,rest_y,rest_z,mass,rest_volume\n";
    unsigned id = 1;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                particles += std::to_string(id++) + "," + std::to_string(x) + "," + std::to_string(y) + "," +
                             std::to_string(z) + ",1,1\n";
    const auto particlesPath = root / "captured-example" / "particles.csv";
    writeText(particlesPath, particles);

    const auto scene = vulkax::viewer::loadCapturedWorldScene(run, particlesPath);
    assert(scene.before.size() == 2U);
    assert(scene.after.size() == 2U);
    assert(scene.particles.size() == 8U);
    assert(scene.rewriteParticleCount == 2U);
    assert(scene.surfaceKind == "physical_lattice_2x2x2");
    assert(scene.surfaceTriangles.size() == 36U);
    assert(scene.maxGaussianDisplacement > 0.19 && scene.maxGaussianDisplacement < 0.21);
    assert(scene.bounds.radius > 0.0);

    std::size_t marked = 0;
    for (const auto& particle : scene.particles)
        if (particle.inRewriteRegion) ++marked;
    assert(marked == 2U);

    const auto standalone = vulkax::viewer::loadStandaloneGaussianScene(run / "appearance" / "before.ply");
    assert(standalone.before.size() == 2U);
    assert(standalone.after.size() == 2U);
    assert(standalone.particles.empty());
    assert(!standalone.hasSurface());
    assert(standalone.before.front().shCoefficientCount == 1U);

    testVisibility();
    testShPreservation();

    std::filesystem::remove_all(root);
    return 0;
}
