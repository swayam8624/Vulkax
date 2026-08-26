#pragma once

#include "vulkax/capture/deformable_dataset.hpp"
#include "vulkax/gaussian/gaussian_cloud.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vulkax::capture {

enum class CapturedSourceKind : std::uint8_t {
    Synthetic,
    Measured,
    Derived,
};

struct CapturedObservationUncertainty {
    std::string markerId;
    double time{};
    math::Vec3 positionSigma{};
};

struct CapturedDeformableBundleManifest {
    unsigned schemaVersion{1U};
    std::string id;

    std::filesystem::path appearanceFile;
    std::filesystem::path particlesFile;
    std::filesystem::path observationsFile;
    std::filesystem::path uncertaintyFile;

    std::string appearanceSha256;
    std::string particlesSha256;
    std::string observationsSha256;
    std::string uncertaintySha256;

    std::string lengthUnit{"m"};
    std::string massUnit{"kg"};
    std::string timeUnit{"s"};
    std::string coordinateFrame{"world"};
    std::string axisConvention{"right-handed-y-up"};
    double timeStep{};

    CapturedSourceKind sourceKind{CapturedSourceKind::Synthetic};
    std::string sourceDescription;
};

struct CapturedDeformableBundle {
    std::filesystem::path manifestPath;
    CapturedDeformableBundleManifest manifest;
    gaussian::GaussianCloud appearance;
    CapturedDeformableDataset dataset;
    std::vector<CapturedObservationUncertainty> uncertainty;
};

[[nodiscard]] const char* toString(CapturedSourceKind value) noexcept;
[[nodiscard]] CapturedSourceKind capturedSourceKindFromString(const std::string& value);

[[nodiscard]] CapturedDeformableBundleManifest parseCapturedDeformableBundleManifest(
    const std::string& source);
[[nodiscard]] CapturedDeformableBundleManifest loadCapturedDeformableBundleManifest(
    const std::filesystem::path& path);
[[nodiscard]] std::string writeCapturedDeformableBundleManifest(
    const CapturedDeformableBundleManifest& manifest);
void saveCapturedDeformableBundleManifest(
    const CapturedDeformableBundleManifest& manifest,
    const std::filesystem::path& path);

[[nodiscard]] std::vector<CapturedObservationUncertainty> loadCapturedObservationUncertaintyCsv(
    const std::filesystem::path& path);
void writeCapturedObservationUncertaintyCsv(
    const std::vector<CapturedObservationUncertainty>& uncertainty,
    const std::filesystem::path& path);

// Recomputes hashes for the four manifest payloads relative to baseDirectory.
// Paths must be portable relative paths contained by that directory.
void refreshCapturedDeformableBundleHashes(
    CapturedDeformableBundleManifest& manifest,
    const std::filesystem::path& baseDirectory);

// Requires each marker to have both an initialization observation and at least
// one dynamic observation. A marker's nonzero-time fit/validation assignment
// must remain stable across its trajectory. The t=0 split may differ because
// initialization and held-out dynamic evaluation are separate roles.
void validateCapturedObservationTrajectoryContract(
    const CapturedDeformableDataset& dataset);

// Loads and validates the complete bundle. V1 payload values are required to be
// expressed directly in SI units (m, kg, s); no silent unit conversion occurs.
// Validation includes hashes, payload syntax, stable marker->particle identity,
// observation/uncertainty coverage, solver-lattice timestamps and fit/validation
// requirements needed by the current captured-deformable research path.
[[nodiscard]] CapturedDeformableBundle loadAndValidateCapturedDeformableBundle(
    const std::filesystem::path& manifestPath);

} // namespace vulkax::capture
