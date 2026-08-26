#include "vulkax/capture/deformable_bundle.hpp"

#include "vulkax/core/sha256.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
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

[[nodiscard]] double parseFiniteDouble(
    const std::string& text,
    const char* label,
    std::size_t lineNumber) {
    std::size_t consumed = 0U;
    double value = 0.0;
    try {
        value = std::stod(text, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(
            std::string(label) + " is not numeric at line " + std::to_string(lineNumber));
    }
    if (consumed != text.size() || !std::isfinite(value))
        throw std::invalid_argument(
            std::string(label) + " is not finite at line " + std::to_string(lineNumber));
    return value;
}

[[nodiscard]] bool sameTime(double lhs, double rhs) noexcept {
    const double scale = std::max({1.0, std::abs(lhs), std::abs(rhs)});
    return std::abs(lhs - rhs) <= scale * 1.0e-10;
}

[[nodiscard]] std::string normalizedHash(std::string value, const char* label) {
    if (!core::isSha256Hex(value))
        throw std::invalid_argument(std::string(label) + " must be a 64-digit SHA-256 hex string");
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        if (character >= 'A' && character <= 'F') return static_cast<char>(character - 'A' + 'a');
        return static_cast<char>(character);
    });
    return value;
}

void validatePortableRelativePath(const std::filesystem::path& path, const char* label) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
        throw std::invalid_argument(std::string(label) + " must be a non-empty relative path");
    const auto normalized = path.lexically_normal();
    for (const auto& component : normalized) {
        if (component == "..")
            throw std::invalid_argument(std::string(label) + " may not escape the bundle directory");
    }
}

[[nodiscard]] std::filesystem::path resolvePayload(
    const std::filesystem::path& baseDirectory,
    const std::filesystem::path& relativePath,
    const char* label) {
    validatePortableRelativePath(relativePath, label);
    return (baseDirectory / relativePath).lexically_normal();
}

void verifyPayloadHash(
    const std::filesystem::path& path,
    const std::string& expected,
    const char* label) {
    const std::string actual = core::sha256FileHex(path);
    if (actual != expected) {
        throw std::invalid_argument(
            std::string("captured bundle ") + label + " SHA-256 mismatch: expected " +
            expected + ", got " + actual);
    }
}

void requireNoTrailing(std::istringstream& stream, std::size_t lineNumber) {
    std::string trailing;
    if (stream >> trailing)
        throw std::invalid_argument(
            "captured bundle manifest line " + std::to_string(lineNumber) +
            " contains unexpected trailing data");
}

[[nodiscard]] std::vector<std::pair<std::size_t, std::string>> meaningfulLines(
    const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured bundle CSV: " + path.string());
    std::vector<std::pair<std::size_t, std::string>> lines;
    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(stream, line)) {
        ++lineNumber;
        const std::string cleaned = trim(line);
        if (cleaned.empty() || cleaned.front() == '#') continue;
        lines.emplace_back(lineNumber, cleaned);
    }
    if (!stream.eof()) throw std::runtime_error("failed while reading captured bundle CSV: " + path.string());
    return lines;
}

void validateManifestValues(const CapturedDeformableBundleManifest& manifest) {
    if (manifest.schemaVersion != 1U)
        throw std::invalid_argument("only captured bundle schema version 1 is supported");
    if (manifest.id.empty()) throw std::invalid_argument("captured bundle id cannot be empty");
    validatePortableRelativePath(manifest.appearanceFile, "appearance path");
    validatePortableRelativePath(manifest.particlesFile, "particles path");
    validatePortableRelativePath(manifest.observationsFile, "observations path");
    validatePortableRelativePath(manifest.uncertaintyFile, "uncertainty path");
    if (manifest.lengthUnit != "m" || manifest.massUnit != "kg" || manifest.timeUnit != "s")
        throw std::invalid_argument("captured bundle schema 1 requires SI payload units: m, kg, s");
    if (manifest.coordinateFrame.empty() || manifest.axisConvention.empty())
        throw std::invalid_argument("captured bundle frame and axis convention cannot be empty");
    if (!std::isfinite(manifest.timeStep) || !(manifest.timeStep > 0.0))
        throw std::invalid_argument("captured bundle time_step must be finite and positive");
    if (manifest.sourceDescription.empty())
        throw std::invalid_argument("captured bundle source description cannot be empty");
    (void)normalizedHash(manifest.appearanceSha256, "appearance SHA-256");
    (void)normalizedHash(manifest.particlesSha256, "particles SHA-256");
    (void)normalizedHash(manifest.observationsSha256, "observations SHA-256");
    (void)normalizedHash(manifest.uncertaintySha256, "uncertainty SHA-256");
}

