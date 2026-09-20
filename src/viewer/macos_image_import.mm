#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include "vulkax/viewer/macos_image_import.hpp"

#include <algorithm>
#include <cmath>
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
        if (!source)
            throw std::runtime_error("AppKit could not decode image: " + imagePath.string());

        const NSInteger width = source.pixelsWide;
        const NSInteger height = source.pixelsHigh;
        if (width <= 0 || height <= 0)
            throw std::runtime_error("decoded image has invalid dimensions");

        NSRect proposed = NSMakeRect(0.0, 0.0, static_cast<CGFloat>(width),
                                    static_cast<CGFloat>(height));
        NSImage* image = [[NSImage alloc] initWithData:data];
        CGImageRef cgImage = [image CGImageForProposedRect:&proposed context:nil hints:nil];
        if (!cgImage)
            throw std::runtime_error("AppKit could not expose decoded CGImage: " + imagePath.string());

        const std::size_t pixelWidth = static_cast<std::size_t>(width);
        const std::size_t pixelHeight = static_cast<std::size_t>(height);
        const std::size_t bytesPerRow = pixelWidth * 4U;
        std::vector<std::uint8_t> pixels(pixelWidth * pixelHeight * 4U, 0U);

        CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        if (!colorSpace) colorSpace = CGColorSpaceCreateDeviceRGB();
        if (!colorSpace)
            throw std::runtime_error("failed to create sRGB image color space");

        // CoreGraphics has a well-defined 32-bit premultiplied-RGBA bitmap path
        // across hosted/headless macOS runners. NSGraphicsContext +
        // AlphaNonpremultiplied is not guaranteed to yield a drawable bitmap
        // context and failed on macOS 26 CI.
        const CGBitmapInfo bitmapInfo =
            static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big |
                                      kCGImageAlphaPremultipliedLast);
        CGContextRef context = CGBitmapContextCreate(
            pixels.data(),
            pixelWidth,
            pixelHeight,
            8U,
            bytesPerRow,
            colorSpace,
            bitmapInfo);
        CGColorSpaceRelease(colorSpace);
        if (!context)
            throw std::runtime_error("failed to create CoreGraphics RGBA normalization context");

        // Normalize all decoders to top-to-bottom RGBA rows. makeImageSplatCard
        // maps row zero to +Y, so this keeps PNG/JPEG/WebP orientation identical.
        CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(pixelHeight));
        CGContextScaleCTM(context, 1.0, -1.0);
        CGContextSetBlendMode(context, kCGBlendModeCopy);
        CGContextDrawImage(context,
                           CGRectMake(0.0, 0.0,
                                      static_cast<CGFloat>(pixelWidth),
                                      static_cast<CGFloat>(pixelHeight)),
                           cgImage);
        CGContextRelease(context);

        // The normalization buffer is premultiplied RGBA. Undo premultiplication
        // before constructing Gaussian colours so a translucent red pixel remains
        // red with lower opacity rather than a darker red with lower opacity.
        for (std::size_t offset = 0U; offset < pixels.size(); offset += 4U) {
            const unsigned alpha = pixels[offset + 3U];
            if (alpha == 0U) {
                pixels[offset + 0U] = 0U;
                pixels[offset + 1U] = 0U;
                pixels[offset + 2U] = 0U;
                continue;
            }
            if (alpha == 255U) continue;
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
                const unsigned premultiplied = pixels[offset + channel];
                const unsigned restored =
                    static_cast<unsigned>(std::lround(
                        static_cast<double>(premultiplied) * 255.0 /
                        static_cast<double>(alpha)));
                pixels[offset + channel] =
                    static_cast<std::uint8_t>(std::min(restored, 255U));
            }
        }

        return makeImageSplatCard(pixelWidth, pixelHeight, pixels, settings);
    }
}

} // namespace vulkax::viewer
