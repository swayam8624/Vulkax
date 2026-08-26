#include "vulkax/backend/backend.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/render/gaussian_scaling.hpp"
#include "vulkax/render/headless.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::optional<vulkax::backend::BackendKind> parseBackend(std::string_view name) {
    using vulkax::backend::BackendKind;
    if (name == "Vulkan") return BackendKind::Vulkan;
    if (name == "Metal") return BackendKind::Metal;
    return std::nullopt;
}

std::size_t parsePositiveSize(std::string_view text, const char* label) {
    if (text.empty()) throw std::invalid_argument(std::string(label) + " cannot be empty");
    std::size_t value = 0U;
    for (const char character : text) {
        if (character < '0' || character > '9')
            throw std::invalid_argument(std::string(label) + " must be a positive integer");
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10U)
            throw std::invalid_argument(std::string(label) + " is too large");
        value = value * 10U + digit;
    }
    if (value == 0U) throw std::invalid_argument(std::string(label) + " must be positive");
    return value;
}

vulkax::backend::BackendKind defaultBackend() {
    using namespace vulkax;
    const auto available = render::availableHeadlessRenderBackends();
    if (std::find(available.begin(), available.end(), backend::BackendKind::Metal) != available.end())
        return backend::BackendKind::Metal;
    if (std::find(available.begin(), available.end(), backend::BackendKind::Vulkan) != available.end())
        return backend::BackendKind::Vulkan;
    throw std::runtime_error("no native Gaussian render backend is available");
}

vulkax::render::GaussianRenderSettings makeSettings(const vulkax::gaussian::GaussianCloud& cloud) {
    using namespace vulkax;
    if (cloud.empty()) throw std::invalid_argument("Gaussian scaling benchmark requires a non-empty cloud");

    math::Vec3 minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    math::Vec3 maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    for (const auto& splat : cloud.splats) {
        minimum.x = std::min(minimum.x, splat.position.x);
        minimum.y = std::min(minimum.y, splat.position.y);
        minimum.z = std::min(minimum.z, splat.position.z);
        maximum.x = std::max(maximum.x, splat.position.x);
        maximum.y = std::max(maximum.y, splat.position.y);
        maximum.z = std::max(maximum.z, splat.position.z);
    }

    const math::Vec3 center = (minimum + maximum) * 0.5;
    const math::Vec3 extent = maximum - minimum;
    const double span = std::max({extent.x, extent.y, extent.z, 1.0e-3});

    render::GaussianRenderSettings settings;
    settings.image.width = 640U;
    settings.image.height = 360U;
    settings.camera.target = center;
    settings.camera.position = {center.x, center.y, center.z + 2.5 * span};
    settings.camera.up = {0.0, 1.0, 0.0};
    settings.camera.verticalFovDegrees = 50.0;
    settings.nearPlane = std::max(1.0e-5, span * 1.0e-5);
    return settings;
}

vulkax::gaussian::GaussianCloud expandCloud(
    const vulkax::gaussian::GaussianCloud& source,
    std::size_t targetCount) {
    if (source.empty()) throw std::invalid_argument("cannot expand an empty Gaussian cloud");
    vulkax::gaussian::GaussianCloud result;
    result.shRestCoefficientsPerSplat = source.shRestCoefficientsPerSplat;
    result.splats.reserve(targetCount);
    for (std::size_t index = 0U; index < targetCount; ++index)
        result.splats.push_back(source.splats[index % source.size()]);
    return result;
}

std::vector<std::size_t> geometricCounts(
    std::size_t minimum,
    std::size_t maximum,
    std::size_t levels) {
    if (minimum > maximum) throw std::invalid_argument("minimum splat count exceeds maximum");
    if (levels == 1U) return {maximum};

    std::vector<std::size_t> counts;
    counts.reserve(levels);
    const double logMinimum = std::log(static_cast<double>(minimum));
    const double logMaximum = std::log(static_cast<double>(maximum));
    for (std::size_t level = 0U; level < levels; ++level) {
        const double alpha = static_cast<double>(level) / static_cast<double>(levels - 1U);
        const auto value = static_cast<std::size_t>(std::llround(std::exp(
            logMinimum + alpha * (logMaximum - logMinimum))));
        if (counts.empty() || value > counts.back()) counts.push_back(value);
    }
    if (counts.back() != maximum) counts.push_back(maximum);
    return counts;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 8) {
            std::cerr << "usage: vulkax_gaussian_scaling <point_cloud.ply> <output.csv> "
                         "[Vulkan|Metal] [min-splats] [max-splats] [levels] [tile-size]\n";
            return 2;
        }

        const auto source = vulkax::gaussian::load3dgsPly(argv[1]);
        vulkax::backend::BackendKind backend = defaultBackend();
        if (argc >= 4) {
            const auto parsed = parseBackend(argv[3]);
            if (!parsed) throw std::invalid_argument("backend must be Vulkan or Metal");
            backend = *parsed;
        }
        const std::size_t minimum = argc >= 5 ? parsePositiveSize(argv[4], "minimum splat count") : 64U;
        const std::size_t maximum = argc >= 6 ? parsePositiveSize(argv[5], "maximum splat count") : 4096U;
        const std::size_t levels = argc >= 7 ? parsePositiveSize(argv[6], "benchmark levels") : 5U;
        const std::size_t tileSizeValue = argc >= 8 ? parsePositiveSize(argv[7], "tile size") : 16U;
        if (tileSizeValue > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            throw std::invalid_argument("tile size exceeds uint32 range");
        const auto tileSize = static_cast<std::uint32_t>(tileSizeValue);
        const auto settings = makeSettings(source);
        const auto counts = geometricCounts(minimum, maximum, levels);

        std::vector<vulkax::render::GaussianScalingSample> samples;
        samples.reserve(counts.size());
        for (const std::size_t count : counts) {
            const auto cloud = expandCloud(source, count);
            auto sample = vulkax::render::benchmarkGaussianExecution(
                backend, cloud, settings, tileSize);
            if (!sample.usedNativeProjection)
                throw std::runtime_error(
                    "scaling benchmark requires native projection evidence; fallback reason: " +
                    sample.fallbackReason);
            samples.push_back(sample);
            std::cout << "splats=" << sample.inputSplats
                      << " visible=" << sample.visibleSplats
                      << " tile_refs=" << sample.tileReferences
                      << " max_splats_per_tile=" << sample.maximumSplatsPerTile
                      << " projection_input_bytes=" << sample.projectionInputBytes
                      << " projection_output_bytes=" << sample.projectionOutputBytes
                      << " tile_reference_bytes=" << sample.tileReferenceBytes
                      << " cpu_projection_ms=" << sample.cpuProjectionMilliseconds
                      << " native_projection_ms=" << sample.nativeProjectionMilliseconds
                      << " scalable_total_ms=" << sample.scalableTotalMilliseconds
                      << " max_channel_difference="
                      << static_cast<unsigned>(sample.imageComparison.maximumChannelDifference)
                      << " rmse=" << sample.imageComparison.rootMeanSquareError
                      << " psnr_db=" << sample.imageComparison.psnrDb
                      << " changed_pixel_fraction=" << sample.imageComparison.changedPixelFraction
                      << " used_native_projection=" << (sample.usedNativeProjection ? 1 : 0)
                      << '\n';
        }

        vulkax::render::writeGaussianScalingCsv(samples, argv[2]);
        std::cout << "Gaussian scaling evidence written to " << argv[2] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vulkax Gaussian scaling error: " << error.what() << '\n';
        return 1;
    }
}
