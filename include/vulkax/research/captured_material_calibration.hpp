#pragma once

#include "vulkax/research/captured_deformable.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace vulkax::research {

struct CapturedMaterialCandidate {
    double youngModulus{};
    double poissonRatio{};
    std::size_t fitDynamicSamples{};
    std::size_t validationDynamicSamples{};
    double fitDynamicRms{};
    double fitDynamicMaximum{};
    double validationDynamicRms{};
    double validationDynamicMaximum{};
    double initializationFitRms{};
    double appearanceRoundtripRms{};
    double appearanceRoundtripMaximum{};
    bool selected{};
};

struct CapturedMaterialCalibrationResult {
    std::vector<CapturedMaterialCandidate> candidates;
    std::size_t selectedIndex{};
    CapturedFreeRelaxationResult selectedReplay;
};

// Grid-search material identification for a captured free-relaxation sequence.
// Candidate ranking uses ONLY fit-split observations with time > 0. The t=0
// fit rows are reserved for capture-pose initialization, and validation rows
// are reported only after selection so model choice cannot leak held-out data.
[[nodiscard]] CapturedMaterialCalibrationResult calibrateCapturedMaterialGrid(
    const gaussian::GaussianCloud& world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& dataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    const std::vector<double>& youngModulusCandidates,
    const std::vector<double>& poissonRatioCandidates);

void writeCapturedMaterialCalibrationCsv(
    const CapturedMaterialCalibrationResult& result,
    const std::filesystem::path& path);

} // namespace vulkax::research
