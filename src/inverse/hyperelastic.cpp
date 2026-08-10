#include "vulkax/inverse/hyperelastic.hpp"

#include "vulkax/numerics/dense.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vulkax::inverse {

namespace {

std::size_t parameterCount(HyperelasticModel model) {
    switch (model) {
    case HyperelasticModel::NeoHookean: return 1;
    case HyperelasticModel::MooneyRivlin: return 2;
    case HyperelasticModel::Yeoh: return 3;
    }
    return 0;
}

} // namespace

std::string_view toString(HyperelasticModel model) noexcept {
    switch (model) {
    case HyperelasticModel::NeoHookean: return "Neo-Hookean";
    case HyperelasticModel::MooneyRivlin: return "Mooney-Rivlin";
    case HyperelasticModel::Yeoh: return "Yeoh";
    }
    return "Unknown";
}

std::vector<double> uniaxialSensitivityBasis(HyperelasticModel model, double stretch) {
    if (stretch <= 0.0) {
        throw std::invalid_argument("stretch must be positive");
    }
    const double d = stretch - 1.0 / (stretch * stretch);
    const double invariantOffset = stretch * stretch + 2.0 / stretch - 3.0;
    switch (model) {
    case HyperelasticModel::NeoHookean:
        return {d};
    case HyperelasticModel::MooneyRivlin:
        return {2.0 * d, 2.0 * d / stretch};
    case HyperelasticModel::Yeoh:
        return {2.0 * d, 4.0 * d * invariantOffset,
                6.0 * d * invariantOffset * invariantOffset};
    }
    throw std::logic_error("unknown hyperelastic model");
}

double predictUniaxialNominal(HyperelasticModel model, const std::vector<double>& parameters,
                              double stretch) {
    const auto basis = uniaxialSensitivityBasis(model, stretch);
    if (basis.size() != parameters.size()) {
        throw std::invalid_argument("hyperelastic parameter count mismatch");
    }
    return numerics::dot(basis, parameters);
}

MaterialFit fitHyperelasticModel(HyperelasticModel model, const std::vector<UniaxialSample>& samples,
                                 double ridge) {
    if (samples.size() < parameterCount(model) || ridge < 0.0) {
        throw std::invalid_argument("insufficient samples or invalid regularization");
    }
    const std::size_t p = parameterCount(model);
    numerics::DenseMatrix normal(p, p);
    std::vector<double> rhs(p, 0.0);
    double totalWeight = 0.0;
    for (const auto& sample : samples) {
        if (sample.stretch <= 0.0 || sample.weight <= 0.0 || !std::isfinite(sample.nominalStress)) {
            throw std::invalid_argument("invalid uniaxial calibration sample");
        }
        const auto basis = uniaxialSensitivityBasis(model, sample.stretch);
        totalWeight += sample.weight;
        for (std::size_t row = 0; row < p; ++row) {
            rhs[row] += sample.weight * basis[row] * sample.nominalStress;
            for (std::size_t col = 0; col < p; ++col) {
                normal(row, col) += sample.weight * basis[row] * basis[col];
            }
        }
    }
    for (std::size_t index = 0; index < p; ++index) {
        normal(index, index) += ridge;
    }
    const auto parameters = numerics::solveGaussian(normal, rhs, 1.0e-15);

    double squaredError = 0.0;
    for (const auto& sample : samples) {
        const double residual = predictUniaxialNominal(model, parameters, sample.stretch) -
                                sample.nominalStress;
        squaredError += sample.weight * residual * residual;
    }
    const double mse = squaredError / totalWeight;
    const double safeMse = std::max(mse, std::numeric_limits<double>::min());
    const double n = static_cast<double>(samples.size());
    const double aic = n * std::log(safeMse) + 2.0 * static_cast<double>(p);
    return {model, parameters, std::sqrt(mse), aic, samples.size()};
}

std::vector<MaterialFit> rankHyperelasticModels(const std::vector<UniaxialSample>& samples) {
    std::vector<MaterialFit> result;
    for (HyperelasticModel model : {HyperelasticModel::NeoHookean,
                                    HyperelasticModel::MooneyRivlin,
                                    HyperelasticModel::Yeoh}) {
        if (samples.size() >= parameterCount(model)) {
            result.push_back(fitHyperelasticModel(model, samples));
        }
    }
    std::sort(result.begin(), result.end(), [](const MaterialFit& lhs, const MaterialFit& rhs) {
        return lhs.aic < rhs.aic;
    });
    return result;
}

} // namespace vulkax::inverse
