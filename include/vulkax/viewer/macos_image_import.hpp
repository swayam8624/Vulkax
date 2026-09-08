#pragma once

#if defined(__APPLE__)

#include "vulkax/viewer/image_splat_card.hpp"

#include <filesystem>

namespace vulkax::viewer {

// Decode a macOS-supported raster image through AppKit, normalize it to RGBA8,
// then pass it through the explicit 2.5D image splat-card representation.
[[nodiscard]] ViewerScene loadMacImageAsSplatCard(
    const std::filesystem::path& imagePath,
    const ImageSplatCardSettings& settings = {});

} // namespace vulkax::viewer

#endif
