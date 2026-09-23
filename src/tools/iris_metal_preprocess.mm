#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::string input;
    std::string output;
    std::string meta;
    std::string kind{"clean"};
    double value{0.0};
    std::uint32_t seed{20260923u};
    int width{640};
    double maxSeconds{14.0};
    bool selfTest{false};
};

[[noreturn]] void usage() {
    std::cerr
        << "Usage: vulkax_iris_metal_preprocess --input VIDEO --output FRAMES.raw "
           "--meta META.json [--kind clean|noise|blur|frame_drop|fps|occlusion|crop] "
           "[--value X] [--seed N] [--width 640] [--max-seconds 14]\\n";
    std::exit(2);
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << "\\n";
                usage();
            }
            return argv[++i];
        };
        if (a == "--input") o.input = need("--input");
        else if (a == "--output") o.output = need("--output");
        else if (a == "--meta") o.meta = need("--meta");
        else if (a == "--kind") o.kind = need("--kind");
        else if (a == "--value") o.value = std::stod(need("--value"));
        else if (a == "--seed") o.seed = static_cast<std::uint32_t>(std::stoul(need("--seed")));
        else if (a == "--width") o.width = std::stoi(need("--width"));
        else if (a == "--max-seconds") o.maxSeconds = std::stod(need("--max-seconds"));
        else if (a == "--self-test") o.selfTest = true;
        else usage();
    }
    if ((!o.selfTest && (o.input.empty() || o.output.empty() || o.meta.empty())) ||
        o.width <= 0 || o.maxSeconds <= 0.0) usage();
    const std::string allowed[] = {"clean","noise","blur","frame_drop","fps","occlusion","crop"};
    if (std::find(std::begin(allowed), std::end(allowed), o.kind) == std::end(allowed)) {
        throw std::runtime_error("unsupported corruption kind: " + o.kind);
    }
    return o;
}

std::uint32_t kindId(const std::string& kind) {
    if (kind == "noise") return 1;
    if (kind == "blur") return 2;
    if (kind == "occlusion") return 3;
    if (kind == "crop") return 4;
    return 0;
}

struct CorruptParams {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t kind;
    float value;
    std::uint32_t seed;
    std::uint32_t frame;
};

struct ScaleParams {
    std::uint32_t srcWidth;
    std::uint32_t srcHeight;
    std::uint32_t dstWidth;
    std::uint32_t dstHeight;
};

std::string nsError(NSError* error) {
    if (error == nil) return "unknown Apple framework error";
    const char* p = [[error localizedDescription] UTF8String];
    return p != nullptr ? p : "unknown Apple framework error";
}

