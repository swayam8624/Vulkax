#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace vulkax::inverse {

enum class HyperelasticModel { NeoHookean, MooneyRivlin, Yeoh };

struct UniaxialSample {
    double stretch{1.0};
    double nominalStress{};
    double weight{1.0};
};

struct MaterialFit {
    HyperelasticModel model{HyperelasticModel::NeoHookean};
    std::vector<double> parameters;
    double weightedRmse{};
    double aic{};
    std::size_t samples{};
};

[[nodiscard]] std::string_view toString(HyperelasticModel model) noexcept;
[[nodiscard]] double predictUniaxialNominal(HyperelasticModel model,
                                            const std::vector<double>& parameters,
                                            double stretch);
[[nodiscard]] MaterialFit fitHyperelasticModel(HyperelasticModel model,
                                               const std::vector<UniaxialSample>& samples,
                                               double ridge = 1.0e-10);
[[nodiscard]] std::vector<MaterialFit> rankHyperelasticModels(
    const std::vector<UniaxialSample>& samples);
[[nodiscard]] std::vector<double> uniaxialSensitivityBasis(HyperelasticModel model,
                                                           double stretch);

} // namespace vulkax::inverse
