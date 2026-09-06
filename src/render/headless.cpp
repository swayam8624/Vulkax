#include "vulkax/render/headless.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VULKAX_HAS_VULKAN_RENDER
#define VULKAX_HAS_VULKAN_RENDER 0
#endif
#ifndef VULKAX_HAS_METAL_RENDER
#define VULKAX_HAS_METAL_RENDER 0
#endif

namespace vulkax::render {
namespace {

void validateImage(const ImageRGBA8& image) {
    if (image.width == 0U || image.height == 0U)
        throw std::invalid_argument("image dimensions must be positive");
    const auto expected = static_cast<std::size_t>(image.width) * image.height * 4U;
    if (image.pixels.size() != expected)
        throw std::invalid_argument("RGBA image byte count does not match dimensions");
}

void appendBigEndian32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

[[nodiscard]] std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

[[nodiscard]] std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
    constexpr std::uint32_t modulus = 65521U;
    std::uint32_t a = 1U;
    std::uint32_t b = 0U;
    for (const auto byte : data) {
        a = (a + byte) % modulus;
        b = (b + a) % modulus;
    }
    return (b << 16U) | a;
}

void appendChunk(
    std::vector<std::uint8_t>& png,
    const std::array<char, 4>& type,
    const std::vector<std::uint8_t>& data) {
    if (data.size() > 0xFFFFFFFFULL)
        throw std::runtime_error("PNG chunk exceeds format size limit");
    appendBigEndian32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t crcStart = png.size();
    for (const char character : type) png.push_back(static_cast<std::uint8_t>(character));
    png.insert(png.end(), data.begin(), data.end());
    appendBigEndian32(png, crc32(png.data() + crcStart, 4U + data.size()));
}

[[nodiscard]] std::vector<std::uint8_t> makeStoredZlibStream(
    const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> result;
    result.reserve(raw.size() + raw.size() / 65535U * 5U + 16U);

    // CMF/FLG for DEFLATE with a 32 KiB window and fastest/no compression.
    result.push_back(0x78U);
    result.push_back(0x01U);

    std::size_t offset = 0U;
    while (offset < raw.size()) {
        const std::size_t remaining = raw.size() - offset;
        const auto blockSize = static_cast<std::uint16_t>(
            std::min<std::size_t>(remaining, 65535U));
        const bool finalBlock = offset + blockSize == raw.size();

        // Stored blocks are byte-aligned. BFINAL occupies bit zero and BTYPE=00.
        result.push_back(finalBlock ? 0x01U : 0x00U);
        result.push_back(static_cast<std::uint8_t>(blockSize & 0xFFU));
        result.push_back(static_cast<std::uint8_t>((blockSize >> 8U) & 0xFFU));
        const std::uint16_t complement = static_cast<std::uint16_t>(~blockSize);
        result.push_back(static_cast<std::uint8_t>(complement & 0xFFU));
        result.push_back(static_cast<std::uint8_t>((complement >> 8U) & 0xFFU));
        result.insert(
            result.end(),
            raw.begin() + static_cast<std::ptrdiff_t>(offset),
            raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    }

    appendBigEndian32(result, adler32(raw));
    return result;
}

} // namespace

#if VULKAX_HAS_VULKAN_RENDER
ImageRGBA8 renderParticlesVulkan(const std::vector<visualization::ParticleInstance>& particles,
                                 const RenderSettings& settings);
#endif
#if VULKAX_HAS_METAL_RENDER
ImageRGBA8 renderParticlesMetal(const std::vector<visualization::ParticleInstance>& particles,
                                const RenderSettings& settings);
#endif

std::vector<backend::BackendKind> availableHeadlessRenderBackends() {
    std::vector<backend::BackendKind> result;
#if VULKAX_HAS_VULKAN_RENDER
    result.push_back(backend::BackendKind::Vulkan);
#endif
#if VULKAX_HAS_METAL_RENDER
    result.push_back(backend::BackendKind::Metal);
#endif
    return result;
}

ImageRGBA8 renderParticlesHeadless(backend::BackendKind backend,
                                   const std::vector<visualization::ParticleInstance>& particles,
                                   const RenderSettings& settings) {
    if (settings.width == 0 || settings.height == 0 || settings.worldScale <= 0.0)
        throw std::invalid_argument("invalid headless render settings");
    switch (backend) {
    case backend::BackendKind::Vulkan:
#if VULKAX_HAS_VULKAN_RENDER
        return renderParticlesVulkan(particles, settings);
#else
        throw std::runtime_error("Vulkan headless renderer is not built");
#endif
    case backend::BackendKind::Metal:
#if VULKAX_HAS_METAL_RENDER
        return renderParticlesMetal(particles, settings);
#else
        throw std::runtime_error("Metal headless renderer is not built");
#endif
    case backend::BackendKind::OpenGL:
        throw std::runtime_error("OpenGL headless renderer is not built");
    }
    throw std::logic_error("unknown renderer backend");
}

void writePpm(const ImageRGBA8& image, const std::string& path) {
    validateImage(image);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("failed to open PPM output: " + path);
    stream << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t i = 0; i < image.pixels.size(); i += 4U)
        stream.write(reinterpret_cast<const char*>(image.pixels.data() + i), 3);
}

void writePng(const ImageRGBA8& image, const std::string& path) {
    validateImage(image);

    const auto rowBytes = static_cast<std::size_t>(image.width) * 4U;
    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1U) * image.height);
    for (std::uint32_t y = 0U; y < image.height; ++y) {
        raw.push_back(0U); // PNG filter type: None.
        const auto rowStart = static_cast<std::size_t>(y) * rowBytes;
        raw.insert(
            raw.end(),
            image.pixels.begin() + static_cast<std::ptrdiff_t>(rowStart),
            image.pixels.begin() + static_cast<std::ptrdiff_t>(rowStart + rowBytes));
    }

    std::vector<std::uint8_t> png{
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};

    std::vector<std::uint8_t> ihdr;
    ihdr.reserve(13U);
    appendBigEndian32(ihdr, image.width);
    appendBigEndian32(ihdr, image.height);
    ihdr.push_back(8U); // bit depth
    ihdr.push_back(6U); // RGBA
    ihdr.push_back(0U); // compression
    ihdr.push_back(0U); // filter
    ihdr.push_back(0U); // no interlace
    appendChunk(png, {'I', 'H', 'D', 'R'}, ihdr);

    appendChunk(png, {'I', 'D', 'A', 'T'}, makeStoredZlibStream(raw));
    appendChunk(png, {'I', 'E', 'N', 'D'}, {});

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("failed to open PNG output: " + path);
    stream.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!stream) throw std::runtime_error("failed to write PNG output: " + path);
}

} // namespace vulkax::render
