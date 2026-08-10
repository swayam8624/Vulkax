#pragma once

#include "vulkax/field/field.hpp"

#include <cstddef>

namespace vulkax::visualization {

struct IsoSurfaceResult {
    field::TriangleMesh mesh;
    std::size_t activeCells{};
};

[[nodiscard]] IsoSurfaceResult extractIsoSurface(const field::ScalarField& field, double isoValue);

} // namespace vulkax::visualization
