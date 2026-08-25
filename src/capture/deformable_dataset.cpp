#include "vulkax/capture/deformable_dataset.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace vulkax::capture {
namespace {

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) fields.push_back(trim(field));
    if (!line.empty() && line.back() == ',') fields.emplace_back();
    return fields;
}

[[nodiscard]] double parseDouble(const std::string& text, const char* label, std::size_t lineNumber) {
    std::size_t consumed = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " is not numeric at CSV line " + std::to_string(lineNumber));
    }
    if (consumed != text.size() || !std::isfinite(value))
        throw std::invalid_argument(std::string(label) + " is not finite at CSV line " + std::to_string(lineNumber));
    return value;
}

[[nodiscard]] std::uint64_t parseId(const std::string& text, const char* label, std::size_t lineNumber) {
    if (text.empty() || text.front() == '-')
        throw std::invalid_argument(std::string(label) + " is not an unsigned integer at CSV line " +
                                    std::to_string(lineNumber));
    std::size_t consumed = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(text, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " is not an unsigned integer at CSV line " + std::to_string(lineNumber));
    }
    if (consumed != text.size() || value == 0ULL)
        throw std::invalid_argument(std::string(label) + " is invalid at CSV line " + std::to_string(lineNumber));
    return static_cast<std::uint64_t>(value);
}

[[nodiscard]] ObservationSplit parseSplit(const std::string& text, std::size_t lineNumber) {
    if (text == "fit") return ObservationSplit::Fit;
    if (text == "validation") return ObservationSplit::Validation;
    throw std::invalid_argument("observation split must be fit or validation at CSV line " + std::to_string(lineNumber));
}

[[nodiscard]] std::vector<std::pair<std::size_t, std::string>> meaningfulLines(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured deformable CSV: " + path.string());
    std::vector<std::pair<std::size_t, std::string>> lines;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        const std::string cleaned = trim(line);
        if (cleaned.empty() || cleaned.front() == '#') continue;
        lines.emplace_back(lineNumber, cleaned);
    }
    if (!stream.eof()) throw std::runtime_error("failed while reading captured deformable CSV: " + path.string());
    return lines;
}

} // namespace

std::vector<CapturedParticleSpec> loadCapturedParticlesCsv(const std::filesystem::path& path) {
    const auto lines = meaningfulLines(path);
    if (lines.empty()) throw std::invalid_argument("captured particle CSV is empty");
    constexpr std::string_view expectedHeader = "particle_id,rest_x,rest_y,rest_z,mass,rest_volume";
    if (lines.front().second != expectedHeader)
        throw std::invalid_argument("captured particle CSV header must be: " + std::string(expectedHeader));

    std::vector<CapturedParticleSpec> result;
    result.reserve(lines.size() - 1U);
    std::unordered_set<std::uint64_t> ids;
    for (std::size_t row = 1; row < lines.size(); ++row) {
        const auto& [lineNumber, line] = lines[row];
        const auto fields = splitCsv(line);
        if (fields.size() != 6U)
            throw std::invalid_argument("captured particle CSV row must contain six fields at line " + std::to_string(lineNumber));
        CapturedParticleSpec particle;
        particle.particleId = parseId(fields[0], "particle_id", lineNumber);
        particle.restPosition = {
            parseDouble(fields[1], "rest_x", lineNumber),
            parseDouble(fields[2], "rest_y", lineNumber),
            parseDouble(fields[3], "rest_z", lineNumber),
        };
        particle.mass = parseDouble(fields[4], "mass", lineNumber);
        particle.restVolume = parseDouble(fields[5], "rest_volume", lineNumber);
        if (!(particle.mass > 0.0) || !(particle.restVolume > 0.0))
            throw std::invalid_argument("captured particle mass and rest_volume must be positive at line " +
                                        std::to_string(lineNumber));
        if (!ids.insert(particle.particleId).second)
            throw std::invalid_argument("duplicate captured particle_id at line " + std::to_string(lineNumber));
        result.push_back(particle);
    }
    if (result.size() < 4U) throw std::invalid_argument("captured deformable dataset requires at least four particles");
    return result;
}

std::vector<CapturedMarkerObservation> loadCapturedMarkerObservationsCsv(const std::filesystem::path& path) {
    const auto lines = meaningfulLines(path);
    if (lines.empty()) throw std::invalid_argument("captured marker observation CSV is empty");
    constexpr std::string_view expectedHeader = "marker_id,particle_id,time,x,y,z,split";
    if (lines.front().second != expectedHeader)
        throw std::invalid_argument("captured marker CSV header must be: " + std::string(expectedHeader));

    std::vector<CapturedMarkerObservation> result;
    result.reserve(lines.size() - 1U);
    std::set<std::pair<std::string, double>> markerTimes;
    for (std::size_t row = 1; row < lines.size(); ++row) {
        const auto& [lineNumber, line] = lines[row];
        const auto fields = splitCsv(line);
        if (fields.size() != 7U)
            throw std::invalid_argument("captured marker CSV row must contain seven fields at line " + std::to_string(lineNumber));
        CapturedMarkerObservation observation;
        observation.markerId = fields[0];
        if (observation.markerId.empty())
            throw std::invalid_argument("marker_id cannot be empty at CSV line " + std::to_string(lineNumber));
        observation.particleId = parseId(fields[1], "particle_id", lineNumber);
        observation.time = parseDouble(fields[2], "time", lineNumber);
        if (observation.time < 0.0)
            throw std::invalid_argument("captured marker time cannot be negative at line " + std::to_string(lineNumber));
        observation.position = {
            parseDouble(fields[3], "x", lineNumber),
            parseDouble(fields[4], "y", lineNumber),
            parseDouble(fields[5], "z", lineNumber),
        };
        observation.split = parseSplit(fields[6], lineNumber);
        if (!markerTimes.emplace(observation.markerId, observation.time).second)
            throw std::invalid_argument("duplicate marker_id/time pair at CSV line " + std::to_string(lineNumber));
        result.push_back(std::move(observation));
    }
    if (result.empty()) throw std::invalid_argument("captured marker CSV contains no observations");
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.time != rhs.time) return lhs.time < rhs.time;
        if (lhs.markerId != rhs.markerId) return lhs.markerId < rhs.markerId;
        return lhs.particleId < rhs.particleId;
    });
    return result;
}

CapturedDeformableDataset loadCapturedDeformableDataset(
    const std::filesystem::path& particlesPath,
    const std::filesystem::path& observationsPath) {
    CapturedDeformableDataset result;
    result.particles = loadCapturedParticlesCsv(particlesPath);
    result.observations = loadCapturedMarkerObservationsCsv(observationsPath);

    std::unordered_set<std::uint64_t> ids;
    for (const auto& particle : result.particles) ids.insert(particle.particleId);
    for (const auto& observation : result.observations) {
        if (!ids.contains(observation.particleId))
            throw std::invalid_argument("captured marker observation references unknown particle_id " +
                                        std::to_string(observation.particleId));
    }
    return result;
}

std::vector<solvers::MpmParticle> makeMpmParticles(const std::vector<CapturedParticleSpec>& particles) {
    if (particles.size() < 4U) throw std::invalid_argument("captured deformable MPM body needs at least four particles");
    std::vector<solvers::MpmParticle> result;
    result.reserve(particles.size());
    for (const auto& source : particles) {
        solvers::MpmParticle particle;
        particle.id = source.particleId;
        particle.restPosition = source.restPosition;
        particle.position = source.restPosition;
        particle.mass = source.mass;
        particle.restVolume = source.restVolume;
        result.push_back(particle);
    }
    return result;
}

} // namespace vulkax::capture
