#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
           "[--value X] [--seed N] [--width 640] [--max-seconds 14] [--self-test]\\n";
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
        o.width <= 0 || o.maxSeconds <= 0.0) {
        usage();
    }
    const std::string allowed[] = {"clean","noise","blur","frame_drop","fps","occlusion","crop"};
    if (std::find(std::begin(allowed), std::end(allowed), o.kind) == std::end(allowed)) {
        throw std::runtime_error("unsupported corruption kind: " + o.kind);
    }
    return o;
}

std::uint32_t kindId(const std::string& kind) {
    if (kind == "noise") return 1u;
    if (kind == "occlusion") return 2u;
    if (kind == "crop") return 3u;
    return 0u;
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
    std::uint32_t dstWidth;
    std::uint32_t dstHeight;
};

struct BlurParams {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t radius;
    float sigma;
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
    f << "{\n"
      << "  \"schema\": \"vulkax.iris_metal_gray_frames\",\n"
      << "  \"version\": 2,\n"
      << "  \"backend\": \"metal\",\n"
      << "  \"device\": \"" << deviceName << "\",\n"
      << "  \"corruption_space\": \"analysis_resolution_after_decode\",\n"
      << "  \"width\": " << width << ",\n"
      << "  \"height\": " << height << ",\n"
      << "  \"fps\": " << fps << ",\n"
      << "  \"frame_count\": " << frames << ",\n"
      << "  \"source_fps\": " << sourceFps << ",\n"
      << "  \"source_width\": " << sourceWidth << ",\n"
      << "  \"source_height\": " << sourceHeight << ",\n"
      << "  \"kind\": \"" << o.kind << "\",\n"
      << "  \"value\": " << o.value << ",\n"
      << "  \"seed\": " << o.seed << ",\n"
      << "  \"max_seconds\": " << o.maxSeconds << "\n"
      << "}\n";
}

MTLSize threadsFor(id<MTLComputePipelineState> pipeline) {
    const NSUInteger width = std::min<NSUInteger>(16, pipeline.maxTotalThreadsPerThreadgroup);
    const NSUInteger height =
        std::max<NSUInteger>(1, std::min<NSUInteger>(16, pipeline.maxTotalThreadsPerThreadgroup / width));
    return MTLSizeMake(width, height, 1);
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
    uint dstWidth;
    uint dstHeight;
};
struct BlurParams {
    uint width;
    uint height;
    uint radius;
    float sigma;
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

kernel void scale_gray(texture2d<float, access::sample> src [[texture(0)]],
                       texture2d<float, access::write> dst [[texture(1)]],
                       constant ScaleParams& p [[buffer(0)]],
                       uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= p.dstWidth || gid.y >= p.dstHeight) return;
    constexpr sampler s(address::clamp_to_edge, filter::linear, coord::normalized);
    float2 uv = (float2(gid) + 0.5f) / float2(p.dstWidth, p.dstHeight);
    float4 c = src.sample(s, uv);
    float gray = clamp(dot(c.rgb, float3(0.299f, 0.587f, 0.114f)), 0.0f, 1.0f);
    dst.write(float4(gray, 0.0f, 0.0f, 1.0f), gid);
}

kernel void corrupt_gray(texture2d<float, access::sample> src [[texture(0)]],
                         texture2d<float, access::write> dst [[texture(1)]],
                         constant CorruptParams& p [[buffer(0)]],
                         uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= p.width || gid.y >= p.height) return;
    constexpr sampler s(address::clamp_to_edge, filter::linear, coord::normalized);
    float2 uv = (float2(gid) + 0.5f) / float2(p.width, p.height);
    if (p.kind == 3u) {
        float c = clamp(p.value, 0.0f, 0.24f);
        uv = float2(c) + uv * (1.0f - 2.0f * c);
    }
    float gray = src.sample(s, uv).r;
    if (p.kind == 1u) {
        uint base = p.seed ^ (p.frame * 0x9e3779b9u) ^
                    (gid.x * 0x85ebca6bu) ^ (gid.y * 0xc2b2ae35u);
        gray = clamp(gray + (p.value / 255.0f) *
                     gaussian01(base, base ^ 0xa341316cu), 0.0f, 1.0f);
    } else if (p.kind == 2u) {
        float side = sqrt(clamp(p.value, 0.0f, 0.95f));
        float2 d = abs(uv - float2(0.5f));
        if (d.x <= 0.5f * side && d.y <= 0.5f * side) gray = 0.0f;
    }
    dst.write(float4(gray, 0.0f, 0.0f, 1.0f), gid);
}

