#include "vulkax/render/gaussian_scaling.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

vulkax::render::GaussianProjectedSplat projected(
    float depth,
    float firstColumn,
    float lastColumn,
    float firstRow,
    float lastRow) {
    vulkax::render::GaussianProjectedSplat value;
    value.minorDepth[2] = depth;
    value.minorDepth[3] = 0.9F;
    value.colorCull[3] = 0.0F;
    value.tileBounds = {firstColumn, lastColumn, firstRow, lastRow};
    return value;
}

void testTileReferenceOrdering() {
    using namespace vulkax;
    render::GaussianNativeProjectionResult projection;
    projection.tileSize = 16U;
    projection.tileColumns = 2U;
    projection.tileRows = 2U;
    projection.projected = {
        projected(5.0F, 0.0F, 1.0F, 0.0F, 0.0F),
        projected(3.0F, 0.0F, 0.0F, 0.0F, 1.0F),
        projected(1.0F, 0.0F, 1.0F, 0.0F, 1.0F),
    };
    projection.stats.visibleSplats = projection.projected.size();
    projection.splatReferences = 8U;
    projection.maximumSplatsPerTile = 3U;

    const auto stream = render::buildGaussianTileReferenceStream(projection);
    assert(stream.offsets.size() == 5U);
    assert(stream.offsets[0] == 0U);
    assert(stream.offsets[1] == 3U); // tile (0,0): splats 0,1,2
    assert(stream.offsets[2] == 5U); // tile (1,0): splats 0,2
    assert(stream.offsets[3] == 7U); // tile (0,1): splats 1,2
    assert(stream.offsets[4] == 8U); // tile (1,1): splat 2
    assert(stream.splatIndices == std::vector<std::size_t>({0U, 1U, 2U, 0U, 2U, 1U, 2U, 2U}));
    assert(stream.maximumSplatsPerTile == 3U);

    for (std::size_t tile = 0U; tile + 1U < stream.offsets.size(); ++tile) {
        float previousDepth = std::numeric_limits<float>::infinity();
        for (std::size_t offset = stream.offsets[tile]; offset < stream.offsets[tile + 1U]; ++offset) {
            const float depth = projection.projected[stream.splatIndices[offset]].minorDepth[2];
            assert(depth <= previousDepth);
            previousDepth = depth;
        }
    }
}

void testTileReferenceValidation() {
    using namespace vulkax;
    render::GaussianNativeProjectionResult projection;
    projection.tileSize = 16U;
    projection.tileColumns = 1U;
    projection.tileRows = 1U;
    projection.projected = {projected(1.0F, 0.0F, 0.0F, 0.0F, 0.0F)};
    projection.stats.visibleSplats = 1U;
    projection.splatReferences = 2U; // deliberately inconsistent
    projection.maximumSplatsPerTile = 1U;

    bool rejected = false;
    try {
        (void)render::buildGaussianTileReferenceStream(projection);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
}

void testScalingCsv() {
    using namespace vulkax;
    render::GaussianScalingSample sample;
    sample.inputSplats = 128U;
    sample.visibleSplats = 96U;
    sample.tileReferences = 512U;
    sample.maximumSplatsPerTile = 17U;
    sample.projectionInputBytes = 8192U;
    sample.projectionOutputBytes = 8192U;
    sample.tileReferenceBytes = 4608U;
    sample.cpuProjectionMilliseconds = 1.25;
    sample.nativeProjectionMilliseconds = 0.25;
    sample.scalableTotalMilliseconds = 0.75;
    sample.imageComparison.pixelCount = 64U;
    sample.imageComparison.maximumChannelDifference = 2U;
    sample.imageComparison.rootMeanSquareError = 0.3;
    sample.imageComparison.psnrDb = 58.0;
    sample.imageComparison.changedPixelFraction = 0.02;
    sample.usedNativeProjection = true;

    const auto path = std::filesystem::temp_directory_path() / "vulkax_gaussian_scaling_evidence.csv";
    render::writeGaussianScalingCsv({sample}, path);
    {
        std::ifstream stream(path);
        assert(stream.good());
        const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        assert(content.find("input_splats,visible_splats") != std::string::npos);
        assert(content.find("128,96,512,17") != std::string::npos);
        assert(content.find(",1,\"\"") != std::string::npos);
    }
    assert(std::filesystem::remove(path));
}

} // namespace

int main() {
    testTileReferenceOrdering();
    testTileReferenceValidation();
    testScalingCsv();
    return 0;
}