void validateObservationSemantics(
    const CapturedDeformableDataset& dataset,
    const std::vector<CapturedObservationUncertainty>& uncertainty,
    double timeStep) {
    std::unordered_map<std::string, std::uint64_t> markerParticles;
    std::unordered_set<std::uint64_t> initializationFitParticles;
    std::size_t dynamicFitSamples = 0U;
    std::size_t dynamicValidationSamples = 0U;

    for (const auto& observation : dataset.observations) {
        const auto [iterator, inserted] = markerParticles.emplace(observation.markerId, observation.particleId);
        if (!inserted && iterator->second != observation.particleId)
            throw std::invalid_argument(
                "captured bundle marker '" + observation.markerId +
                "' changes particle_id across its trajectory");

        const double latticeIndex = observation.time / timeStep;
        const double nearest = std::round(latticeIndex);
        const double snapped = nearest * timeStep;
        const double tolerance = std::max(1.0e-12, std::abs(observation.time) * 1.0e-10);
        if (std::abs(snapped - observation.time) > tolerance)
            throw std::invalid_argument(
                "captured bundle observation '" + observation.markerId + "' at time " +
                std::to_string(observation.time) + " is off the manifest solver time lattice");

        if (sameTime(observation.time, 0.0) && observation.split == ObservationSplit::Fit)
            initializationFitParticles.insert(observation.particleId);
        if (observation.time > 0.0 && observation.split == ObservationSplit::Fit)
            ++dynamicFitSamples;
        if (observation.time > 0.0 && observation.split == ObservationSplit::Validation)
            ++dynamicValidationSamples;
    }

    if (initializationFitParticles.size() < 4U)
        throw std::invalid_argument(
            "captured bundle requires at least four distinct t=0 fit particles for affine initialization");
    if (dynamicFitSamples == 0U)
        throw std::invalid_argument("captured bundle requires at least one nonzero-time fit observation");
    if (dynamicValidationSamples == 0U)
        throw std::invalid_argument("captured bundle requires at least one nonzero-time validation observation");

    if (uncertainty.size() != dataset.observations.size())
        throw std::invalid_argument(
            "captured bundle uncertainty sidecar must contain exactly one row per observation");

    std::vector<bool> uncertaintyUsed(uncertainty.size(), false);
    for (const auto& observation : dataset.observations) {
        std::size_t matches = 0U;
        std::size_t matchIndex = 0U;
        for (std::size_t index = 0U; index < uncertainty.size(); ++index) {
            if (uncertainty[index].markerId == observation.markerId &&
                sameTime(uncertainty[index].time, observation.time)) {
                ++matches;
                matchIndex = index;
            }
        }
        if (matches != 1U)
            throw std::invalid_argument(
                "captured bundle uncertainty coverage is missing or ambiguous for marker '" +
                observation.markerId + "' at time " + std::to_string(observation.time));
        if (uncertaintyUsed[matchIndex])
            throw std::invalid_argument("captured bundle uncertainty row matched more than one observation");
        uncertaintyUsed[matchIndex] = true;
    }
    if (std::find(uncertaintyUsed.begin(), uncertaintyUsed.end(), false) != uncertaintyUsed.end())
        throw std::invalid_argument("captured bundle uncertainty sidecar contains an unmatched row");
}

} // namespace

const char* toString(CapturedSourceKind value) noexcept {
    switch (value) {
        case CapturedSourceKind::Synthetic: return "synthetic";
        case CapturedSourceKind::Measured: return "measured";
        case CapturedSourceKind::Derived: return "derived";
    }
    return "unknown";
}

CapturedSourceKind capturedSourceKindFromString(const std::string& value) {
    if (value == "synthetic") return CapturedSourceKind::Synthetic;
    if (value == "measured") return CapturedSourceKind::Measured;
    if (value == "derived") return CapturedSourceKind::Derived;
    throw std::invalid_argument("captured bundle source kind must be synthetic, measured or derived");
}

