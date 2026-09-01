#include "vulkax/render/gaussian_projection.hpp"
#include "vulkax/render/gaussian_scheduler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef VULKAX_HAS_VULKAN_GAUSSIAN_PROJECTION
#define VULKAX_HAS_VULKAN_GAUSSIAN_PROJECTION 0
#endif
#ifndef VULKAX_HAS_METAL_GAUSSIAN_PROJECTION
#define VULKAX_HAS_METAL_GAUSSIAN_PROJECTION 0
#endif

namespace vulkax::render {

#if VULKAX_HAS_VULKAN_GAUSSIAN_PROJECTION
std::pair<std::vector<GaussianProjectedSplat>, double> projectGaussianSplatsVulkan(
    const std::vector<GaussianProjectionInput>& inputs,
    const GaussianProjectionParameters& parameters);
#endif
#if VULKAX_HAS_METAL_GAUSSIAN_PROJECTION
std::pair<std::vector<GaussianProjectedSplat>, double> projectGaussianSplatsMetal(
    const std::vector<GaussianProjectionInput>& inputs,
    const GaussianProjectionParameters& parameters);
#endif

namespace {

[[nodiscard]] double shCoefficient(const gaussian::GaussianSplat& splat,
                                   std::size_t channel,
                                   std::size_t coefficient) {
    if (coefficient == 0U) return splat.shDC[channel];
    const std::size_t index = (coefficient - 1U) * 3U + channel;
    return index < splat.shRest.size() ? splat.shRest[index] : 0.0;
}

[[nodiscard]] std::array<double, 3> evaluateSphericalHarmonics(
    const gaussian::GaussianSplat& splat,
    math::Vec3 direction,
    bool evaluateHigherOrders) {
    constexpr double c0 = 0.28209479177387814;
    constexpr double c1 = 0.4886025119029199;
    constexpr std::array<double, 5> c2{
        1.0925484305920792, -1.0925484305920792, 0.31539156525252005,
        -1.0925484305920792, 0.5462742152960396,
    };
    constexpr std::array<double, 7> c3{
        -0.5900435899266435, 2.890611442640554, -0.4570457994644658,
        0.3731763325901154, -0.4570457994644658, 1.445305721320277,
        -0.5900435899266435,
    };

    direction = math::normalized(direction);
    const double x = direction.x;
    const double y = direction.y;
    const double z = direction.z;
    const double xx = x * x;
    const double yy = y * y;
    const double zz = z * z;
    const double xy = x * y;
    const double yz = y * z;
    const double xz = x * z;
    const std::size_t coefficientCount = 1U + splat.shRest.size() / 3U;

    std::array<double, 3> color{};
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
        double value = c0 * shCoefficient(splat, channel, 0U);
        if (evaluateHigherOrders && coefficientCount >= 4U) {
            value += -c1 * y * shCoefficient(splat, channel, 1U) +
                     c1 * z * shCoefficient(splat, channel, 2U) -
                     c1 * x * shCoefficient(splat, channel, 3U);
        }
        if (evaluateHigherOrders && coefficientCount >= 9U) {
            value += c2[0] * xy * shCoefficient(splat, channel, 4U) +
                     c2[1] * yz * shCoefficient(splat, channel, 5U) +
                     c2[2] * (2.0 * zz - xx - yy) * shCoefficient(splat, channel, 6U) +
                     c2[3] * xz * shCoefficient(splat, channel, 7U) +
                     c2[4] * (xx - yy) * shCoefficient(splat, channel, 8U);
        }
        if (evaluateHigherOrders && coefficientCount >= 16U) {
            value += c3[0] * y * (3.0 * xx - yy) * shCoefficient(splat, channel, 9U) +
                     c3[1] * xy * z * shCoefficient(splat, channel, 10U) +
                     c3[2] * y * (4.0 * zz - xx - yy) * shCoefficient(splat, channel, 11U) +
                     c3[3] * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) *
                         shCoefficient(splat, channel, 12U) +
                     c3[4] * x * (4.0 * zz - xx - yy) * shCoefficient(splat, channel, 13U) +
                     c3[5] * z * (xx - yy) * shCoefficient(splat, channel, 14U) +
                     c3[6] * x * (xx - 3.0 * yy) * shCoefficient(splat, channel, 15U);
        }
        color[channel] = std::clamp(value + 0.5, 0.0, 1.0);
    }
    return color;
}

