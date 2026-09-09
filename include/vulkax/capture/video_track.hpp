#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace vulkax::capture {

enum class VideoTrackSplit : std::uint8_t { Fit, Validation };

[[nodiscard]] constexpr const char* toString(VideoTrackSplit split) noexcept {
    switch (split) {
        case VideoTrackSplit::Fit: return "fit";
        case VideoTrackSplit::Validation: return "validation";
    }
    return "unknown";
}

struct VideoPointTrackSample {
    std::uint64_t frameIndex{};
    double timeSeconds{};
    double xPixels{};
    double yPixels{};
    double confidence{1.0};
    VideoTrackSplit split{VideoTrackSplit::Fit};
};

struct VideoPointTrack {
    std::string source;
    std::uint32_t widthPixels{};
    std::uint32_t heightPixels{};
    double nominalFps{};
    std::vector<VideoPointTrackSample> samples;
};

namespace detail {

inline void stripVideoTrackCarriageReturn(std::string& line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
}

[[nodiscard]] inline std::string_view videoTrackMetadataValue(
    std::string_view line, std::string_view key) {
    const std::string prefix = "# " + std::string(key) + "=";
    if (!line.starts_with(prefix)) return {};
    return line.substr(prefix.size());
}

[[nodiscard]] inline std::uint64_t parseVideoTrackUnsigned(
    std::string_view text, const char* label) {
    std::uint64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument(std::string("invalid video-track ") + label);
    }
    return value;
}

[[nodiscard]] inline double parseVideoTrackDouble(
    std::string_view text, const char* label) {
    std::string owned(text);
    char* end = nullptr;
    const double value = std::strtod(owned.c_str(), &end);
    if (end != owned.c_str() + owned.size() || !std::isfinite(value)) {
        throw std::invalid_argument(std::string("invalid video-track ") + label);
    }
    return value;
}

[[nodiscard]] inline std::vector<std::string_view> splitVideoTrackCsv(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t begin = 0;
    while (begin <= line.size()) {
        const std::size_t comma = line.find(',', begin);
        if (comma == std::string_view::npos) {
            fields.push_back(line.substr(begin));
            break;
        }
        fields.push_back(line.substr(begin, comma - begin));
        begin = comma + 1;
    }
    return fields;
}

[[nodiscard]] inline VideoTrackSplit parseVideoTrackSplit(std::string_view text) {
    if (text == "fit") return VideoTrackSplit::Fit;
    if (text == "validation") return VideoTrackSplit::Validation;
    throw std::invalid_argument("video-track split must be fit or validation");
}

} // namespace detail

