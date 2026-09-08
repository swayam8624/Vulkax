#import <AppKit/AppKit.h>

#include "vulkax/viewer/macos_image_import.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace vulkax::viewer {

ViewerScene loadMacImageAsSplatCard(const std::filesystem::path& imagePath,
                                    const ImageSplatCardSettings& settings) {
    @autoreleasepool {
        NSString* path = [NSString stringWithUTF8String:imagePath.string().c_str()];
        NSData* data = [NSData dataWithContentsOfFile:path];
        if (!data) throw std::runtime_error("failed to read image file: " + imagePath.string());

        NSBitmapImageRep* source = [NSBitmapImageRep imageRepWithData:data];
        NSImage* image = [[NSImage alloc] initWithData:data];
        if (!source || !image)
            throw std::runtime_error("AppKit could not decode image: " + imagePath.string());

        const NSInteger width = source.pixelsWide;
        const NSInteger height = source.pixelsHigh;
        if (width <= 0 || height <= 0)
            throw std::runtime_error("decoded image has invalid dimensions");

        NSBitmapImageRep* rgba = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL
            pixelsWide:width
            pixelsHigh:height
            bitsPerSample:8
            samplesPerPixel:4
            hasAlpha:YES
            isPlanar:NO
            colorSpaceName:NSDeviceRGBColorSpace
            bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
            bytesPerRow:width * 4
            bitsPerPixel:32];
        if (!rgba || !rgba.bitmapData)
            throw std::runtime_error("failed to allocate normalized RGBA image buffer");

        NSGraphicsContext* context = [NSGraphicsContext graphicsContextWithBitmapImageRep:rgba];
        if (!context) throw std::runtime_error("failed to create image normalization context");

        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:context];
        [image drawInRect:NSMakeRect(0, 0, width, height)
                 fromRect:NSZeroRect
                operation:NSCompositingOperationCopy
                 fraction:1.0
           respectFlipped:NO
                    hints:nil];
        [context flushGraphics];
        [NSGraphicsContext restoreGraphicsState];

        // NSBitmapImageRep's drawing coordinate system is bottom-up. Normalize to
        // top-to-bottom rows so makeImageSplatCard has one platform-independent
        // interpretation and maps input row zero to +Y.
        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
        const auto* sourceBytes = reinterpret_cast<const std::uint8_t*>(rgba.bitmapData);
        const std::size_t sourceStride = static_cast<std::size_t>(rgba.bytesPerRow);
        const std::size_t outputStride = static_cast<std::size_t>(width) * 4U;
        for (NSInteger y = 0; y < height; ++y) {
            const std::size_t sourceY = static_cast<std::size_t>(height - 1 - y);
            const auto* row = sourceBytes + sourceY * sourceStride;
            auto* destination = pixels.data() + static_cast<std::size_t>(y) * outputStride;
            std::copy(row, row + outputStride, destination);
        }

        return makeImageSplatCard(static_cast<std::size_t>(width),
                                  static_cast<std::size_t>(height),
                                  pixels,
                                  settings);
    }
}

} // namespace vulkax::viewer
