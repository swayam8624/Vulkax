#pragma once

#include "vulkax/research/captured_deformable.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vulkax::research {

struct CapturedMaterialInfluenceRegion {
    std::string id;
    std::vector<std::uint64_t> particleIds;
};

struct CapturedMaterialInfluenceSettings {
    std::string objectiveMarkerId;
    double objectiveTime{};
    math::Vec3 objectiveDirection{1.0, 0.0, 0.0};
    double finiteDifferenceScaleStep{0.025};
    double verificationScaleDelta{0.05};
};

struct CapturedMaterialInfluenceFieldSample {
    std::string regionId;
    std::size_t particleCount{};
    math::Vec3 restCentroid{};
    double minusScale{};
    double plusScale{};
    double minusObservable{};
    double plusObservable{};
    // dJ / d(scale), where scale multiplies the selected particles' Young's modulus.
    double derivative{};
};

struct CapturedMaterialCounterfactualVerification {
    std::string regionId;
    double deltaScale{};
    double baselineObservable{};
    double predictedObservable{};
    double actualObservable{};
    double absoluteError{};
    double relativeLinearizationError{};
};

struct CapturedMaterialInfluenceResult {
    std::string objectiveMarkerId;
    double objectiveTime{};
    math::Vec3 objectiveDirection{};
    double baselineObservable{};
    CapturedFreeRelaxationResult baselineReplay;
    std::vector<CapturedMaterialInfluenceFieldSample> field;
    std::vector<CapturedMaterialCounterfactualVerification> verification;
};

struct CapturedMaterialAdjointInfluenceFieldSample {
    std::string regionId;
    std::size_t particleCount{};
    math::Vec3 restCentroid{};
    double derivative{};
};

struct CapturedMaterialAdjointInfluenceResult {
    std::string objectiveMarkerId;
    double objectiveTime{};
    math::Vec3 objectiveDirection{};
    double baselineObservable{};
    double minimumStencilKnotMargin{};
    // Ordered exactly like dataset.particles. This is dJ/ds_p for each stable
    // particle-local Young's-modulus coefficient.
    std::vector<double> particleScaleGradient;
    std::vector<CapturedMaterialAdjointInfluenceFieldSample> field;
};

struct CapturedMaterialInfluenceDerivativeComparison {
    std::string regionId;
    double referenceDerivative{};
    double adjointDerivative{};
    double absoluteError{};
    double relativeError{};
};

// Nonlinear finite-difference reference for local material Operator Influence.
// The scalar observable is the projected displacement of objectiveMarkerId from
// t=0 to objectiveTime. Each region perturbs a coefficient field multiplying
// Young's modulus on the listed stable particle IDs. A central difference
// estimates dJ/d(scale); a distinct one-sided perturbation is then rerun to
// measure the first-order counterfactual error. This is intentionally a
// reference/oracle path for later adjoint work, not an efficiency claim.
[[nodiscard]] CapturedMaterialInfluenceResult computeCapturedMaterialInfluenceReference(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& dataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    const std::vector<CapturedMaterialInfluenceRegion>& regions,
    const CapturedMaterialInfluenceSettings& influenceSettings);

// Reverse-mode APIC/MPM material Operator Influence for the same captured
// free-relaxation objective. The adjoint produces particle-local scale
// gradients in one reverse trajectory; each region derivative is the sum over
// its listed particle gradients. The current reverse kernel intentionally has
// the same explicit scope as the controlled benchmark: APIC, zero external
// forcing/gravity, and no boundary clamping.
[[nodiscard]] CapturedMaterialAdjointInfluenceResult computeCapturedMaterialInfluenceAdjoint(
    gaussian::GaussianCloud world,
    const std::vector<std::size_t>& activeGaussianIndices,
    const capture::CapturedDeformableDataset& dataset,
    const solvers::MpmGridSettings& grid,
    NonlinearDeformableWorldSettings settings,
    const std::vector<CapturedMaterialInfluenceRegion>& regions,
    const CapturedMaterialInfluenceSettings& influenceSettings);

[[nodiscard]] std::vector<CapturedMaterialInfluenceDerivativeComparison>
compareCapturedMaterialInfluenceDerivatives(
    const CapturedMaterialInfluenceResult& reference,
    const CapturedMaterialAdjointInfluenceResult& adjoint);

void writeCapturedMaterialInfluenceCsv(
    const CapturedMaterialInfluenceResult& result,
    const std::filesystem::path& path);

void writeCapturedMaterialCounterfactualCsv(
    const CapturedMaterialInfluenceResult& result,
    const std::filesystem::path& path);

void writeCapturedMaterialAdjointInfluenceCsv(
    const CapturedMaterialAdjointInfluenceResult& result,
    const std::filesystem::path& path);

void writeCapturedMaterialInfluenceDerivativeComparisonCsv(
    const std::vector<CapturedMaterialInfluenceDerivativeComparison>& comparison,
    const std::filesystem::path& path);

} // namespace vulkax::research