// Exact text contract written by scripts/extract_video_track.py:
//   # vulkax_video_point_track_v1
//   # source=<provenance URI or stable source id>
//   # width_pixels=<positive integer>
//   # height_pixels=<positive integer>
//   # nominal_fps=<positive floating-point value>
//   frame_index,time_seconds,x_pixels,y_pixels,confidence,split
//   ...
//
// Coordinates are pixel-center coordinates in the extracted frame space. The
// parser rejects malformed metadata, non-monotonic time/frame order, points
// outside the declared image, confidence outside (0, 1], and unknown splits.
[[nodiscard]] inline VideoPointTrack loadVideoPointTrackCsv(const std::filesystem::path& path) {
    constexpr std::string_view signature = "# vulkax_video_point_track_v1";
    constexpr std::string_view header =
        "frame_index,time_seconds,x_pixels,y_pixels,confidence,split";

    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("failed to open video point-track CSV");

    std::string line;
    if (!std::getline(stream, line)) throw std::invalid_argument("video-track file is empty");
    detail::stripVideoTrackCarriageReturn(line);
    if (line != signature) throw std::invalid_argument("unexpected video-track schema signature");

    VideoPointTrack result;
    bool haveSource = false;
    bool haveWidth = false;
    bool haveHeight = false;
    bool haveFps = false;
    bool haveHeader = false;

    while (std::getline(stream, line)) {
        detail::stripVideoTrackCarriageReturn(line);
        if (line.empty()) continue;
        if (line == header) {
            haveHeader = true;
            break;
        }
        if (!line.starts_with("# ")) {
            throw std::invalid_argument("video-track metadata must precede the CSV header");
        }
        if (const auto value = detail::videoTrackMetadataValue(line, "source"); !value.empty()) {
            if (haveSource) throw std::invalid_argument("duplicate video-track source metadata");
            result.source = std::string(value);
            haveSource = true;
            continue;
        }
        if (const auto value = detail::videoTrackMetadataValue(line, "width_pixels"); !value.empty()) {
            if (haveWidth) throw std::invalid_argument("duplicate video-track width metadata");
            const auto parsed = detail::parseVideoTrackUnsigned(value, "width_pixels");
            if (parsed == 0 || parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("video-track width_pixels is out of range");
            }
            result.widthPixels = static_cast<std::uint32_t>(parsed);
            haveWidth = true;
            continue;
        }
        if (const auto value = detail::videoTrackMetadataValue(line, "height_pixels"); !value.empty()) {
            if (haveHeight) throw std::invalid_argument("duplicate video-track height metadata");
            const auto parsed = detail::parseVideoTrackUnsigned(value, "height_pixels");
            if (parsed == 0 || parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("video-track height_pixels is out of range");
            }
            result.heightPixels = static_cast<std::uint32_t>(parsed);
            haveHeight = true;
            continue;
        }
        if (const auto value = detail::videoTrackMetadataValue(line, "nominal_fps"); !value.empty()) {
            if (haveFps) throw std::invalid_argument("duplicate video-track fps metadata");
            result.nominalFps = detail::parseVideoTrackDouble(value, "nominal_fps");
            if (!(result.nominalFps > 0.0)) {
                throw std::invalid_argument("video-track nominal_fps must be positive");
            }
            haveFps = true;
            continue;
        }
        throw std::invalid_argument("unknown video-track metadata line");
    }

    if (!haveSource || result.source.empty() || !haveWidth || !haveHeight || !haveFps || !haveHeader) {
        throw std::invalid_argument("video-track metadata/header is incomplete");
    }

    bool first = true;
    std::uint64_t previousFrame{};
    double previousTime{};
    while (std::getline(stream, line)) {
        detail::stripVideoTrackCarriageReturn(line);
        if (line.empty()) continue;
        const auto fields = detail::splitVideoTrackCsv(line);
        if (fields.size() != 6U) {
            throw std::invalid_argument("video-track row must contain exactly six fields");
        }

        VideoPointTrackSample sample;
        sample.frameIndex = detail::parseVideoTrackUnsigned(fields[0], "frame_index");
        sample.timeSeconds = detail::parseVideoTrackDouble(fields[1], "time_seconds");
        sample.xPixels = detail::parseVideoTrackDouble(fields[2], "x_pixels");
        sample.yPixels = detail::parseVideoTrackDouble(fields[3], "y_pixels");
        sample.confidence = detail::parseVideoTrackDouble(fields[4], "confidence");
        sample.split = detail::parseVideoTrackSplit(fields[5]);

        if (sample.timeSeconds < 0.0) {
            throw std::invalid_argument("video-track time must be non-negative");
        }
        if (!(sample.confidence > 0.0 && sample.confidence <= 1.0)) {
            throw std::invalid_argument("video-track confidence must lie in (0, 1]");
        }
        if (sample.xPixels < 0.0 || sample.xPixels >= static_cast<double>(result.widthPixels) ||
            sample.yPixels < 0.0 || sample.yPixels >= static_cast<double>(result.heightPixels)) {
            throw std::invalid_argument("video-track point lies outside declared frame bounds");
        }
        if (!first && (sample.frameIndex <= previousFrame || sample.timeSeconds <= previousTime)) {
            throw std::invalid_argument("video-track frame/time order must be strictly increasing");
        }
        first = false;
        previousFrame = sample.frameIndex;
        previousTime = sample.timeSeconds;
        result.samples.push_back(sample);
    }

    if (result.samples.empty()) throw std::invalid_argument("video-track contains no point samples");
    return result;
}

} // namespace vulkax::capture