CapturedDeformableBundleManifest parseCapturedDeformableBundleManifest(const std::string& source) {
    CapturedDeformableBundleManifest result;
    std::istringstream input(source);
    std::string line;
    std::size_t lineNumber = 0U;
    bool headerSeen = false;
    bool idSeen = false;
    bool appearanceSeen = false;
    bool particlesSeen = false;
    bool observationsSeen = false;
    bool uncertaintySeen = false;
    bool unitsSeen = false;
    bool frameSeen = false;
    bool timeStepSeen = false;
    bool sourceSeen = false;

    const auto duplicate = [&](bool alreadySeen, const char* label) {
        if (alreadySeen)
            throw std::invalid_argument(std::string("duplicate captured bundle ") + label + " record");
    };

    while (std::getline(input, line)) {
        ++lineNumber;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream stream(line.substr(first));
        std::string command;
        stream >> command;
        try {
            if (command == "vulkax_capture") {
                duplicate(headerSeen, "header");
                unsigned version = 0U;
                if (!(stream >> version) || version != 1U)
                    throw std::invalid_argument("only schema version 1 is supported");
                result.schemaVersion = version;
                headerSeen = true;
                requireNoTrailing(stream, lineNumber);
            } else if (command == "id") {
                duplicate(idSeen, "id");
                if (!(stream >> std::quoted(result.id))) throw std::invalid_argument("expected quoted bundle id");
                idSeen = true;
                requireNoTrailing(stream, lineNumber);
            } else if (command == "appearance") {
                duplicate(appearanceSeen, "appearance");
                std::string path;
                if (!(stream >> std::quoted(path) >> std::quoted(result.appearanceSha256)))
                    throw std::invalid_argument("expected quoted appearance path and SHA-256");
                result.appearanceFile = path;
                result.appearanceSha256 = normalizedHash(result.appearanceSha256, "appearance SHA-256");
                appearanceSeen = true;
                requireNoTrailing(stream, lineNumber);
            } else if (command == "particles") {
                duplicate(particlesSeen, "particles");
                std::string path;
                if (!(stream >> std::quoted(path) >> std::quoted(result.particlesSha256)))
                    throw std::invalid_argument("expected quoted particles path and SHA-256");
                result.particlesFile = path;
                result.particlesSha256 = normalizedHash(result.particlesSha256, "particles SHA-256");
                particlesSeen = true;
                requireNoTrailing(stream, lineNumber);
            } else if (command == "observations") {
                duplicate(observationsSeen, "observations");
                std::string path;
                if (!(stream >> std::quoted(path) >> std::quoted(result.observationsSha256)))
                    throw std::invalid_argument("expected quoted observations path and SHA-256");
                result.observationsFile = path;
                result.observationsSha256 = normalizedHash(result.observationsSha256, "observations SHA-256");
                observationsSeen = true;
                requireNoTrailing(stream, lineNumber);
            } else if (command == "uncertainty") {
                duplicate(uncertaintySeen, "uncertainty");
                std::string path;
                if (!(stream >> std::quoted(path) >> std::quoted(result.uncertaintySha256)))
                    throw std::invalid_argument("expected quoted uncertainty path and SHA-256");
                result.uncertaintyFile = path;
                result.uncertaintySha256 = normalizedHash(result.uncertaintySha256, "uncertainty SHA-256");
                uncertaintySeen = true;
                requireNoTrailing(stream, lineNumber);
            } else if (command == "units") {
                duplicate(unitsSeen, "units");
                if (!(stream >> std::quoted(result.lengthUnit) >> std::quoted(result.massUnit) >>
                      std::quoted(result.timeUnit)))
                    throw std::invalid_argument("expected quoted length, mass and time units");
                unitsSeen = true;
                requireNoTrailing(stream, lineNumber);
            } else if (command == "frame") {
                duplicate(frameSeen, "frame");
                if (!(stream >> std::quoted(result.coordinateFrame) >> std::quoted(result.axisConvention)))
                    throw std::invalid_argument("expected quoted coordinate frame and axis convention");
                frameSeen = true;
                requireNoTrailing(stream, lineNumber);
            } else if (command == "time_step") {
                duplicate(timeStepSeen, "time_step");
                if (!(stream >> result.timeStep) || !std::isfinite(result.timeStep))
                    throw std::invalid_argument("expected finite time_step");
                timeStepSeen = true;
                requireNoTrailing(stream, lineNumber);
            } else if (command == "source") {
                duplicate(sourceSeen, "source");
                std::string kind;
                if (!(stream >> kind >> std::quoted(result.sourceDescription)))
                    throw std::invalid_argument("expected source kind and quoted description");
                result.sourceKind = capturedSourceKindFromString(kind);
                sourceSeen = true;
                requireNoTrailing(stream, lineNumber);
            } else {
                throw std::invalid_argument("unknown command: " + command);
            }
        } catch (const std::exception& error) {
            throw std::invalid_argument(
                "captured bundle manifest line " + std::to_string(lineNumber) + ": " + error.what());
        }
    }

    if (!headerSeen || !idSeen || !appearanceSeen || !particlesSeen || !observationsSeen ||
        !uncertaintySeen || !unitsSeen || !frameSeen || !timeStepSeen || !sourceSeen)
        throw std::invalid_argument("captured bundle manifest is missing one or more required records");
    validateManifestValues(result);
    return result;
}