void validateSettings(const GaussianRenderSettings& settings, std::uint32_t tileSize) {
    if (settings.image.width == 0U || settings.image.height == 0U || tileSize == 0U)
        throw std::invalid_argument("Gaussian native projection dimensions must be positive");
    if (!(settings.camera.verticalFovDegrees > 0.0 && settings.camera.verticalFovDegrees < 179.0))
        throw std::invalid_argument("Gaussian camera vertical FOV must lie in (0, 179) degrees");
    if (!(settings.nearPlane > 0.0) || !(settings.sigmaCutoff > 0.0) ||
        !(settings.minimumSigmaPixels > 0.0) ||
        !(settings.maximumSigmaPixels >= settings.minimumSigmaPixels))
        throw std::invalid_argument("invalid Gaussian native projection settings");
}

[[nodiscard]] GaussianProjectionCullReason decodeCullReason(float encoded) {
    if (!std::isfinite(encoded))
        throw std::runtime_error("native Gaussian projection returned a non-finite cull reason");
    const long rounded = std::lround(encoded);
    if (rounded < 0L || rounded > 3L || std::abs(encoded - static_cast<float>(rounded)) > 1.0e-4F)
        throw std::runtime_error("native Gaussian projection returned an invalid cull reason");
    return static_cast<GaussianProjectionCullReason>(static_cast<std::uint32_t>(rounded));
}

[[nodiscard]] std::uint32_t decodeTile(float encoded,
                                       std::uint32_t limit,
                                       const char* label) {
    if (!std::isfinite(encoded))
        throw std::runtime_error(std::string("native Gaussian projection returned non-finite ") + label);
    const long rounded = std::lround(encoded);
    if (rounded < 0L || static_cast<unsigned long>(rounded) >= static_cast<unsigned long>(limit) ||
        std::abs(encoded - static_cast<float>(rounded)) > 1.0e-4F)
        throw std::runtime_error(std::string("native Gaussian projection returned invalid ") + label);
    return static_cast<std::uint32_t>(rounded);
}

[[nodiscard]] std::size_t referenceCapacityForProjectedSplat(
    const GaussianProjectedSplat& projected,
    std::uint32_t tileColumns,
    std::uint32_t tileRows) {
    const std::uint32_t firstColumn = decodeTile(projected.tileBounds[0], tileColumns, "first tile column");
    const std::uint32_t lastColumn = decodeTile(projected.tileBounds[1], tileColumns, "last tile column");
    const std::uint32_t firstRow = decodeTile(projected.tileBounds[2], tileRows, "first tile row");
    const std::uint32_t lastRow = decodeTile(projected.tileBounds[3], tileRows, "last tile row");
    if (firstColumn > lastColumn || firstRow > lastRow)
        throw std::runtime_error("native Gaussian projection returned an inverted tile range");
    const std::size_t columns = static_cast<std::size_t>(lastColumn - firstColumn) + 1U;
    const std::size_t rows = static_cast<std::size_t>(lastRow - firstRow) + 1U;
    if (rows > std::numeric_limits<std::size_t>::max() / columns)
        throw std::overflow_error("native Gaussian tile-reference capacity overflow");
    return columns * rows;
}

} // namespace

std::vector<GaussianProjectionInput> prepareGaussianProjectionInputs(
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings) {
    validateSettings(settings, 16U);
    std::vector<GaussianProjectionInput> inputs;
    inputs.reserve(cloud.size());
    for (const auto& splat : cloud.splats) {
        const auto scale = splat.linearScale();
        const auto color = evaluateSphericalHarmonics(
            splat, splat.position - settings.camera.position, settings.evaluateSphericalHarmonics);
        const double opacity = splat.opacity();

        GaussianProjectionInput input;
        input.positionOpacity = {
            static_cast<float>(splat.position.x),
            static_cast<float>(splat.position.y),
            static_cast<float>(splat.position.z),
            static_cast<float>(opacity),
        };
        input.scale = {
            static_cast<float>(scale[0]),
            static_cast<float>(scale[1]),
            static_cast<float>(scale[2]),
            0.0F,
        };
        input.rotation = {
            static_cast<float>(splat.rotation[0]),
            static_cast<float>(splat.rotation[1]),
            static_cast<float>(splat.rotation[2]),
            static_cast<float>(splat.rotation[3]),
        };
        input.color = {
            static_cast<float>(color[0]),
            static_cast<float>(color[1]),
            static_cast<float>(color[2]),
            0.0F,
        };
        inputs.push_back(input);
    }
    return inputs;
}

