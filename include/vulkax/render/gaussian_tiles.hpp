#pragma once

#include "vulkax/render/gaussian.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vulkax::render {

struct GaussianTile {
    std::vector<std::size_t> splatIndices;
};

struct GaussianTileGrid {
    std::uint32_t tileSize{};
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::size_t splatReferences{};
    std::size_t maximumSplatsPerTile{};
    std::vector<GaussianTile> tiles;

    [[nodiscard]] const GaussianTile& at(std::uint32_t column, std::uint32_t row) const;
};

[[nodiscard]] GaussianTileGrid buildGaussianTileGrid(
    const GaussianRasterBatch& batch,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t tileSize = 16);

} // namespace vulkax::render
