#pragma once

#include <cstdint>
#include <vector>

namespace vulkax::calibration {

enum class HyperelasticFamily : std::uint8_t { NeoHookean, MooneyRivlin, Yeoh3 };
struct StressDatum { double stretch{1.0}; double nominalStress{}; double weight{1.0}; };
struct ModelFit {
    HyperelasticFamily family{HyperelasticFamily::NeoHookean};
    std::vector<double> parameters;
    double rmsError{};
    double aic{};
    double bic{};
    bool valid{false};
};
struct ModelSelection { std::vector<ModelFit> fits; std::size_t bestIndex{}; bool valid{false}; };
struct ExperimentRecommendation { double stretch{}; double disagreement{}; bool valid{false}; };

[[nodiscard]] double predictNominalStress(HyperelasticFamily family, const std::vector<double>& parameters,
                                          double stretch);
[[nodiscard]] ModelFit fitModel(HyperelasticFamily family, const std::vector<StressDatum>& data);
[[nodiscard]] ModelSelection selectModel(const std::vector<StressDatum>& data);
[[nodiscard]] ExperimentRecommendation recommendNextUniaxialStretch(
    const ModelSelection& selection, const std::vector<double>& candidateStretches);

} // namespace vulkax::calibration