GaussianProjectionParameters makeGaussianProjectionParameters(
    const GaussianRenderSettings& settings,
    std::uint32_t tileSize) {
    validateSettings(settings, tileSize);
    const math::Vec3 forward = math::normalized(settings.camera.target - settings.camera.position);
    const math::Vec3 right = math::normalized(math::cross(forward, settings.camera.up));
    if (math::length(right) <= 1.0e-12)
        throw std::invalid_argument("Gaussian camera up vector is parallel to its viewing direction");
    const math::Vec3 cameraUp = math::normalized(math::cross(right, forward));

    constexpr double pi = 3.1415926535897932384626433832795;
    const double fovRadians = settings.camera.verticalFovDegrees * pi / 180.0;
    const double focalPixels =
        0.5 * static_cast<double>(settings.image.height) / std::tan(0.5 * fovRadians);

    GaussianProjectionParameters parameters;
    parameters.cameraPosition = {
        static_cast<float>(settings.camera.position.x),
        static_cast<float>(settings.camera.position.y),
        static_cast<float>(settings.camera.position.z), 0.0F,
    };
    parameters.right = {
        static_cast<float>(right.x), static_cast<float>(right.y), static_cast<float>(right.z), 0.0F,
    };
    parameters.up = {
        static_cast<float>(cameraUp.x), static_cast<float>(cameraUp.y), static_cast<float>(cameraUp.z), 0.0F,
    };
    parameters.forward = {
        static_cast<float>(forward.x), static_cast<float>(forward.y), static_cast<float>(forward.z), 0.0F,
    };
    parameters.imageFocalNear = {
        static_cast<float>(settings.image.width),
        static_cast<float>(settings.image.height),
        static_cast<float>(focalPixels),
        static_cast<float>(settings.nearPlane),
    };
    parameters.sigmaTile = {
        static_cast<float>(settings.sigmaCutoff),
        static_cast<float>(settings.minimumSigmaPixels),
        static_cast<float>(settings.maximumSigmaPixels),
        static_cast<float>(tileSize),
    };
    return parameters;
}

