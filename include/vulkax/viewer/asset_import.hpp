#pragma once

#include "vulkax/viewer/scene.hpp"

#include <array>
#include <cstddef>
#include <filesystem>

namespace vulkax::viewer {

struct ObjImportSettings {
    // Number of presentation splats sampled from triangle area when the mesh has faces.
    std::size_t targetSplats{120000U};
    std::size_t maxSplats{250000U};
    double splatRadiusMultiplier{0.90};
    double normalThicknessRatio{0.22};
    float opacity{0.96F};
    std::array<float, 3> defaultColor{0.58F, 0.70F, 0.94F};
};

// Load a Wavefront OBJ as a viewer-side authored asset. Polygon faces are fan-
// triangulated, retained as an exact presentation surface, and area-sampled into
// surface-aligned anisotropic Gaussian splats. This never creates or mutates
// scientific evidence; before/after are identical authored representations.
[[nodiscard]] ViewerScene loadObjAsGaussianScene(
    const std::filesystem::path& objPath,
    const ObjImportSettings& settings = {});

} // namespace vulkax::viewer
