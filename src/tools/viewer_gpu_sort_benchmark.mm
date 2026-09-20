#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "vulkax/viewer/metal_gpu_sorter.hpp"
#include "vulkax/viewer/visibility.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <simd/simd.h>

namespace {

using vulkax::math::Vec3;
using vulkax::viewer::ViewerGaussian;

struct GPUSplat {
    simd_float4 positionScaleX{};
    simd_float4 scaleYZOpacity{};
    simd_float4 rotation{};
    simd_float4 colorMark{};
};
static_assert(sizeof(GPUSplat) == 64U);

struct Row {
    std::size_t source{};
    std::size_t retained{};
    bool parity{};
    double cpuFull{};
    double cpuFullP95{};
    double cpuCull{};
    double cpuCullP95{};
    double gpuWall{};
    double gpuWallP95{};
    double gpuDevice{};
    double gpuDeviceP95{};
    double hybrid{};
    double hybridP95{};
    double ratio{};
};

struct Options {
    std::vector<std::size_t> sizes{10000U, 50000U, 100000U, 200000U};
    unsigned repeats{5U};
    std::filesystem::path json;
    std::filesystem::path markdown;
};

double ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

double quantile(std::vector<double> values, double q) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double x = q * static_cast<double>(values.size() - 1U);
    const auto a = static_cast<std::size_t>(std::floor(x));
    const auto b = static_cast<std::size_t>(std::ceil(x));
    if (a == b) return values[a];
    const double f = x - static_cast<double>(a);
    return values[a] * (1.0 - f) + values[b] * f;
}

std::vector<std::size_t> parseSizes(std::string_view value) {
    std::vector<std::size_t> out;
    std::size_t start = 0U;
    while (start < value.size()) {
        const auto comma = value.find(',', start);
        const auto token = value.substr(start, comma == std::string_view::npos
            ? value.size() - start : comma - start);
        const auto count = std::stoull(std::string(token));
        if (count == 0U || count > 1000000ULL)
            throw std::runtime_error("benchmark size must be in 1..1000000");
        out.push_back(static_cast<std::size_t>(count));
        if (comma == std::string_view::npos) break;
        start = comma + 1U;
    }
    return out;
}

Options parseArgs(int argc, const char* argv[]) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* flag) {
            if (i + 1 >= argc) throw std::runtime_error(std::string(flag) + " requires a value");
            return std::string(argv[++i]);
        };
        if (arg == "--sizes") o.sizes = parseSizes(next("--sizes"));
        else if (arg == "--repeats") {
            const auto n = std::stoul(next("--repeats"));
            if (n < 2U || n > 50U) throw std::runtime_error("--repeats must be in 2..50");
            o.repeats = static_cast<unsigned>(n);
        } else if (arg == "--json") o.json = next("--json");
        else if (arg == "--markdown") o.markdown = next("--markdown");
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: vulkax_viewer_benchmark [--sizes 10000,50000,100000,200000] "
                         "[--repeats 5] [--json out.json] [--markdown out.md]\n";
            std::exit(0);
        } else throw std::runtime_error("unknown argument: " + arg);
    }
    if (o.sizes.empty()) throw std::runtime_error("no benchmark sizes");
    return o;
}

std::vector<ViewerGaussian> makeScene(std::size_t count) {
    std::vector<ViewerGaussian> scene;
    scene.reserve(count);
    constexpr double golden = 2.39996322972865332;
    for (std::size_t i = 0U; i < count; ++i) {
        const double t = (static_cast<double>(i) + 0.5) / static_cast<double>(count);
        const double radial = 0.72 * std::sqrt(
            std::fmod(static_cast<double>(i) * 0.6180339887498949, 1.0));
        const double angle = golden * static_cast<double>(i);
        ViewerGaussian g;
        g.position = {radial * std::cos(angle), radial * std::sin(angle), -1.0 + 2.0 * t};
        g.scale = {0.0035F, 0.0030F, 0.0025F};
        g.opacity = 0.91F;
        g.id = {0x42454E43U, static_cast<std::uint32_t>(i + 1U)};
        scene.push_back(g);
    }
    return scene;
}

std::vector<GPUSplat> makeGpuScene(const std::vector<ViewerGaussian>& scene) {
    std::vector<GPUSplat> out;
    out.reserve(scene.size());
    for (const auto& g : scene) {
        GPUSplat s;
        s.positionScaleX = {(float)g.position.x, (float)g.position.y, (float)g.position.z, g.scale[0]};
        s.scaleYZOpacity = {g.scale[1], g.scale[2], g.opacity, 0.0F};
        s.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
        s.colorMark = {0.6F, 0.75F, 0.95F, 0.0F};
        out.push_back(s);
    }
    return out;
}