GaussianNativeProjectionResult projectGaussianCloudNative(
    backend::BackendKind backend,
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings,
    std::uint32_t tileSize) {
    const auto inputs = prepareGaussianProjectionInputs(cloud, settings);
    const auto parameters = makeGaussianProjectionParameters(settings, tileSize);

    std::pair<std::vector<GaussianProjectedSplat>, double> raw;
    switch (backend) {
        case backend::BackendKind::Vulkan:
#if VULKAX_HAS_VULKAN_GAUSSIAN_PROJECTION
            raw = projectGaussianSplatsVulkan(inputs, parameters);
            break;
#else
            throw std::runtime_error("Vulkan Gaussian projection was not compiled into this build");
#endif
        case backend::BackendKind::Metal:
#if VULKAX_HAS_METAL_GAUSSIAN_PROJECTION
            raw = projectGaussianSplatsMetal(inputs, parameters);
            break;
#else
            throw std::runtime_error("Metal Gaussian projection was not compiled into this build");
#endif
        case backend::BackendKind::OpenGL:
            throw std::runtime_error("OpenGL Gaussian projection is not implemented");
    }

    if (raw.first.size() != inputs.size())
        throw std::runtime_error("native Gaussian projection returned the wrong record count");

    GaussianNativeProjectionResult result;
    result.stats.inputSplats = cloud.size();
    result.tileSize = tileSize;
    result.tileColumns = (settings.image.width + tileSize - 1U) / tileSize;
    result.tileRows = (settings.image.height + tileSize - 1U) / tileSize;
    result.projectionMilliseconds = raw.second;
    result.inputBytes = inputs.size() * sizeof(GaussianProjectionInput);
    result.outputBytes = raw.first.size() * sizeof(GaussianProjectedSplat);
    result.projected.reserve(raw.first.size());

    for (const auto& projected : raw.first) {
        switch (decodeCullReason(projected.colorCull[3])) {
            case GaussianProjectionCullReason::Visible:
                if (!std::isfinite(projected.minorDepth[2]) || !(projected.minorDepth[2] > 0.0F))
                    throw std::runtime_error("visible native Gaussian projection has invalid depth");
                result.projected.push_back(projected);
                break;
            case GaussianProjectionCullReason::Opacity:
                ++result.stats.culledOpacity;
                break;
            case GaussianProjectionCullReason::BehindCamera:
                ++result.stats.culledBehindCamera;
                break;
            case GaussianProjectionCullReason::OutsideImage:
                ++result.stats.culledOutsideImage;
                break;
        }
    }
    result.stats.visibleSplats = result.projected.size();

    // The host computes only the scalar allocation bound needed to size the
    // device reference buffer. It no longer constructs per-tile occupancy,
    // prefix offsets, or ordering.
    for (const auto& projected : result.projected) {
        const std::size_t references = referenceCapacityForProjectedSplat(
            projected, result.tileColumns, result.tileRows);
        if (references > std::numeric_limits<std::size_t>::max() - result.splatReferences)
            throw std::overflow_error("native Gaussian total tile-reference capacity overflow");
        result.splatReferences += references;
    }

    const auto nativeSchedule = scheduleGaussianProjectionNative(backend, result);
    if (nativeSchedule.schedule.splatReferences != result.splatReferences)
        throw std::runtime_error("native Gaussian scheduler reference count disagrees with allocation bound");
    result.maximumSplatsPerTile = nativeSchedule.schedule.maximumSplatsPerTile;
    result.schedulingMilliseconds = nativeSchedule.schedulingMilliseconds;
    result.schedulerInputBytes = nativeSchedule.inputBytes;
    result.schedulerOutputBytes = nativeSchedule.outputBytes;
    result.schedulerWorkspaceBytes = nativeSchedule.workspaceBytes;

    std::vector<GaussianProjectedSplat> ordered;
    ordered.reserve(result.projected.size());
    for (const std::uint32_t index : nativeSchedule.depthOrder)
        ordered.push_back(result.projected.at(index));
    result.projected = std::move(ordered);
    return result;
}

GaussianRasterBatch buildGaussianRasterBatchFromProjection(
    const GaussianNativeProjectionResult& projection,
    const GaussianRenderSettings& settings) {
    validateSettings(settings, projection.tileSize);
    if (projection.projected.size() != projection.stats.visibleSplats)
        throw std::invalid_argument("native Gaussian projection visible count is inconsistent");

    constexpr std::array<std::array<float, 2>, 6> corners{{
        {-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F},
        {-1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F},
    }};

    GaussianRasterBatch batch;
    batch.stats = projection.stats;
    batch.vertices.reserve(projection.projected.size() * 6U);
    for (const auto& projected : projection.projected) {
        for (const auto& corner : corners) {
            const float localX = corner[0];
            const float localY = corner[1];
            batch.vertices.push_back({
                projected.centerMajor[0] + localX * projected.centerMajor[2] +
                    localY * projected.minorDepth[0],
                projected.centerMajor[1] + localX * projected.centerMajor[3] +
                    localY * projected.minorDepth[1],
                0.0F,
                localX * static_cast<float>(settings.sigmaCutoff),
                localY * static_cast<float>(settings.sigmaCutoff),
                projected.colorCull[0], projected.colorCull[1], projected.colorCull[2],
                std::clamp(projected.minorDepth[3], 0.0F, 0.999F),
            });
        }
    }
    return batch;
}

GaussianRenderResult renderGaussianCloudScalableHeadless(
    backend::BackendKind backend,
    const gaussian::GaussianCloud& cloud,
    const GaussianRenderSettings& settings,
    std::uint32_t tileSize) {
    const auto projection = projectGaussianCloudNative(backend, cloud, settings, tileSize);
    const auto batch = buildGaussianRasterBatchFromProjection(projection, settings);
    return renderGaussianRasterBatchHeadless(backend, batch, settings.image);
}

static_assert(sizeof(GaussianProjectionInput) == 64U);
static_assert(sizeof(GaussianProjectedSplat) == 64U);
static_assert(sizeof(GaussianProjectionParameters) == 96U);
static_assert(alignof(GaussianProjectionInput) == 16U);
static_assert(alignof(GaussianProjectedSplat) == 16U);
static_assert(alignof(GaussianProjectionParameters) == 16U);

} // namespace vulkax::render