CapturedDeformableBundleManifest loadCapturedDeformableBundleManifest(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("failed to open captured bundle manifest: " + path.string());
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream && !stream.eof())
        throw std::runtime_error("failed while reading captured bundle manifest: " + path.string());
    return parseCapturedDeformableBundleManifest(contents.str());
}

std::string writeCapturedDeformableBundleManifest(
    const CapturedDeformableBundleManifest& manifest) {
    validateManifestValues(manifest);
    std::ostringstream stream;
    stream << std::setprecision(17);
    stream << "vulkax_capture 1\n"
           << "id " << std::quoted(manifest.id) << '\n'
           << "appearance " << std::quoted(manifest.appearanceFile.generic_string()) << ' '
           << std::quoted(normalizedHash(manifest.appearanceSha256, "appearance SHA-256")) << '\n'
           << "particles " << std::quoted(manifest.particlesFile.generic_string()) << ' '
           << std::quoted(normalizedHash(manifest.particlesSha256, "particles SHA-256")) << '\n'
           << "observations " << std::quoted(manifest.observationsFile.generic_string()) << ' '
           << std::quoted(normalizedHash(manifest.observationsSha256, "observations SHA-256")) << '\n'
           << "uncertainty " << std::quoted(manifest.uncertaintyFile.generic_string()) << ' '
           << std::quoted(normalizedHash(manifest.uncertaintySha256, "uncertainty SHA-256")) << '\n'
           << "units " << std::quoted(manifest.lengthUnit) << ' ' << std::quoted(manifest.massUnit) << ' '
           << std::quoted(manifest.timeUnit) << '\n'
           << "frame " << std::quoted(manifest.coordinateFrame) << ' '
           << std::quoted(manifest.axisConvention) << '\n'
           << "time_step " << manifest.timeStep << '\n'
           << "source " << toString(manifest.sourceKind) << ' '
           << std::quoted(manifest.sourceDescription) << '\n';
    return stream.str();
}

void saveCapturedDeformableBundleManifest(
    const CapturedDeformableBundleManifest& manifest,
    const std::filesystem::path& path) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("failed to create captured bundle manifest: " + path.string());
    stream << writeCapturedDeformableBundleManifest(manifest);
    if (!stream) throw std::runtime_error("failed while writing captured bundle manifest: " + path.string());
}

std::vector<CapturedObservationUncertainty> loadCapturedObservationUncertaintyCsv(
    const std::filesystem::path& path) {
    const auto lines = meaningfulLines(path);
    if (lines.empty()) throw std::invalid_argument("captured observation uncertainty CSV is empty");
    constexpr std::string_view expectedHeader = "marker_id,time,sigma_x,sigma_y,sigma_z";
    if (lines.front().second != expectedHeader)
        throw std::invalid_argument("captured uncertainty CSV header must be: " + std::string(expectedHeader));

    std::vector<CapturedObservationUncertainty> result;
    result.reserve(lines.size() - 1U);
    std::set<std::pair<std::string, double>> markerTimes;
    for (std::size_t row = 1U; row < lines.size(); ++row) {
        const auto& [lineNumber, line] = lines[row];
        const auto fields = splitCsv(line);
        if (fields.size() != 5U)
            throw std::invalid_argument(
                "captured uncertainty CSV row must contain five fields at line " +
                std::to_string(lineNumber));
        CapturedObservationUncertainty sample;
        sample.markerId = fields[0];
        if (sample.markerId.empty())
            throw std::invalid_argument("uncertainty marker_id cannot be empty at line " + std::to_string(lineNumber));
        sample.time = parseFiniteDouble(fields[1], "uncertainty time", lineNumber);
        if (sample.time < 0.0)
            throw std::invalid_argument("uncertainty time cannot be negative at line " + std::to_string(lineNumber));
        sample.positionSigma = {
            parseFiniteDouble(fields[2], "sigma_x", lineNumber),
            parseFiniteDouble(fields[3], "sigma_y", lineNumber),
            parseFiniteDouble(fields[4], "sigma_z", lineNumber),
        };
        if (sample.positionSigma.x < 0.0 || sample.positionSigma.y < 0.0 || sample.positionSigma.z < 0.0)
            throw std::invalid_argument("uncertainty sigma values must be non-negative at line " + std::to_string(lineNumber));
        if (!markerTimes.emplace(sample.markerId, sample.time).second)
            throw std::invalid_argument("duplicate uncertainty marker_id/time at line " + std::to_string(lineNumber));
        result.push_back(std::move(sample));
    }
    if (result.empty()) throw std::invalid_argument("captured uncertainty CSV contains no rows");
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.time != rhs.time) return lhs.time < rhs.time;
        return lhs.markerId < rhs.markerId;
    });
    return result;
}