kernel void blur_horizontal(texture2d<float, access::sample> src [[texture(0)]],
                            texture2d<float, access::write> dst [[texture(1)]],
                            constant BlurParams& p [[buffer(0)]],
                            uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= p.width || gid.y >= p.height) return;
    constexpr sampler s(address::clamp_to_edge, filter::linear, coord::normalized);
    float2 uv = (float2(gid) + 0.5f) / float2(p.width, p.height);
    float inv = 1.0f / (2.0f * p.sigma * p.sigma);
    float acc = 0.0f;
    float sumw = 0.0f;
    for (int dx = -7; dx <= 7; ++dx) {
        if (abs(dx) > int(p.radius)) continue;
        float w = exp(-float(dx * dx) * inv);
        float2 q = uv + float2(float(dx) / float(p.width), 0.0f);
        acc += src.sample(s, q).r * w;
        sumw += w;
    }
    float gray = acc / max(sumw, 1.0e-6f);
    dst.write(float4(gray, 0.0f, 0.0f, 1.0f), gid);
}

kernel void blur_vertical(texture2d<float, access::sample> src [[texture(0)]],
                          texture2d<float, access::write> dst [[texture(1)]],
                          constant BlurParams& p [[buffer(0)]],
                          uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= p.width || gid.y >= p.height) return;
    constexpr sampler s(address::clamp_to_edge, filter::linear, coord::normalized);
    float2 uv = (float2(gid) + 0.5f) / float2(p.width, p.height);
    float inv = 1.0f / (2.0f * p.sigma * p.sigma);
    float acc = 0.0f;
    float sumw = 0.0f;
    for (int dy = -7; dy <= 7; ++dy) {
        if (abs(dy) > int(p.radius)) continue;
        float w = exp(-float(dy * dy) * inv);
        float2 q = uv + float2(0.0f, float(dy) / float(p.height));
        acc += src.sample(s, q).r * w;
        sumw += w;
    }
    float gray = acc / max(sumw, 1.0e-6f);
    dst.write(float4(gray, 0.0f, 0.0f, 1.0f), gid);
}
)metal";

            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:shaderSource options:nil error:&error];
            if (library == nil) throw std::runtime_error("Metal shader compilation failed: " + nsError(error));

            auto makePipeline = [&](NSString* name) -> id<MTLComputePipelineState> {
                id<MTLFunction> function = [library newFunctionWithName:name];
                if (function == nil) throw std::runtime_error("Metal function not found");
                NSError* pipelineError = nil;
                id<MTLComputePipelineState> pipeline =
                    [device newComputePipelineStateWithFunction:function error:&pipelineError];
                if (pipeline == nil)
                    throw std::runtime_error("Metal pipeline failed: " + nsError(pipelineError));
                return pipeline;
            };

            id<MTLComputePipelineState> scalePipeline = makePipeline(@"scale_gray");
            id<MTLComputePipelineState> corruptPipeline = makePipeline(@"corrupt_gray");
            id<MTLComputePipelineState> blurHPipeline = makePipeline(@"blur_horizontal");
            id<MTLComputePipelineState> blurVPipeline = makePipeline(@"blur_vertical");

            if (options.selfTest) {
                const char* name = [[device name] UTF8String];
                std::cout << "VALID Metal IRIS preprocessor self-test\\n"
                          << "DEVICE " << (name != nullptr ? name : "Metal device") << "\\n";
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
            if (sourceWidth <= 0 || sourceHeight <= 0)
                throw std::runtime_error("invalid video dimensions");
            const double sourceFps =
                track.nominalFrameRate > 0.0f ? static_cast<double>(track.nominalFrameRate) : 60.0;
            const int targetHeight = std::max(
                1,
                static_cast<int>(std::lround(
                    static_cast<double>(sourceHeight) * static_cast<double>(options.width) /
                    static_cast<double>(sourceWidth))));

            AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
            if (reader == nil) throw std::runtime_error("AVAssetReader creation failed: " + nsError(error));
            NSDictionary* settings = @{
                (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                (id)kCVPixelBufferMetalCompatibilityKey : @YES
            };
            AVAssetReaderTrackOutput* output =
                [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:settings];
            output.alwaysCopiesSampleData = NO;
            if (![reader canAddOutput:output])
                throw std::runtime_error("cannot add AVAssetReader output");
            [reader addOutput:output];
            if (![reader startReading])
                throw std::runtime_error("AVAssetReader failed to start: " + nsError(reader.error));

            CVMetalTextureCacheRef textureCache = nullptr;
            const CVReturn cvStatus =
                CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &textureCache);
            if (cvStatus != kCVReturnSuccess || textureCache == nullptr)
                throw std::runtime_error("CVMetalTextureCacheCreate failed");

            auto makeGrayTexture = [&](MTLStorageMode mode) -> id<MTLTexture> {
                MTLTextureDescriptor* d =
                    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                       width:static_cast<NSUInteger>(options.width)
                                                                      height:static_cast<NSUInteger>(targetHeight)
                                                                   mipmapped:NO];
                d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
                d.storageMode = mode;
                id<MTLTexture> texture = [device newTextureWithDescriptor:d];
                if (texture == nil) throw std::runtime_error("failed to allocate Metal grayscale texture");
                return texture;
            };

            id<MTLTexture> baseGray = makeGrayTexture(MTLStorageModePrivate);
            id<MTLTexture> blurTemp = makeGrayTexture(MTLStorageModePrivate);
            id<MTLTexture> outputGray = makeGrayTexture(MTLStorageModeShared);

            std::ofstream raw(options.output, std::ios::binary);
            if (!raw) throw std::runtime_error("cannot open raw output: " + options.output);
            std::vector<std::uint8_t> gray(
                static_cast<std::size_t>(options.width) * static_cast<std::size_t>(targetHeight));
            std::vector<std::uint8_t> previous;

            const int fpsStep = options.kind == "fps"
                ? std::max(1, static_cast<int>(std::lround(sourceFps / std::max(1.0, options.value))))
                : 1;
            const double outputFps = sourceFps / static_cast<double>(fpsStep);
            const int dropPeriod = options.kind == "frame_drop"
                ? std::max(2, static_cast<int>(
                    std::lround(1.0 / std::max(1.0e-6, options.value))))
                : 0;
            const bool isBlur = options.kind == "blur";
            const int blurRadius = isBlur
                ? std::clamp(static_cast<int>(std::lround((options.value - 1.0) * 0.5)), 1, 7)
                : 1;
            const float blurSigma = static_cast<float>(
                std::max(0.8, 0.3 * (static_cast<double>(blurRadius) - 1.0) + 0.8));

            std::uint64_t inputIndex = 0;
            std::uint64_t outputCount = 0;
            const MTLSize grid =
                MTLSizeMake(static_cast<NSUInteger>(options.width),
                            static_cast<NSUInteger>(targetHeight), 1);

            while (reader.status == AVAssetReaderStatusReading) {
                @autoreleasepool {
                    CMSampleBufferRef sample = [output copyNextSampleBuffer];
                    if (sample == nullptr) break;
                    const double seconds =
                        CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample));
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
                    const std::size_t pixelWidth = CVPixelBufferGetWidth(pixel);
                    const std::size_t pixelHeight = CVPixelBufferGetHeight(pixel);
                    CVMetalTextureRef cvTexture = nullptr;
                    CVReturn textureStatus = CVMetalTextureCacheCreateTextureFromImage(
                        kCFAllocatorDefault, textureCache, pixel, nullptr,
                        MTLPixelFormatBGRA8Unorm, pixelWidth, pixelHeight, 0, &cvTexture);
                    if (textureStatus != kCVReturnSuccess || cvTexture == nullptr) {
                        CFRelease(sample);
                        throw std::runtime_error("CVMetalTextureCacheCreateTextureFromImage failed");
                    }
                    id<MTLTexture> sourceTexture = CVMetalTextureGetTexture(cvTexture);
                    if (sourceTexture == nil) {
                        CFRelease(cvTexture);
                        CFRelease(sample);
                        throw std::runtime_error("failed to acquire source Metal texture");
                    }

                    ScaleParams sp{
                        static_cast<std::uint32_t>(options.width),
                        static_cast<std::uint32_t>(targetHeight)
                    };
                    CorruptParams cp{
                        static_cast<std::uint32_t>(options.width),
                        static_cast<std::uint32_t>(targetHeight),
                        kindId(options.kind),
                        static_cast<float>(options.value),
                        options.seed,
                        static_cast<std::uint32_t>(outputCount)
                    };
                    BlurParams bp{
                        static_cast<std::uint32_t>(options.width),
                        static_cast<std::uint32_t>(targetHeight),
                        static_cast<std::uint32_t>(blurRadius),
                        blurSigma
                    };

                    id<MTLCommandBuffer> cb = [queue commandBuffer];
                    id<MTLComputeCommandEncoder> encoder = [cb computeCommandEncoder];

                    [encoder setComputePipelineState:scalePipeline];
                    [encoder setTexture:sourceTexture atIndex:0];
                    [encoder setTexture:baseGray atIndex:1];
                    [encoder setBytes:&sp length:sizeof(sp) atIndex:0];
                    [encoder dispatchThreads:grid threadsPerThreadgroup:threadsFor(scalePipeline)];

                    if (isBlur) {
                        [encoder setComputePipelineState:blurHPipeline];
                        [encoder setTexture:baseGray atIndex:0];
                        [encoder setTexture:blurTemp atIndex:1];
                        [encoder setBytes:&bp length:sizeof(bp) atIndex:0];
                        [encoder dispatchThreads:grid threadsPerThreadgroup:threadsFor(blurHPipeline)];

                        [encoder setComputePipelineState:blurVPipeline];
                        [encoder setTexture:blurTemp atIndex:0];
                        [encoder setTexture:outputGray atIndex:1];
                        [encoder setBytes:&bp length:sizeof(bp) atIndex:0];
                        [encoder dispatchThreads:grid threadsPerThreadgroup:threadsFor(blurVPipeline)];
                    } else {
                        [encoder setComputePipelineState:corruptPipeline];
                        [encoder setTexture:baseGray atIndex:0];
                        [encoder setTexture:outputGray atIndex:1];
                        [encoder setBytes:&cp length:sizeof(cp) atIndex:0];
                        [encoder dispatchThreads:grid threadsPerThreadgroup:threadsFor(corruptPipeline)];
                    }

                    [encoder endEncoding];
                    [cb commit];
                    [cb waitUntilCompleted];
                    if (cb.status == MTLCommandBufferStatusError) {
                        const std::string message =
                            cb.error != nil ? nsError(cb.error) : "unknown Metal command failure";
                        CFRelease(cvTexture);
                        CFRelease(sample);
                        throw std::runtime_error("Metal frame preprocessing failed: " + message);
                    }

                    [outputGray getBytes:gray.data()
                            bytesPerRow:static_cast<NSUInteger>(options.width)
                             fromRegion:MTLRegionMake2D(
                                 0, 0,
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
            writeMeta(
                options, options.width, targetHeight, outputFps, outputCount,
                deviceUtf8 != nullptr ? deviceUtf8 : "Metal device",
                sourceFps, sourceWidth, sourceHeight);

            std::cout << "VALID Metal IRIS preprocessing\\n"
                      << "DEVICE " << (deviceUtf8 != nullptr ? deviceUtf8 : "Metal device") << "\\n"
                      << "SOURCE " << sourceWidth << "x" << sourceHeight << "@" << sourceFps << "\\n"
                      << "OUTPUT " << options.width << "x" << targetHeight << "@" << outputFps
                      << " frames=" << outputCount << "\\n"
                      << "CORRUPTION_SPACE analysis_resolution_after_decode\\n"
                      << "KIND " << options.kind << " VALUE " << options.value << "\\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR " << e.what() << "\\n";
        return 1;
    }
}
