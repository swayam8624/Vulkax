#pragma once

#include "vulkax/capture/deformable_dataset.hpp"
#include "vulkax/research/nonlinear_deformable_world.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace vulkax::research {

struct CapturedReplaySample {
    std::string markerId;
    std::uint64_t particleId{};
    double time{};
    capture::ObservationSplit split{capture::ObservationSplit::Fit};
    math::Vec3 observed{};
    math::Vec3 predicted{};
    double positionError{};
};

struct CapturedReplayErrorMetrics {
    std::size_t sampleCount{};
    double rmsPositionError{};
    double maximumPositionError{};
};

struct CapturedFreeRelaxationResult {
    solvers::Matrix3 fittedInitialDeformation{solvers::identityMatrix3()};
    math::Vec3 fittedInitialTranslation{};
    double initializationFitRms{};
    double appearanceRoundtripRms{};
    double appearanceRoundtripMaximum{};
    CapturedReplayErrorMetrics fit;
    CapturedReplayErrorMetrics validation;
    std::vector<CapturedReplaySample> samples;
    NonlinearDeformableWorldResult simulation;
};

// Fits x(t=0) = F0 X + t0 from t=0 observations in the fit split only, then
// runs a free APIC/MPM-compatible nonlinear relaxation. Every later observation
// is compared against the particle with the explicitly supplied particle_id.
// Observation times must lie exactly on solver timesteps so the benchmark never
// hides temporal interpolation error inside the replay metric. The captured
// active Gaussian object is inverse-warped into rest space with MLS before the
// simulation, and appearanceRoundtrip* measures whether rest->captured mapping
// reconstructs the supplied t=0 Gaussian centers.
//
// particleYoungModulusScales optionally multiplies the global Young's modulus
// for selected stable particle IDs. Omitting the map preserves the homogeneous
// material path exactly. This explicit coefficient field is used by nonlinear
// local material / Operator Influence counterfactuals.
[[nodiscard]] CapturedFreeRelaxationResult runCapturedFreeRelaxationBenchmark(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& dataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    const std::unordered_map<std::uint64_t, double>& particleYoungModulusScales = {});

void writeCapturedReplaySamplesCsv(
    const CapturedFreeRelaxationResult& result,
    const std::filesystem::path& path);

void writeCapturedReplaySummaryCsv(
    const CapturedFreeRelaxationResult& result,
    const std::filesystem::path& path);

} // namespace vulkax::research