void writeMeta(const Options& o, int width, int height, double fps, std::uint64_t frames,
               const std::string& deviceName, double sourceFps, int sourceWidth, int sourceHeight) {
    std::ofstream f(o.meta, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write metadata: " + o.meta);
    f << "{\\n"
      << "  \\"schema\\": \\"vulkax.iris_metal_gray_frames\\",\\n"
      << "  \\"version\\": 1,\\n"
      << "  \\"backend\\": \\"metal\\",\\n"
      << "  \\"device\\": \\"" << deviceName << "\\",\\n"
      << "  \\"width\\": " << width << ",\\n"
      << "  \\"height\\": " << height << ",\\n"
      << "  \\"fps\\": " << fps << ",\\n"
      << "  \\"frame_count\\": " << frames << ",\\n"
      << "  \\"source_fps\\": " << sourceFps << ",\\n"
      << "  \\"source_width\\": " << sourceWidth << ",\\n"
      << "  \\"source_height\\": " << sourceHeight << ",\\n"
      << "  \\"kind\\": \\"" << o.kind << "\\",\\n"
      << "  \\"value\\": " << o.value << ",\\n"
      << "  \\"seed\\": " << o.seed << ",\\n"
      << "  \\"max_seconds\\": " << o.maxSeconds << "\\n"
      << "}\\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse(argc, argv);
        @autoreleasepool {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (device == nil) throw std::runtime_error("no Metal device available");
            id<MTLCommandQueue> queue = [device newCommandQueue];
            if (queue == nil) throw std::runtime_error("failed to create Metal command queue");

            NSString* shaderSource = @R"metal(
#include <metal_stdlib>
using namespace metal;

struct CorruptParams {
    uint width;
    uint height;
    uint kind;
    float value;
    uint seed;
    uint frame;
};
struct ScaleParams {
    uint srcWidth;
    uint srcHeight;
    uint dstWidth;
    uint dstHeight;
};

inline uint hash_u32(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline float uniform01(uint x) {
    return (float(hash_u32(x)) + 0.5f) / 4294967296.0f;
}
inline float gaussian01(uint x, uint y) {
    float u1 = max(1.0e-7f, uniform01(x));
    float u2 = uniform01(y);
    return sqrt(-2.0f * log(u1)) * cos(6.28318530718f * u2);
}
inline float4 sample_pixel(texture2d<float, access::sample> src, sampler s, float2 uv) {
    return src.sample(s, clamp(uv, float2(0.0f), float2(1.0f)));
}

kernel void corrupt_frame(texture2d<float, access::sample> src [[texture(0)]],
                          texture2d<float, access::write> dst [[texture(1)]],
                          constant CorruptParams& p [[buffer(0)]],
                          uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= p.width || gid.y >= p.height) return;
    constexpr sampler s(address::clamp_to_edge, filter::linear, coord::normalized);
    float2 uv = (float2(gid) + 0.5f) / float2(p.width, p.height);

    if (p.kind == 4u) {
        float c = clamp(p.value, 0.0f, 0.24f);
        uv = float2(c) + uv * (1.0f - 2.0f * c);
    }

    float4 value;
    if (p.kind == 2u) {
        int radius = clamp(int(round((p.value - 1.0f) * 0.5f)), 1, 7);
        float sigma = max(0.8f, 0.3f * (float(radius) - 1.0f) + 0.8f);
        float inv = 1.0f / (2.0f * sigma * sigma);
        float4 acc = float4(0.0f);
        float sumw = 0.0f;
        for (int dy = -7; dy <= 7; ++dy) {
            if (abs(dy) > radius) continue;
            for (int dx = -7; dx <= 7; ++dx) {
                if (abs(dx) > radius) continue;
                float w = exp(-float(dx * dx + dy * dy) * inv);
                float2 q = uv + float2(dx, dy) / float2(p.width, p.height);
                acc += sample_pixel(src, s, q) * w;
                sumw += w;
            }
        }
        value = acc / max(sumw, 1.0e-6f);
    } else {
        value = sample_pixel(src, s, uv);
    }

    if (p.kind == 1u) {
        uint base = p.seed ^ (p.frame * 0x9e3779b9u) ^ (gid.x * 0x85ebca6bu) ^ (gid.y * 0xc2b2ae35u);
        float sigma = p.value / 255.0f;
        value.r = clamp(value.r + sigma * gaussian01(base, base ^ 0xa341316cu), 0.0f, 1.0f);
        value.g = clamp(value.g + sigma * gaussian01(base ^ 0xc8013ea4u, base ^ 0xad90777du), 0.0f, 1.0f);
        value.b = clamp(value.b + sigma * gaussian01(base ^ 0x7e95761eu, base ^ 0x9e3779b9u), 0.0f, 1.0f);
    } else if (p.kind == 3u) {
        float side = sqrt(clamp(p.value, 0.0f, 0.95f));
        float2 d = abs(uv - float2(0.5f));
        if (d.x <= 0.5f * side && d.y <= 0.5f * side) {
            value = float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
    }
    dst.write(value, gid);
}

kernel void scale_gray(texture2d<float, access::sample> src [[texture(0)]],
                       texture2d<uchar, access::write> dst [[texture(1)]],
                       constant ScaleParams& p [[buffer(0)]],
                       uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= p.dstWidth || gid.y >= p.dstHeight) return;
    constexpr sampler s(address::clamp_to_edge, filter::linear, coord::normalized);
    float2 uv = (float2(gid) + 0.5f) / float2(p.dstWidth, p.dstHeight);
    float4 c = src.sample(s, uv);
    float gray = clamp(dot(c.rgb, float3(0.299f, 0.587f, 0.114f)), 0.0f, 1.0f);
    dst.write(uchar4(uchar(round(gray * 255.0f)), 0, 0, 0), gid);
}
)metal";

            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:shaderSource options:nil error:&error];
            if (library == nil) throw std::runtime_error("Metal shader compilation failed: " + nsError(error));
            id<MTLFunction> corruptFunction = [library newFunctionWithName:@"corrupt_frame"];
            id<MTLFunction> scaleFunction = [library newFunctionWithName:@"scale_gray"];
            id<MTLComputePipelineState> corruptPipeline =
                [device newComputePipelineStateWithFunction:corruptFunction error:&error];
            if (corruptPipeline == nil) throw std::runtime_error("Metal corrupt pipeline failed: " + nsError(error));
            id<MTLComputePipelineState> scalePipeline =
                [device newComputePipelineStateWithFunction:scaleFunction error:&error];
            if (scalePipeline == nil) throw std::runtime_error("Metal scale pipeline failed: " + nsError(error));

            if (options.selfTest) {
                const char* name = [[device name] UTF8String];
                std::cout << "VALID Metal IRIS preprocessor self-test\n"
                          << "DEVICE " << (name != nullptr ? name : "Metal device") << "\n";
                return 0;
            }

            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:options.input.c_str()]];
            AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
            NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
            if (tracks.count == 0) throw std::runtime_error("video has no video track");
            AVAssetTrack* track = tracks.firstObject;
            const CGSize natural = track.naturalSize;
            const int sourceWidth = static_cast<int>(std::lround(std::abs(natural.width)));
            const int sourceHeight = static_cast<int>(std::lround(std::abs(natural.height)));
            if (sourceWidth <= 0 || sourceHeight <= 0) throw std::runtime_error("invalid video dimensions");
            const double sourceFps = track.nominalFrameRate > 0.0f ? static_cast<double>(track.nominalFrameRate) : 60.0;
            int targetHeight = static_cast<int>(std::lround(static_cast<double>(sourceHeight) *
                                                            static_cast<double>(options.width) /
                                                            static_cast<double>(sourceWidth)));
            targetHeight = std::max(1, targetHeight);

            AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
            if (reader == nil) throw std::runtime_error("AVAssetReader creation failed: " + nsError(error));
            NSDictionary* settings = @{
                (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                (id)kCVPixelBufferMetalCompatibilityKey : @YES
            };
            AVAssetReaderTrackOutput* output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track
                                                                               outputSettings:settings];
            output.alwaysCopiesSampleData = NO;
            if (![reader canAddOutput:output]) throw std::runtime_error("cannot add AVAssetReader output");
            [reader addOutput:output];
            if (![reader startReading]) throw std::runtime_error("AVAssetReader failed to start: " + nsError(reader.error));

            CVMetalTextureCacheRef textureCache = nullptr;
            CVReturn cvStatus = CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &textureCache);
            if (cvStatus != kCVReturnSuccess || textureCache == nullptr)
                throw std::runtime_error("CVMetalTextureCacheCreate failed");

            MTLTextureDescriptor* intermediateDesc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                   width:static_cast<NSUInteger>(sourceWidth)
                                                                  height:static_cast<NSUInteger>(sourceHeight)
                                                               mipmapped:NO];
            intermediateDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            intermediateDesc.storageMode = MTLStorageModePrivate;
            id<MTLTexture> intermediate = [device newTextureWithDescriptor:intermediateDesc];
            if (intermediate == nil) throw std::runtime_error("failed to allocate Metal intermediate texture");

            MTLTextureDescriptor* grayDesc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Uint
                                                                   width:static_cast<NSUInteger>(options.width)
                                                                  height:static_cast<NSUInteger>(targetHeight)
                                                               mipmapped:NO];
            grayDesc.usage = MTLTextureUsageShaderWrite;
            grayDesc.storageMode = MTLStorageModeShared;
            id<MTLTexture> grayTexture = [device newTextureWithDescriptor:grayDesc];
            if (grayTexture == nil) throw std::runtime_error("failed to allocate Metal grayscale texture");

            std::ofstream raw(options.output, std::ios::binary);
            if (!raw) throw std::runtime_error("cannot open raw output: " + options.output);
            std::vector<std::uint8_t> gray(static_cast<std::size_t>(options.width) *
                                           static_cast<std::size_t>(targetHeight));
            std::vector<std::uint8_t> previous;
            const int fpsStep = options.kind == "fps"
                ? std::max(1, static_cast<int>(std::lround(sourceFps / std::max(1.0, options.value))))
                : 1;
            const double outputFps = sourceFps / static_cast<double>(fpsStep);
            const int dropPeriod = options.kind == "frame_drop"
                ? std::max(2, static_cast<int>(std::lround(1.0 / std::max(1.0e-6, options.value))))
                : 0;
            std::uint64_t inputIndex = 0;
            std::uint64_t outputCount = 0;

            while (reader.status == AVAssetReaderStatusReading) {
                @autoreleasepool {
                    CMSampleBufferRef sample = [output copyNextSampleBuffer];
                    if (sample == nullptr) break;
                    const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
                    const double seconds = CMTimeGetSeconds(pts);
                    if (std::isfinite(seconds) && seconds > options.maxSeconds) {
                        CFRelease(sample);
                        break;
                    }
                    if ((inputIndex % static_cast<std::uint64_t>(fpsStep)) != 0u) {
                        ++inputIndex;
                        CFRelease(sample);
                        continue;
                    }
                    if (dropPeriod > 0 && outputCount > 0 &&
                        (outputCount % static_cast<std::uint64_t>(dropPeriod)) == 0u &&
                        !previous.empty()) {
                        raw.write(reinterpret_cast<const char*>(previous.data()),
                                  static_cast<std::streamsize>(previous.size()));
                        ++outputCount;
                        ++inputIndex;
                        CFRelease(sample);
                        continue;
                    }

                    CVPixelBufferRef pixel = CMSampleBufferGetImageBuffer(sample);
                    if (pixel == nullptr) {
                        CFRelease(sample);
                        throw std::runtime_error("sample has no CVPixelBuffer");
                    }
                    CVMetalTextureRef cvTexture = nullptr;
                    cvStatus = CVMetalTextureCacheCreateTextureFromImage(
                        kCFAllocatorDefault, textureCache, pixel, nullptr, MTLPixelFormatBGRA8Unorm,
                        static_cast<std::size_t>(sourceWidth), static_cast<std::size_t>(sourceHeight),
                        0, &cvTexture);
                    if (cvStatus != kCVReturnSuccess || cvTexture == nullptr) {
                        CFRelease(sample);
                        throw std::runtime_error("CVMetalTextureCacheCreateTextureFromImage failed");
                    }
                    id<MTLTexture> sourceTexture = CVMetalTextureGetTexture(cvTexture);
                    if (sourceTexture == nil) {
                        CFRelease(cvTexture);
                        CFRelease(sample);
                        throw std::runtime_error("failed to acquire source Metal texture");
                    }

                    CorruptParams cp{
                        static_cast<std::uint32_t>(sourceWidth),
                        static_cast<std::uint32_t>(sourceHeight),
                        kindId(options.kind),
                        static_cast<float>(options.value),
                        options.seed,
                        static_cast<std::uint32_t>(outputCount)
                    };
                    ScaleParams sp{
                        static_cast<std::uint32_t>(sourceWidth),
                        static_cast<std::uint32_t>(sourceHeight),
                        static_cast<std::uint32_t>(options.width),
                        static_cast<std::uint32_t>(targetHeight)
                    };

                    id<MTLCommandBuffer> cb = [queue commandBuffer];
                    id<MTLComputeCommandEncoder> encoder = [cb computeCommandEncoder];
                    [encoder setComputePipelineState:corruptPipeline];
                    [encoder setTexture:sourceTexture atIndex:0];
                    [encoder setTexture:intermediate atIndex:1];
                    [encoder setBytes:&cp length:sizeof(cp) atIndex:0];
                    const NSUInteger tw1 = std::min<NSUInteger>(16, corruptPipeline.maxTotalThreadsPerThreadgroup);
                    const NSUInteger th1 = std::max<NSUInteger>(1, std::min<NSUInteger>(16, corruptPipeline.maxTotalThreadsPerThreadgroup / tw1));
                    [encoder dispatchThreads:MTLSizeMake(static_cast<NSUInteger>(sourceWidth),
                                                        static_cast<NSUInteger>(sourceHeight), 1)
                       threadsPerThreadgroup:MTLSizeMake(tw1, th1, 1)];

                    [encoder setComputePipelineState:scalePipeline];
                    [encoder setTexture:intermediate atIndex:0];
                    [encoder setTexture:grayTexture atIndex:1];
                    [encoder setBytes:&sp length:sizeof(sp) atIndex:0];
                    const NSUInteger tw2 = std::min<NSUInteger>(16, scalePipeline.maxTotalThreadsPerThreadgroup);
                    const NSUInteger th2 = std::max<NSUInteger>(1, std::min<NSUInteger>(16, scalePipeline.maxTotalThreadsPerThreadgroup / tw2));
                    [encoder dispatchThreads:MTLSizeMake(static_cast<NSUInteger>(options.width),
                                                        static_cast<NSUInteger>(targetHeight), 1)
                       threadsPerThreadgroup:MTLSizeMake(tw2, th2, 1)];
                    [encoder endEncoding];
                    [cb commit];
                    [cb waitUntilCompleted];
                    if (cb.status == MTLCommandBufferStatusError) {
                        const std::string message = cb.error != nil ? nsError(cb.error) : "unknown Metal command failure";
                        CFRelease(cvTexture);
                        CFRelease(sample);
                        throw std::runtime_error("Metal frame preprocessing failed: " + message);
                    }

                    [grayTexture getBytes:gray.data()
                              bytesPerRow:static_cast<NSUInteger>(options.width)
                               fromRegion:MTLRegionMake2D(0, 0,
                                                         static_cast<NSUInteger>(options.width),
                                                         static_cast<NSUInteger>(targetHeight))
                              mipmapLevel:0];
                    raw.write(reinterpret_cast<const char*>(gray.data()),
                              static_cast<std::streamsize>(gray.size()));
                    previous = gray;
                    ++outputCount;
                    ++inputIndex;
                    CFRelease(cvTexture);
                    CFRelease(sample);
                }
            }

            if (textureCache != nullptr) CFRelease(textureCache);
            raw.close();
            if (outputCount == 0) throw std::runtime_error("no frames were emitted");

            const char* deviceUtf8 = [[device name] UTF8String];
            writeMeta(options, options.width, targetHeight, outputFps, outputCount,
                      deviceUtf8 != nullptr ? deviceUtf8 : "Metal device",
                      sourceFps, sourceWidth, sourceHeight);
            std::cout << "VALID Metal IRIS preprocessing\\n"
                      << "DEVICE " << (deviceUtf8 != nullptr ? deviceUtf8 : "Metal device") << "\\n"
                      << "SOURCE " << sourceWidth << "x" << sourceHeight << "@" << sourceFps << "\\n"
                      << "OUTPUT " << options.width << "x" << targetHeight << "@" << outputFps
                      << " frames=" << outputCount << "\\n"
                      << "KIND " << options.kind << " VALUE " << options.value << "\\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR " << e.what() << "\\n";
        return 1;
    }
}
