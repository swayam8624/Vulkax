#include "vulkax/render/gaussian_scaling.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

namespace vulkax::render {
namespace {

[[nodiscard]] std::uint32_t decodeTileCoordinate(
    float encoded,
    std::uint32_t limit,
    const char* label) {
    if (!std::isfinite(encoded))
        throw std::runtime_error(std::string("Gaussian tile stream received non-finite ") + label);
    const long rounded = std::lround(encoded);
    if (rounded < 0L || static_cast<unsigned long>(rounded) >= static_cast<unsigned long>(limit) ||
        std::abs(encoded - static_cast<float>(rounded)) > 1.0e-4F)
        throw std::runtime_error(std::string("Gaussian tile stream received invalid ") + label);
    return static_cast<std::uint32_t>(rounded);
}

[[nodiscard]] bool isUnavailableProjectionError(const std::runtime_error& error) {
    const std::string message(error.what());
    return message.find("Gaussian projection was not compiled into this build") != std::string::npos ||
           message.find("OpenGL Gaussian projection is not implemented") != std::string::npos;
}

[[nodiscard]] std::string quoteCsv(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (const char character : value) {
        if (character == '"') escaped.push_back('"');
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

} // namespace

GaussianTileReferenceStream buildGaussianTileReferenceStream(
    const GaussianNativeProjectionResult& projection) {
    if (projection.tileSize == 0U || projection.tileColumns == 0U || projection.tileRows == 0U)
        throw std::invalid_argument("Gaussian tile reference stream requires a valid tile grid");
    if (projection.projected.size() != projection.stats.visibleSplats)
        throw std::invalid_argument("Gaussian tile reference stream visible count is inconsistent");

    const std::size_t tileCount =
        static_cast<std::size_t>(projection.tileColumns) * projection.tileRows;
    std::vector<std::size_t> counts(tileCount, 0U);

    for (const auto& projected : projection.projected) {
        const std::uint32_t firstColumn = decodeTileCoordinate(
            projected.tileBounds[0], projection.tileColumns, "first tile column");
        const std::uint32_t lastColumn = decodeTileCoordinate(
            projected.tileBounds[1], projection.tileColumns, "last tile column");
        const std::uint32_t firstRow = decodeTileCoordinate(
            projected.tileBounds[2], projection.tileRows, "first tile row");
        const std::uint32_t lastRow = decodeTileCoordinate(
            projected.tileBounds[3], projection.tileRows, "last tile row");
        if (firstColumn > lastColumn || firstRow > lastRow)
            throw std::runtime_error("Gaussian tile reference stream received an inverted tile range");

        for (std::uint32_t row = firstRow; row <= lastRow; ++row) {
            for (std::uint32_t column = firstColumn; column <= lastColumn; ++column) {
                ++counts[static_cast<std::size_t>(row) * projection.tileColumns + column];
            }
        }
    }

    GaussianTileReferenceStream stream;
    stream.tileSize = projection.tileSize;
    stream.columns = projection.tileColumns;
    stream.rows = projection.tileRows;
    stream.offsets.resize(tileCount + 1U, 0U);
    for (std::size_t tile = 0U; tile < tileCount; ++tile) {
        stream.offsets[tile + 1U] = stream.offsets[tile] + counts[tile];
        stream.maximumSplatsPerTile = std::max(stream.maximumSplatsPerTile, counts[tile]);
    }
    stream.splatIndices.resize(stream.offsets.back());
    std::vector<std::size_t> cursor = stream.offsets;

    // projection.projected is stable far-to-near. Filling every tile in this
    // order preserves deterministic compositing order without a second sort.
    for (std::size_t splat = 0U; splat < projection.projected.size(); ++splat) {
        const auto& projected = projection.projected[splat];
        const std::uint32_t firstColumn = decodeTileCoordinate(
            projected.tileBounds[0], projection.tileColumns, "first tile column");
        const std::uint32_t lastColumn = decodeTileCoordinate(
            projected.tileBounds[1], projection.tileColumns, "last tile column");
        const std::uint32_t firstRow = decodeTileCoordinate(
            projected.tileBounds[2], projection.tileRows, "first tile row");
        const std::uint32_t lastRow = decodeTileCoordinate(
            projected.tileBounds[3], projection.tileRows, "last tile row");
        for (std::uint32_t row = firstRow; row <= lastRow; ++row) {
            for (std::uint32_t column = firstColumn; column <= lastColumn; ++column) {
                const std::size_t tile = static_cast<std::size_t>(row) * projection.tileColumns + column;
                stream.splatIndices[cursor[tile]++] = splat;
            }
        }
    }

    if (stream.splatIndices.size() != projection.splatReferences)
        throw std::runtime_error("Gaussian tile reference stream count disagrees with projection evidence");
    if (stream.maximumSplatsPerTile != projection.maximumSplatsPerTile)
        throw std::runtime_error("Gaussian tile reference stream occupancy disagrees with projection evidence");

    return stream;
}

GaussianScalableRenderOutcome renderGaussianCloudScalableWithFallback(
    backend::BackendKind backend,
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings,
    std::uint32_t tileSize) {
    try {
        GaussianScalableRenderOutcome outcome;
        outcome.result = renderGaussianCloudScalableHeadless(backend, cloud, settings, tileSize);
        outcome.usedNativeProjection = true;
        return outcome;
    } catch (const std::runtime_error& error) {
        if (!isUnavailableProjectionError(error)) throw;
        GaussianScalableRenderOutcome outcome;
        outcome.result = renderGaussianCloudHeadless(backend, cloud, settings);
        outcome.usedNativeProjection = false;
        outcome.fallbackReason = error.what();
        return outcome;
    }
}

GaussianScalingSample benchmarkGaussianExecution(
    backend::BackendKind backend,
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings,
    std::uint32_t tileSize) {
    using Clock = std::chrono::steady_clock;

    const auto cpuStart = Clock::now();
    const auto referenceBatch = buildGaussianRasterBatch(cloud, settings);
    const auto cpuEnd = Clock::now();
    const auto referenceImage = renderGaussianRasterBatchHeadless(
        backend, referenceBatch, settings.image);

    GaussianScalingSample sample;
    sample.inputSplats = cloud.size();
    sample.visibleSplats = referenceBatch.stats.visibleSplats;
    sample.cpuProjectionMilliseconds =
        std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count();

    const auto scalableStart = Clock::now();
    try {
        const auto projection = projectGaussianCloudNative(backend, cloud, settings, tileSize);
        const auto tileStream = buildGaussianTileReferenceStream(projection);
        const auto acceleratedBatch = buildGaussianRasterBatchFromProjection(projection, settings);
        const auto acceleratedImage = renderGaussianRasterBatchHeadless(
            backend, acceleratedBatch, settings.image);
        const auto scalableEnd = Clock::now();

        sample.usedNativeProjection = true;
        sample.visibleSplats = projection.stats.visibleSplats;
        sample.tileReferences = tileStream.splatIndices.size();
        sample.maximumSplatsPerTile = tileStream.maximumSplatsPerTile;
        sample.projectionInputBytes = projection.inputBytes;
        sample.projectionOutputBytes = projection.outputBytes;
        sample.tileReferenceBytes =
            tileStream.offsets.size() * sizeof(std::size_t) +
            tileStream.splatIndices.size() * sizeof(std::size_t);
        sample.nativeProjectionMilliseconds = projection.projectionMilliseconds;
        sample.scalableTotalMilliseconds =
            std::chrono::duration<double, std::milli>(scalableEnd - scalableStart).count();
        sample.imageComparison = compareImages(referenceImage.image, acceleratedImage.image);
        return sample;
    } catch (const std::runtime_error& error) {
        if (!isUnavailableProjectionError(error)) throw;
        const auto fallbackImage = renderGaussianCloudHeadless(backend, cloud, settings);
        const auto scalableEnd = Clock::now();
        sample.usedNativeProjection = false;
        sample.fallbackReason = error.what();
        sample.scalableTotalMilliseconds =
            std::chrono::duration<double, std::milli>(scalableEnd - scalableStart).count();
        sample.imageComparison = compareImages(referenceImage.image, fallbackImage.image);
        return sample;
    }
}

void writeGaussianScalingCsv(
    const std::vector<GaussianScalingSample>& samples,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open Gaussian scaling CSV: " + path.string());
    stream << "input_splats,visible_splats,tile_references,max_splats_per_tile,"
              "projection_input_bytes,projection_output_bytes,tile_reference_bytes,"
              "cpu_projection_ms,native_projection_ms,scalable_total_ms,"
              "max_channel_difference,rmse,psnr_db,changed_pixel_fraction,"
              "used_native_projection,fallback_reason\n";
    stream << std::setprecision(17);
    for (const auto& sample : samples) {
        stream << sample.inputSplats << ','
               << sample.visibleSplats << ','
               << sample.tileReferences << ','
               << sample.maximumSplatsPerTile << ','
               << sample.projectionInputBytes << ','
               << sample.projectionOutputBytes << ','
               << sample.tileReferenceBytes << ','
               << sample.cpuProjectionMilliseconds << ','
               << sample.nativeProjectionMilliseconds << ','
               << sample.scalableTotalMilliseconds << ','
               << static_cast<unsigned>(sample.imageComparison.maximumChannelDifference) << ','
               << sample.imageComparison.rootMeanSquareError << ','
               << sample.imageComparison.psnrDb << ','
               << sample.imageComparison.changedPixelFraction << ','
               << (sample.usedNativeProjection ? 1 : 0) << ','
               << quoteCsv(sample.fallbackReason) << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing Gaussian scaling CSV: " + path.string());
}

} // namespace vulkax::render