Row runSize(id<MTLDevice> device,
            const vulkax::viewer::MetalGpuSorter& sorter,
            std::size_t count,
            unsigned repeats) {
    const auto scene = makeScene(count);
    const auto gpu = makeGpuScene(scene);
    id<MTLBuffer> buffer = [device newBufferWithBytes:gpu.data()
                                               length:gpu.size() * sizeof(GPUSplat)
                                              options:MTLResourceStorageModeShared];
    if (!buffer) throw std::runtime_error("Metal benchmark buffer allocation failed");

    vulkax::viewer::ViewerCamera camera;
    camera.position = {0.0, 0.0, 4.0};
    camera.target = {0.0, 0.0, 0.0};
    camera.verticalFovRadians = 0.7853981633974483;
    camera.aspect = 16.0 / 9.0;
    camera.nearPlane = 0.01;
    camera.farPlane = 10.0;
    const Vec3 forward = vulkax::math::normalized(camera.target - camera.position);

    vulkax::viewer::VisibilitySettings sorted;
    sorted.maxSplats = count;
    sorted.minimumOpacity = 0.001;
    sorted.sortBackToFront = true;
    auto unsorted = sorted;
    unsorted.sortBackToFront = false;

    const auto warmRef = vulkax::viewer::selectVisibleGaussians(scene, camera, sorted);
    const auto warmRet = vulkax::viewer::selectVisibleGaussians(scene, camera, unsorted);
    const auto warmGpu = sorter.sort(buffer, warmRet.order, camera.position, forward);
    if (!warmGpu.success || warmGpu.order != warmRef.order)
        throw std::runtime_error("benchmark warm-up parity failed");

    std::vector<double> cpuFull, cpuCull, gpuWall, gpuDevice, hybrid;
    bool parity = true;
    std::size_t retained = 0U;

    for (unsigned r = 0U; r < repeats; ++r) {
        auto begin = std::chrono::steady_clock::now();
        const auto reference = vulkax::viewer::selectVisibleGaussians(scene, camera, sorted);
        cpuFull.push_back(ms(begin));

        const auto hybridBegin = std::chrono::steady_clock::now();
        begin = hybridBegin;
        const auto retainedSet = vulkax::viewer::selectVisibleGaussians(scene, camera, unsorted);
        cpuCull.push_back(ms(begin));
        retained = retainedSet.order.size();

        begin = std::chrono::steady_clock::now();
        const auto gpuResult = sorter.sort(buffer, retainedSet.order, camera.position, forward);
        gpuWall.push_back(ms(begin));
        hybrid.push_back(ms(hybridBegin));
        gpuDevice.push_back(gpuResult.gpuSeconds * 1000.0);

        if (!gpuResult.success || gpuResult.order != reference.order) parity = false;
    }

    Row row;
    row.source = count;
    row.retained = retained;
    row.parity = parity;
    row.cpuFull = quantile(cpuFull, 0.5);
    row.cpuFullP95 = quantile(cpuFull, 0.95);
    row.cpuCull = quantile(cpuCull, 0.5);
    row.cpuCullP95 = quantile(cpuCull, 0.95);
    row.gpuWall = quantile(gpuWall, 0.5);
    row.gpuWallP95 = quantile(gpuWall, 0.95);
    row.gpuDevice = quantile(gpuDevice, 0.5);
    row.gpuDeviceP95 = quantile(gpuDevice, 0.95);
    row.hybrid = quantile(hybrid, 0.5);
    row.hybridP95 = quantile(hybrid, 0.95);
    row.ratio = row.hybrid > 0.0 ? row.cpuFull / row.hybrid : 0.0;
    return row;
}

std::string escapeJson(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (c == '\\' || c == '"') out.push_back('\\');
        if (c == '\n') { out += "\\n"; continue; }
        out.push_back(c);
    }
    return out;
}

