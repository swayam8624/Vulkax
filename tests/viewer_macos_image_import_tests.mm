#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include "vulkax/viewer/macos_image_import.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace {

bool near(float a, float b, float tolerance = 0.035F) {
    return std::abs(a - b) <= tolerance;
}

void assertColor(const std::array<float, 3>& actual, float r, float g, float b) {
    assert(near(actual[0], r));
    assert(near(actual[1], g));
    assert(near(actual[2], b));
}

} // namespace

int main() {
    @autoreleasepool {
        namespace fs = std::filesystem;
        const auto root = fs::temp_directory_path() / "vulkax-viewer-macos-image-import-tests";
        fs::remove_all(root);
        fs::create_directories(root);
        const auto pngPath = root / "orientation.png";

        // Conventional top-to-bottom RGBA rows:
        //   red   green
        //   blue  yellow
        std::vector<std::uint8_t> rgba{
            255,   0,   0, 255,     0, 255,   0, 255,
              0,   0, 255, 255,   255, 255,   0, 128,
        };

        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        assert(colorSpace != nullptr);
        CGDataProviderRef provider = CGDataProviderCreateWithData(
            nullptr, rgba.data(), rgba.size(), nullptr);
        assert(provider != nullptr);
        CGImageRef image = CGImageCreate(
            2, 2, 8, 32, 8,
            colorSpace,
            kCGBitmapByteOrderDefault | kCGImageAlphaLast,
            provider,
            nullptr,
            false,
            kCGRenderingIntentDefault);
        assert(image != nullptr);

        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:image];
        assert(rep != nil);
        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        assert(png != nil);
        assert([png writeToFile:[NSString stringWithUTF8String:pngPath.string().c_str()] atomically:YES]);

        CGImageRelease(image);
        CGDataProviderRelease(provider);
        CGColorSpaceRelease(colorSpace);

        vulkax::viewer::ImageSplatCardSettings settings;
        settings.maxSplats = 4U;
        settings.worldHeight = 2.0;
        settings.alphaThreshold = 0.01F;
        const auto scene = vulkax::viewer::loadMacImageAsSplatCard(pngPath, settings);

        assert(scene.surfaceKind == "image_splat_card_2_5d");
        assert(scene.before.size() == 4U);
        assert(scene.after.size() == 4U);
        assert(!scene.hasSurface());
        assert(std::abs(scene.bounds.minimum.x + 1.0) < 1.0e-9);
        assert(std::abs(scene.bounds.maximum.x - 1.0) < 1.0e-9);
        assert(std::abs(scene.bounds.minimum.y + 1.0) < 1.0e-9);
        assert(std::abs(scene.bounds.maximum.y - 1.0) < 1.0e-9);

        // makeImageSplatCard preserves row-major order: top-left, top-right,
        // bottom-left, bottom-right. This verifies the AppKit normalization path
        // has not vertically inverted the decoded image or swapped RGB channels.
        assert(scene.before[0].position.y > 0.0);
        assert(scene.before[1].position.y > 0.0);
        assert(scene.before[2].position.y < 0.0);
        assert(scene.before[3].position.y < 0.0);
        assertColor(scene.before[0].color, 1.0F, 0.0F, 0.0F);
        assertColor(scene.before[1].color, 0.0F, 1.0F, 0.0F);
        assertColor(scene.before[2].color, 0.0F, 0.0F, 1.0F);
        assertColor(scene.before[3].color, 1.0F, 1.0F, 0.0F);
        assert(near(scene.before[0].opacity, 1.0F, 0.01F));
        assert(near(scene.before[3].opacity, 128.0F / 255.0F, 0.03F));

        for (const auto& splat : scene.before) {
            assert(std::abs(splat.position.z) < 1.0e-12);
            assert(splat.scale[0] > splat.scale[2]);
            assert(splat.scale[1] > splat.scale[2]);
            assert(splat.shCoefficientCount == 1U);
            assert(splat.id.valid());
        }

        fs::remove_all(root);
        return 0;
    }
}