void writeCapturedObservationUncertaintyCsv(
    const std::vector<CapturedObservationUncertainty>& uncertainty,
    const std::filesystem::path& path) {
    if (uncertainty.empty()) throw std::invalid_argument("cannot write empty captured uncertainty CSV");
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to create captured uncertainty CSV: " + path.string());
    stream << "marker_id,time,sigma_x,sigma_y,sigma_z\n" << std::setprecision(17);
    for (const auto& sample : uncertainty) {
        if (sample.markerId.empty() || !std::isfinite(sample.time) || sample.time < 0.0 ||
            !std::isfinite(sample.positionSigma.x) || !std::isfinite(sample.positionSigma.y) ||
            !std::isfinite(sample.positionSigma.z) || sample.positionSigma.x < 0.0 ||
            sample.positionSigma.y < 0.0 || sample.positionSigma.z < 0.0)
            throw std::invalid_argument("captured uncertainty sample is invalid");
        stream << sample.markerId << ',' << sample.time << ','
               << sample.positionSigma.x << ',' << sample.positionSigma.y << ','
               << sample.positionSigma.z << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing captured uncertainty CSV: " + path.string());
}

void refreshCapturedDeformableBundleHashes(
    CapturedDeformableBundleManifest& manifest,
    const std::filesystem::path& baseDirectory) {
    const auto appearance = resolvePayload(baseDirectory, manifest.appearanceFile, "appearance path");
    const auto particles = resolvePayload(baseDirectory, manifest.particlesFile, "particles path");
    const auto observations = resolvePayload(baseDirectory, manifest.observationsFile, "observations path");
    const auto uncertainty = resolvePayload(baseDirectory, manifest.uncertaintyFile, "uncertainty path");
    manifest.appearanceSha256 = core::sha256FileHex(appearance);
    manifest.particlesSha256 = core::sha256FileHex(particles);
    manifest.observationsSha256 = core::sha256FileHex(observations);
    manifest.uncertaintySha256 = core::sha256FileHex(uncertainty);
}

CapturedDeformableBundle loadAndValidateCapturedDeformableBundle(
    const std::filesystem::path& manifestPath) {
    CapturedDeformableBundle result;
    result.manifestPath = manifestPath;
    result.manifest = loadCapturedDeformableBundleManifest(manifestPath);
    const auto baseDirectory = manifestPath.parent_path();
    const auto appearancePath = resolvePayload(baseDirectory, result.manifest.appearanceFile, "appearance path");
    const auto particlesPath = resolvePayload(baseDirectory, result.manifest.particlesFile, "particles path");
    const auto observationsPath = resolvePayload(baseDirectory, result.manifest.observationsFile, "observations path");
    const auto uncertaintyPath = resolvePayload(baseDirectory, result.manifest.uncertaintyFile, "uncertainty path");

    verifyPayloadHash(appearancePath, result.manifest.appearanceSha256, "appearance");
    verifyPayloadHash(particlesPath, result.manifest.particlesSha256, "particles");
    verifyPayloadHash(observationsPath, result.manifest.observationsSha256, "observations");
    verifyPayloadHash(uncertaintyPath, result.manifest.uncertaintySha256, "uncertainty");

    result.appearance = gaussian::load3dgsPly(appearancePath);
    if (result.appearance.empty()) throw std::invalid_argument("captured bundle appearance contains no Gaussians");
    result.dataset = loadCapturedDeformableDataset(particlesPath, observationsPath);
    result.uncertainty = loadCapturedObservationUncertaintyCsv(uncertaintyPath);
    validateObservationSemantics(result.dataset, result.uncertainty, result.manifest.timeStep);
    return result;
}

} // namespace vulkax::capture
