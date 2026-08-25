#include "vulkax/research/captured_material_calibration.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

namespace vulkax::research {
namespace {

struct DynamicMetrics {
    std::size_t count{};
    double rms{};
    double maximum{};
};

[[nodiscard]] DynamicMetrics summarizeDynamic(
    const CapturedFreeRelaxationResult& replay,
    capture::ObservationSplit split) {
    DynamicMetrics result;
    double squared = 0.0;
    for (const auto& sample : replay.samples) {
        if (sample.split != split || std::abs(sample.time) <= 1.0e-12) continue;
        ++result.count;
        squared += sample.positionError * sample.positionError;
        result.maximum = std::max(result.maximum, sample.positionError);
    }
    if (result.count > 0)
        result.rms = std::sqrt(squared / static_cast<double>(result.count));
    return result;
}

void validateCandidateGrid(
    const std::vector<double>& young,
    const std::vector<double>& poisson) {
    if (young.empty() || poisson.empty())
        throw std::invalid_argument("captured material calibration requires non-empty parameter grids");
    for (const double value : young) {
        if (!std::isfinite(value) || value <= 0.0)
            throw std::invalid_argument("captured material Young's modulus candidates must be positive");
    }
    for (const double value : poisson) {
        if (!std::isfinite(value) || !(value > -1.0 && value < 0.5))
            throw std::invalid_argument("captured material Poisson candidates must lie in (-1, 0.5)");
    }
}

} // namespace

CapturedMaterialCalibrationResult calibrateCapturedMaterialGrid(
    const gaussian::GaussianCloud& world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& dataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    const std::vector<double>& youngModulusCandidates,
    const std::vector<double>& poissonRatioCandidates) {
    validateCandidateGrid(youngModulusCandidates, poissonRatioCandidates);

    CapturedMaterialCalibrationResult result;
    result.candidates.reserve(youngModulusCandidates.size() * poissonRatioCandidates.size());
    double bestFit = std::numeric_limits<double>::infinity();
    std::size_t bestIndex = 0;
    bool haveBest = false;

    for (const double young : youngModulusCandidates) {
        for (const double poisson : poissonRatioCandidates) {
            settings.material.youngModulus = young;
            settings.material.poissonRatio = poisson;
            const auto replay = runCapturedFreeRelaxationBenchmark(
                world, activeGaussianIndices, dataset, grid, settings);
            const auto fit = summarizeDynamic(replay, capture::ObservationSplit::Fit);
            const auto validation = summarizeDynamic(replay, capture::ObservationSplit::Validation);
            if (fit.count == 0)
                throw std::invalid_argument(
                    "captured material calibration requires at least one nonzero-time fit observation");

            CapturedMaterialCandidate candidate;
            candidate.youngModulus = young;
            candidate.poissonRatio = poisson;
            candidate.fitDynamicSamples = fit.count;
            candidate.validationDynamicSamples = validation.count;
            candidate.fitDynamicRms = fit.rms;
            candidate.fitDynamicMaximum = fit.maximum;
            candidate.validationDynamicRms = validation.rms;
            candidate.validationDynamicMaximum = validation.maximum;
            candidate.initializationFitRms = replay.initializationFitRms;
            candidate.appearanceRoundtripRms = replay.appearanceRoundtripRms;
            candidate.appearanceRoundtripMaximum = replay.appearanceRoundtripMaximum;
            result.candidates.push_back(candidate);

            const std::size_t index = result.candidates.size() - 1U;
            if (!haveBest || candidate.fitDynamicRms < bestFit) {
                bestFit = candidate.fitDynamicRms;
                bestIndex = index;
                haveBest = true;
            }
        }
    }

    result.selectedIndex = bestIndex;
    result.candidates.at(bestIndex).selected = true;
    settings.material.youngModulus = result.candidates[bestIndex].youngModulus;
    settings.material.poissonRatio = result.candidates[bestIndex].poissonRatio;
    result.selectedReplay = runCapturedFreeRelaxationBenchmark(
        world, activeGaussianIndices, dataset, grid, settings);
    return result;
}

void writeCapturedMaterialCalibrationCsv(
    const CapturedMaterialCalibrationResult& result,
    const std::filesystem::path& path) {
    if (result.candidates.empty() || result.selectedIndex >= result.candidates.size())
        throw std::invalid_argument("captured material calibration result is empty");
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open captured material calibration CSV");
    stream << "young_modulus,poisson_ratio,fit_dynamic_samples,validation_dynamic_samples,"
              "fit_dynamic_rms,fit_dynamic_max,validation_dynamic_rms,validation_dynamic_max,"
              "initialization_fit_rms,appearance_roundtrip_rms,appearance_roundtrip_max,selected\n";
    stream << std::setprecision(17);
    for (const auto& candidate : result.candidates) {
        stream << candidate.youngModulus << ',' << candidate.poissonRatio << ','
               << candidate.fitDynamicSamples << ',' << candidate.validationDynamicSamples << ','
               << candidate.fitDynamicRms << ',' << candidate.fitDynamicMaximum << ','
               << candidate.validationDynamicRms << ',' << candidate.validationDynamicMaximum << ','
               << candidate.initializationFitRms << ',' << candidate.appearanceRoundtripRms << ','
               << candidate.appearanceRoundtripMaximum << ',' << (candidate.selected ? 1 : 0) << '\n';
    }
    if (!stream) throw std::runtime_error("failed while writing captured material calibration CSV");
}

} // namespace vulkax::research
