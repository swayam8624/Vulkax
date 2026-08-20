#include "vulkax/render/gaussian.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#ifndef VULKAX_HAS_VULKAN_RENDER
#define VULKAX_HAS_VULKAN_RENDER 0
#endif
#ifndef VULKAX_HAS_METAL_RENDER
#define VULKAX_HAS_METAL_RENDER 0
#endif

namespace vulkax::render {

#if VULKAX_HAS_VULKAN_RENDER
ImageRGBA8 renderGaussianVerticesVulkan(const std::vector<GaussianRasterVertex>& vertices,
                                        const RenderSettings& settings);
#endif
#if VULKAX_HAS_METAL_RENDER
ImageRGBA8 renderGaussianVerticesMetal(const std::vector<GaussianRasterVertex>& vertices,
                                       const RenderSettings& settings);
#endif

namespace {

using Matrix3 = std::array<std::array<double, 3>, 3>;

struct ProjectedSplat {
    double depth{};
    std::array<GaussianRasterVertex, 6> vertices{};
};

[[nodiscard]] std::array<double, 3> multiply(const Matrix3& matrix, math::Vec3 value) {
    return {
        matrix[0][0] * value.x + matrix[0][1] * value.y + matrix[0][2] * value.z,
        matrix[1][0] * value.x + matrix[1][1] * value.y + matrix[1][2] * value.z,
        matrix[2][0] * value.x + matrix[2][1] * value.y + matrix[2][2] * value.z,
    };
}

[[nodiscard]] Matrix3 quaternionRotation(const std::array<double, 4>& quaternion) {
    const double w = quaternion[0];
    const double x = quaternion[1];
    const double y = quaternion[2];
    const double z = quaternion[3];
    return {{{1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w),
              2.0 * (x * z + y * w)},
             {2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z),
              2.0 * (y * z - x * w)},
             {2.0 * (x * z - y * w), 2.0 * (y * z + x * w),
              1.0 - 2.0 * (x * x + y * y)}}};
}

[[nodiscard]] Matrix3 covarianceWorld(const gaussian::GaussianSplat& splat) {
    const auto rotation = quaternionRotation(splat.rotation);
    const auto scale = splat.linearScale();
    const std::array<double, 3> variance{scale[0] * scale[0], scale[1] * scale[1],
                                         scale[2] * scale[2]};
    Matrix3 covariance{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                covariance[row][column] +=
                    rotation[row][axis] * variance[axis] * rotation[column][axis];
            }
        }
    }
    return covariance;
}

[[nodiscard]] Matrix3 covarianceCamera(const Matrix3& world,
                                       math::Vec3 right,
                                       math::Vec3 up,
                                       math::Vec3 forward) {
    const Matrix3 basis{{{right.x, right.y, right.z},
                         {up.x, up.y, up.z},
                         {forward.x, forward.y, forward.z}}};
    Matrix3 temporary{};
    Matrix3 camera{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t inner = 0; inner < 3; ++inner)
                temporary[row][column] += basis[row][inner] * world[inner][column];
        }
    }
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t inner = 0; inner < 3; ++inner)
                camera[row][column] += temporary[row][inner] * basis[column][inner];
        }
    }
    return camera;
}

[[nodiscard]] double shCoefficient(const gaussian::GaussianSplat& splat,
                                   std::size_t channel,
                                   std::size_t coefficient) {
    if (coefficient == 0) return splat.shDC[channel];
    const std::size_t index = (coefficient - 1) * 3 + channel;
    return index < splat.shRest.size() ? splat.shRest[index] : 0.0;
}

[[nodiscard]] std::array<double, 3> evaluateSphericalHarmonics(
    const gaussian::GaussianSplat& splat,
    math::Vec3 direction,
    bool evaluateHigherOrders) {
    constexpr double c0 = 0.28209479177387814;
    constexpr double c1 = 0.4886025119029199;
    constexpr std::array<double, 5> c2{1.0925484305920792, -1.0925484305920792,
                                       0.31539156525252005, -1.0925484305920792,
                                       0.5462742152960396};
    constexpr std::array<double, 7> c3{-0.5900435899266435, 2.890611442640554,
                                       -0.4570457994644658, 0.3731763325901154,
                                       -0.4570457994644658, 1.445305721320277,
                                       -0.5900435899266435};

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

    const std::size_t coefficientCount = 1 + splat.shRest.size() / 3;
    std::array<double, 3> color{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        double value = c0 * shCoefficient(splat, channel, 0);
        if (evaluateHigherOrders && coefficientCount >= 4) {
            value += -c1 * y * shCoefficient(splat, channel, 1) +
                     c1 * z * shCoefficient(splat, channel, 2) -
                     c1 * x * shCoefficient(splat, channel, 3);
        }
        if (evaluateHigherOrders && coefficientCount >= 9) {
            value += c2[0] * xy * shCoefficient(splat, channel, 4) +
                     c2[1] * yz * shCoefficient(splat, channel, 5) +
                     c2[2] * (2.0 * zz - xx - yy) * shCoefficient(splat, channel, 6) +
                     c2[3] * xz * shCoefficient(splat, channel, 7) +
                     c2[4] * (xx - yy) * shCoefficient(splat, channel, 8);
        }
        if (evaluateHigherOrders && coefficientCount >= 16) {
            value += c3[0] * y * (3.0 * xx - yy) * shCoefficient(splat, channel, 9) +
                     c3[1] * xy * z * shCoefficient(splat, channel, 10) +
                     c3[2] * y * (4.0 * zz - xx - yy) * shCoefficient(splat, channel, 11) +
                     c3[3] * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) *
                         shCoefficient(splat, channel, 12) +
                     c3[4] * x * (4.0 * zz - xx - yy) * shCoefficient(splat, channel, 13) +
                     c3[5] * z * (xx - yy) * shCoefficient(splat, channel, 14) +
                     c3[6] * x * (xx - 3.0 * yy) * shCoefficient(splat, channel, 15);
        }
        color[channel] = std::clamp(value + 0.5, 0.0, 1.0);
    }
    return color;
}

