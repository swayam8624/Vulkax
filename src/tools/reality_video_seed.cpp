#include "vulkax/capture/video_track.hpp"
#include "vulkax/world/image_plane_bounce.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string jsonEscape(std::string_view text) {
    std::string result;
    result.reserve(text.size() + 8U);
    for (const char character : text) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += character; break;
        }
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: vulkax_reality_video_seed <video_point_track.csv>\n";
        return 2;
    }

    try {
        const auto track = vulkax::capture::loadVideoPointTrackCsv(argv[1]);
        const auto seed = vulkax::world::estimateImagePlaneBounceSeed(track);
        std::size_t fitSamples = 0;
        std::size_t validationSamples = 0;
        double minimumX = track.samples.front().xPixels;
        double maximumX = minimumX;
        double minimumY = track.samples.front().yPixels;
        double maximumY = minimumY;
        for (const auto& sample : track.samples) {
            if (sample.split == vulkax::capture::VideoTrackSplit::Fit) {
                ++fitSamples;
            } else {
                ++validationSamples;
            }
            minimumX = std::min(minimumX, sample.xPixels);
            maximumX = std::max(maximumX, sample.xPixels);
            minimumY = std::min(minimumY, sample.yPixels);
            maximumY = std::max(maximumY, sample.yPixels);
        }

        std::cout << std::setprecision(17);
        std::cout << "{\n";
        std::cout << "  \"schema\": \"vulkax_image_plane_bounce_seed\",\n";
        std::cout << "  \"version\": 2,\n";
        std::cout << "  \"source\": \"" << jsonEscape(track.source) << "\",\n";
        std::cout << "  \"width_pixels\": " << track.widthPixels << ",\n";
        std::cout << "  \"height_pixels\": " << track.heightPixels << ",\n";
        std::cout << "  \"nominal_fps\": " << track.nominalFps << ",\n";
        std::cout << "  \"sample_count\": " << track.samples.size() << ",\n";
        std::cout << "  \"fit_samples\": " << fitSamples << ",\n";
        std::cout << "  \"validation_samples\": " << validationSamples << ",\n";
        std::cout << "  \"x_span_pixels\": " << maximumX - minimumX << ",\n";
        std::cout << "  \"y_span_pixels\": " << maximumY - minimumY << ",\n";
        std::cout << "  \"initial_x_pixels\": " << seed.initialXPixels << ",\n";
        std::cout << "  \"initial_y_pixels\": " << seed.initialYPixels << ",\n";
        std::cout << "  \"velocity_x_pixels_per_second\": "
                  << seed.velocityXPixelsPerSecond << ",\n";
        std::cout << "  \"velocity_y_pixels_per_second\": "
                  << seed.velocityYPixelsPerSecond << ",\n";
        std::cout << "  \"acceleration_y_pixels_per_second2\": "
                  << seed.accelerationYPixelsPerSecond2 << ",\n";
        std::cout << "  \"restitution_proxy\": " << seed.restitution << ",\n";
        std::cout << "  \"ground_y_pixels\": " << seed.groundYPixels << ",\n";
        std::cout << "  \"release_time_seconds\": " << seed.releaseTimeSeconds << ",\n";
        if (seed.releaseSample.has_value()) {
            std::cout << "  \"release_sample\": " << *seed.releaseSample << ",\n";
        } else {
            std::cout << "  \"release_sample\": null,\n";
        }
        if (seed.firstBounceSample.has_value()) {
            const auto index = *seed.firstBounceSample;
            std::cout << "  \"first_bounce_sample\": " << index << ",\n";
            std::cout << "  \"first_bounce_time_seconds\": "
                      << track.samples.at(index).timeSeconds << ",\n";
        } else {
            std::cout << "  \"first_bounce_sample\": null,\n";
            std::cout << "  \"first_bounce_time_seconds\": null,\n";
        }
        std::cout << "  \"metric_status\": \"image-plane-only; acceleration is pixels/s^2 until camera and scene scale are calibrated\"\n";
        std::cout << "}\n";
    } catch (const std::exception& error) {
        std::cerr << "reality video seed FAILED: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