void writeJson(const std::filesystem::path& path,
               std::string_view device,
               std::string_view os,
               unsigned repeats,
               const std::vector<Row>& rows) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) throw std::runtime_error("could not write benchmark JSON");
    out << std::fixed << std::setprecision(6);
    out << "{\n  \"schema\":\"vulkax.native_viewer.research_checkpoint.v1\",\n"
        << "  \"device\":\"" << escapeJson(device) << "\",\n"
        << "  \"os\":\"" << escapeJson(os) << "\",\n"
        << "  \"repeats\":" << repeats << ",\n  \"rows\":[\n";
    for (std::size_t i = 0U; i < rows.size(); ++i) {
        const auto& r = rows[i];
        out << "    {\"source_count\":" << r.source
            << ",\"retained_count\":" << r.retained
            << ",\"parity\":" << (r.parity ? "true" : "false")
            << ",\"cpu_sorted_median_ms\":" << r.cpuFull
            << ",\"cpu_sorted_p95_ms\":" << r.cpuFullP95
            << ",\"cpu_cull_lod_median_ms\":" << r.cpuCull
            << ",\"cpu_cull_lod_p95_ms\":" << r.cpuCullP95
            << ",\"gpu_sort_wall_median_ms\":" << r.gpuWall
            << ",\"gpu_sort_wall_p95_ms\":" << r.gpuWallP95
            << ",\"gpu_device_median_ms\":" << r.gpuDevice
            << ",\"gpu_device_p95_ms\":" << r.gpuDeviceP95
            << ",\"hybrid_median_ms\":" << r.hybrid
            << ",\"hybrid_p95_ms\":" << r.hybridP95
            << ",\"speedup_vs_cpu\":" << r.ratio << "}"
            << (i + 1U == rows.size() ? "" : ",") << '\n';
    }
    out << "  ]\n}\n";
}

void writeMarkdown(const std::filesystem::path& path,
                   std::string_view device,
                   std::string_view os,
                   unsigned repeats,
                   const std::vector<Row>& rows) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) throw std::runtime_error("could not write benchmark Markdown");
    out << "# Vulkax native viewer research checkpoint benchmark\n\n"
        << "- Device: " << device << "\n"
        << "- OS: " << os << "\n"
        << "- Repeats per size: " << repeats << "\n"
        << "- CPU baseline: visibility + importance LOD + final CPU depth sort.\n"
        << "- Hybrid path: same visibility/LOD without final CPU sort, then validated Metal sorting.\n"
        << "- Metal wall time includes allocation, command encoding, synchronization, readback and sanity validation.\n\n"
        << "| Source | Retained | Parity | CPU full ms | CPU cull/LOD ms | Metal wall ms | Metal device ms | Hybrid ms | CPU/Hybrid |\n"
        << "|---:|---:|:---:|---:|---:|---:|---:|---:|---:|\n";
    out << std::fixed << std::setprecision(3);
    for (const auto& r : rows) {
        out << '|' << r.source << '|' << r.retained << '|'
            << (r.parity ? "yes" : "NO") << '|'
            << r.cpuFull << '|' << r.cpuCull << '|' << r.gpuWall << '|'
            << r.gpuDevice << '|' << r.hybrid << '|' << r.ratio << "x|\n";
    }
    out << "\nTiming is hardware-specific; exact CPU/GPU ordering parity is the correctness gate. "
           "This benchmark covers presentation/runtime infrastructure only.\n";
}

} // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        try {
            const auto options = parseArgs(argc, argv);
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device) throw std::runtime_error("no Metal device available");

            vulkax::viewer::MetalGpuSorter sorter(device);
            if (!sorter.available())
                throw std::runtime_error("MetalGpuSorter unavailable: " + sorter.error());

            const std::string deviceName = device.name.UTF8String
                ? std::string(device.name.UTF8String) : "unknown";
            NSString* osString = NSProcessInfo.processInfo.operatingSystemVersionString;
            const std::string os = osString.UTF8String
                ? std::string(osString.UTF8String) : "unknown";

            std::vector<Row> rows;
            bool allParity = true;
            std::cout << "Vulkax native viewer benchmark\n"
                      << "device: " << deviceName << "\nos: " << os << '\n';
            for (const auto count : options.sizes) {
                const auto row = runSize(device, sorter, count, options.repeats);
                rows.push_back(row);
                allParity = allParity && row.parity;
                std::cout << count << " splats parity=" << (row.parity ? "yes" : "NO")
                          << " cpu=" << row.cpuFull << "ms"
                          << " hybrid=" << row.hybrid << "ms"
                          << " gpu_device=" << row.gpuDevice << "ms"
                          << " ratio=" << row.ratio << "x\n";
            }

            if (!options.json.empty())
                writeJson(options.json, deviceName, os, options.repeats, rows);
            if (!options.markdown.empty())
                writeMarkdown(options.markdown, deviceName, os, options.repeats, rows);
            return allParity ? 0 : 2;
        } catch (const std::exception& e) {
            std::cerr << "vulkax_viewer_benchmark: " << e.what() << '\n';
            return 1;
        }
    }
}