[[nodiscard]] double quadraticForm(const std::array<double, 3>& lhs,
                                   const Matrix3& matrix,
                                   const std::array<double, 3>& rhs) {
    double value = 0.0;
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            value += lhs[row] * matrix[row][column] * rhs[column];
    return value;
}

void validateSettings(const GaussianRenderSettings& settings) {
    if (settings.image.width == 0 || settings.image.height == 0)
        throw std::invalid_argument("Gaussian render dimensions must be positive");
    if (!(settings.camera.verticalFovDegrees > 0.0 && settings.camera.verticalFovDegrees < 179.0))
        throw std::invalid_argument("Gaussian camera vertical FOV must lie in (0, 179) degrees");
    if (!(settings.nearPlane > 0.0) || !(settings.sigmaCutoff > 0.0) ||
        !(settings.minimumSigmaPixels > 0.0) ||
        !(settings.maximumSigmaPixels >= settings.minimumSigmaPixels))
        throw std::invalid_argument("invalid Gaussian render projection settings");
}

} // namespace

GaussianRasterBatch buildGaussianRasterBatch(const gaussian::GaussianCloud& cloud,
                                              const GaussianRenderSettings& settings) {
    validateSettings(settings);
    GaussianRasterBatch batch;
    batch.stats.inputSplats = cloud.size();
    if (cloud.empty()) return batch;

    const math::Vec3 forward = math::normalized(settings.camera.target - settings.camera.position);
    math::Vec3 right = math::normalized(math::cross(forward, settings.camera.up));
    if (math::length(right) <= 1.0e-12)
        throw std::invalid_argument("Gaussian camera up vector is parallel to its viewing direction");
    const math::Vec3 cameraUp = math::normalized(math::cross(right, forward));

    constexpr double pi = 3.1415926535897932384626433832795;
    const double fovRadians = settings.camera.verticalFovDegrees * pi / 180.0;
    const double focalPixels =
        0.5 * static_cast<double>(settings.image.height) / std::tan(0.5 * fovRadians);
    const double width = static_cast<double>(settings.image.width);
    const double height = static_cast<double>(settings.image.height);
    const double minimumVariance = settings.minimumSigmaPixels * settings.minimumSigmaPixels;

    std::vector<ProjectedSplat> projected;
    projected.reserve(cloud.size());

    constexpr std::array<std::array<double, 2>, 6> corners{{
        {-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0},
        {-1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0},
    }};

    for (const auto& splat : cloud.splats) {
        const double opacity = splat.opacity();
        if (!std::isfinite(opacity) || opacity < (1.0 / 255.0)) {
            ++batch.stats.culledOpacity;
            continue;
        }

        const math::Vec3 delta = splat.position - settings.camera.position;
        const double cameraX = math::dot(delta, right);
        const double cameraY = math::dot(delta, cameraUp);
        const double cameraZ = math::dot(delta, forward);
        if (!std::isfinite(cameraZ) || cameraZ <= settings.nearPlane) {
            ++batch.stats.culledBehindCamera;
            continue;
        }

        const double centerX = 0.5 * width + focalPixels * cameraX / cameraZ;
        const double centerY = 0.5 * height - focalPixels * cameraY / cameraZ;

        const Matrix3 cameraCovariance =
            covarianceCamera(covarianceWorld(splat), right, cameraUp, forward);
        const double inverseZ = 1.0 / cameraZ;
        const double inverseZ2 = inverseZ * inverseZ;
        const std::array<double, 3> jacobianX{
            focalPixels * inverseZ, 0.0, -focalPixels * cameraX * inverseZ2};
        const std::array<double, 3> jacobianY{
            0.0, -focalPixels * inverseZ, focalPixels * cameraY * inverseZ2};

        const double covarianceXX =
            quadraticForm(jacobianX, cameraCovariance, jacobianX) + minimumVariance;
        const double covarianceXY = quadraticForm(jacobianX, cameraCovariance, jacobianY);
        const double covarianceYY =
            quadraticForm(jacobianY, cameraCovariance, jacobianY) + minimumVariance;

        const double trace = covarianceXX + covarianceYY;
        const double discriminant = std::sqrt(std::max(
            0.0, (covarianceXX - covarianceYY) * (covarianceXX - covarianceYY) +
                     4.0 * covarianceXY * covarianceXY));
        const double lambdaMajor = std::max(0.5 * (trace + discriminant), minimumVariance);
        const double lambdaMinor = std::max(0.5 * (trace - discriminant), minimumVariance);
        const double sigmaMajor = std::clamp(std::sqrt(lambdaMajor), settings.minimumSigmaPixels,
                                             settings.maximumSigmaPixels);
        const double sigmaMinor = std::clamp(std::sqrt(lambdaMinor), settings.minimumSigmaPixels,
                                             settings.maximumSigmaPixels);
        const double angle = 0.5 * std::atan2(2.0 * covarianceXY, covarianceXX - covarianceYY);
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);

        const std::array<double, 2> majorPixels{
            cosine * sigmaMajor * settings.sigmaCutoff,
            sine * sigmaMajor * settings.sigmaCutoff};
        const std::array<double, 2> minorPixels{
            -sine * sigmaMinor * settings.sigmaCutoff,
            cosine * sigmaMinor * settings.sigmaCutoff};
        const double extentX = std::abs(majorPixels[0]) + std::abs(minorPixels[0]);
        const double extentY = std::abs(majorPixels[1]) + std::abs(minorPixels[1]);
        if (centerX + extentX < 0.0 || centerX - extentX >= width ||
            centerY + extentY < 0.0 || centerY - extentY >= height) {
            ++batch.stats.culledOutsideImage;
            continue;
        }

        const double centerNdcX = 2.0 * centerX / width - 1.0;
        const double centerNdcY = 1.0 - 2.0 * centerY / height;
        const std::array<double, 2> majorNdc{
            2.0 * majorPixels[0] / width,
            -2.0 * majorPixels[1] / height};
        const std::array<double, 2> minorNdc{
            2.0 * minorPixels[0] / width,
            -2.0 * minorPixels[1] / height};
        const auto color = evaluateSphericalHarmonics(
            splat, splat.position - settings.camera.position, settings.evaluateSphericalHarmonics);

        ProjectedSplat output;
        output.depth = cameraZ;
        for (std::size_t vertex = 0; vertex < corners.size(); ++vertex) {
            const double localX = corners[vertex][0];
            const double localY = corners[vertex][1];
            output.vertices[vertex] = {
                static_cast<float>(centerNdcX + localX * majorNdc[0] + localY * minorNdc[0]),
                static_cast<float>(centerNdcY + localX * majorNdc[1] + localY * minorNdc[1]),
                0.0F,
                static_cast<float>(localX * settings.sigmaCutoff),
                static_cast<float>(localY * settings.sigmaCutoff),
                static_cast<float>(color[0]),
                static_cast<float>(color[1]),
                static_cast<float>(color[2]),
                static_cast<float>(std::clamp(opacity, 0.0, 0.999)),
            };
        }
        projected.push_back(output);
    }

    std::stable_sort(projected.begin(), projected.end(),
                     [](const ProjectedSplat& lhs, const ProjectedSplat& rhs) {
                         return lhs.depth > rhs.depth;
                     });
    batch.stats.visibleSplats = projected.size();
    batch.vertices.reserve(projected.size() * 6);
    for (const auto& splat : projected)
        batch.vertices.insert(batch.vertices.end(), splat.vertices.begin(), splat.vertices.end());
    return batch;
}

GaussianRenderResult renderGaussianCloudHeadless(backend::BackendKind backend,
                                                  const gaussian::GaussianCloud& cloud,
                                                  const GaussianRenderSettings& settings) {
    const auto batch = buildGaussianRasterBatch(cloud, settings);
    ImageRGBA8 image;
    switch (backend) {
        case backend::BackendKind::Vulkan:
#if VULKAX_HAS_VULKAN_RENDER
            image = renderGaussianVerticesVulkan(batch.vertices, settings.image);
            break;
#else
            throw std::runtime_error("Vulkan Gaussian rendering was not compiled into this build");
#endif
        case backend::BackendKind::Metal:
#if VULKAX_HAS_METAL_RENDER
            image = renderGaussianVerticesMetal(batch.vertices, settings.image);
            break;
#else
            throw std::runtime_error("Metal Gaussian rendering was not compiled into this build");
#endif
        case backend::BackendKind::OpenGL:
            throw std::runtime_error("OpenGL Gaussian rendering is not implemented yet");
    }
    return {std::move(image), batch.stats};
}

} // namespace vulkax::render
