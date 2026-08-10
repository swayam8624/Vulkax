#pragma once

#include "vulkax/field/field.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace vulkax::visualization {

struct Rgba8 { std::uint8_t r{}, g{}, b{}, a{255}; };
struct Image { std::uint32_t width{}, height{}; std::vector<Rgba8> pixels; };

enum class ColorMap { Viridis, Diverging };
[[nodiscard]] Rgba8 mapScientificColor(double normalizedValue, ColorMap map = ColorMap::Viridis);
[[nodiscard]] Image renderScalarSliceZ(const field::ScalarField& field, std::uint32_t z,
                                       double minimum, double maximum, ColorMap map = ColorMap::Viridis,
                                       std::uint32_t scale = 1);
[[nodiscard]] Image renderParticlesOrthographic(const field::ParticleSet& particles,
                                                std::uint32_t width, std::uint32_t height,
                                                double worldHalfExtent, std::span<const double> scalar = {});
void writePpm(const Image& image, const std::filesystem::path& path);
void writeObj(const field::TriangleMesh& mesh, const std::filesystem::path& path);

} // namespace vulkax::visualization
