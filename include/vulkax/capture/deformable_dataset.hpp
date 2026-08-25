#pragma once

#include "vulkax/core/math.hpp"
#include "vulkax/solvers/mpm.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vulkax::capture {

enum class ObservationSplit {
    Fit,
    Validation,
};

[[nodiscard]] constexpr const char* toString(ObservationSplit split) noexcept {
    switch (split) {
        case ObservationSplit::Fit: return "fit";
        case ObservationSplit::Validation: return "validation";
    }
    return "unknown";
}

struct CapturedParticleSpec {
    std::uint64_t particleId{};
    math::Vec3 restPosition{};
    double mass{};
    double restVolume{};
};

struct CapturedMarkerObservation {
    std::string markerId;
    std::uint64_t particleId{};
    double time{};
    math::Vec3 position{};
    ObservationSplit split{ObservationSplit::Fit};
};

struct CapturedDeformableDataset {
    std::vector<CapturedParticleSpec> particles;
    std::vector<CapturedMarkerObservation> observations;
};

// particles.csv exact header:
// particle_id,rest_x,rest_y,rest_z,mass,rest_volume
[[nodiscard]] std::vector<CapturedParticleSpec> loadCapturedParticlesCsv(
    const std::filesystem::path& path);

// observations.csv exact header:
// marker_id,particle_id,time,x,y,z,split
// split must be fit or validation. Times are seconds in the same world frame as
// the Gaussian scene and physical particle coordinates.
[[nodiscard]] std::vector<CapturedMarkerObservation> loadCapturedMarkerObservationsCsv(
    const std::filesystem::path& path);

[[nodiscard]] CapturedDeformableDataset loadCapturedDeformableDataset(
    const std::filesystem::path& particlesPath,
    const std::filesystem::path& observationsPath);

[[nodiscard]] std::vector<solvers::MpmParticle> makeMpmParticles(
    const std::vector<CapturedParticleSpec>& particles);

} // namespace vulkax::capture
