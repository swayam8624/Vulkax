#include "vulkax/render/gaussian_tiles.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::render {

const GaussianTile& GaussianTileGrid::at(std::uint32_t column, std::uint32_t row) const {
    if (column >= columns || row >= rows)
        throw std::out_of_range("Gaussian tile coordinate out of range");
    return tiles[static_cast<std::size_t>(row) * columns + column];
}

GaussianTileGrid buildGaussianTileGrid(const GaussianRasterBatch& batch,
                                       std::uint32_t width,
                                       std::uint32_t height,
                                       std::uint32_t tileSize) {
    if (width == 0 || height == 0 || tileSize == 0)
        throw std::invalid_argument("Gaussian tile grid dimensions must be positive");
    if (batch.vertices.size() % 6 != 0)
        throw std::invalid_argument("Gaussian raster batch must contain six vertices per splat");

    const std::size_t splatCount = batch.vertices.size() / 6;
    if (batch.stats.visibleSplats != splatCount)
        throw std::invalid_argument("Gaussian raster batch visibility count is inconsistent");

    GaussianTileGrid grid;
    grid.tileSize = tileSize;
    grid.columns = (width + tileSize - 1) / tileSize;
    grid.rows = (height + tileSize - 1) / tileSize;
    grid.tiles.resize(static_cast<std::size_t>(grid.columns) * grid.rows);

    const double widthD = static_cast<double>(width);
    const double heightD = static_cast<double>(height);

    for (std::size_t splat = 0; splat < splatCount; ++splat) {
        float minimumX = batch.vertices[splat * 6].clipX;
        float maximumX = minimumX;
        float minimumY = batch.vertices[splat * 6].clipY;
        float maximumY = minimumY;
        for (std::size_t vertex = 1; vertex < 6; ++vertex) {
            const auto& value = batch.vertices[splat * 6 + vertex];
            minimumX = std::min(minimumX, value.clipX);
            maximumX = std::max(maximumX, value.clipX);
            minimumY = std::min(minimumY, value.clipY);
            maximumY = std::max(maximumY, value.clipY);
        }

        const double pixelMinimumX = (0.5 * static_cast<double>(minimumX) + 0.5) * widthD;
        const double pixelMaximumX = (0.5 * static_cast<double>(maximumX) + 0.5) * widthD;
        const double pixelMinimumY = (0.5 - 0.5 * static_cast<double>(maximumY)) * heightD;
        const double pixelMaximumY = (0.5 - 0.5 * static_cast<double>(minimumY)) * heightD;

        if (pixelMaximumX < 0.0 || pixelMinimumX >= widthD ||
            pixelMaximumY < 0.0 || pixelMinimumY >= heightD)
            continue;

        const auto clampX = [width](double value) {
            return std::clamp(value, 0.0, static_cast<double>(width - 1));
        };
        const auto clampY = [height](double value) {
            return std::clamp(value, 0.0, static_cast<double>(height - 1));
        };

        const std::uint32_t firstColumn =
            static_cast<std::uint32_t>(std::floor(clampX(pixelMinimumX))) / tileSize;
        const std::uint32_t lastColumn =
            static_cast<std::uint32_t>(std::floor(clampX(pixelMaximumX))) / tileSize;
        const std::uint32_t firstRow =
            static_cast<std::uint32_t>(std::floor(clampY(pixelMinimumY))) / tileSize;
        const std::uint32_t lastRow =
            static_cast<std::uint32_t>(std::floor(clampY(pixelMaximumY))) / tileSize;

        for (std::uint32_t row = firstRow; row <= lastRow; ++row) {
            for (std::uint32_t column = firstColumn; column <= lastColumn; ++column) {
                auto& tile = grid.tiles[static_cast<std::size_t>(row) * grid.columns + column];
                tile.splatIndices.push_back(splat);
                ++grid.splatReferences;
                grid.maximumSplatsPerTile =
                    std::max(grid.maximumSplatsPerTile, tile.splatIndices.size());
            }
        }
    }

    return grid;
}

} // namespace vulkax::render
