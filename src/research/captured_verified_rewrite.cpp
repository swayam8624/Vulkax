#include "vulkax/research/captured_verified_rewrite.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace vulkax::research {
namespace {

[[nodiscard]] const world::SetMaterialParameter& requireYoungModulusEdit(
    const world::WorldTransaction& transaction) {
    if (transaction.edits.size() != 1U)
        throw std::runtime_error("captured material verifier requires exactly one transaction edit");
    const auto* edit = std::get_if<world::SetMaterialParameter>(&transaction.edits.front());
    if (edit == nullptr || edit->name != "young_modulus")
        throw std::runtime_error("captured material verifier supports only young_modulus rewrites");
    return *edit;
}

[[nodiscard]] double previousYoungModulus(const world::TransactionReceipt& receipt,
                                          world::EntityId entity) {
    const auto snapshot = std::find_if(
        receipt.previousMaterials.begin(), receipt.previousMaterials.end(),
        [entity](const world::EntityMaterialSnapshot& value) { return value.entity == entity; });
    if (snapshot == receipt.previousMaterials.end())
        throw std::runtime_error("captured material verifier is missing the previous material snapshot");
    const auto value = snapshot->materialParameters.find("young_modulus");
    if (value == snapshot->materialParameters.end())
        throw std::runtime_error("captured material verifier requires an existing young_modulus baseline");
    return value->second;
}

[[nodiscard]] std::vector<std::uint64_t> particleIdsForEntity(
    const world::WorldCorrespondenceGraph& graph,
    world::EntityId entity,
    const capture::CapturedDeformableDataset& dataset) {
    std::unordered_set<std::uint64_t> known;
    for (const auto& particle : dataset.particles) known.insert(particle.particleId);

    std::vector<std::uint64_t> ids;
    for (const auto& binding : graph.physicalBindings(entity)) {
        if (binding.kind != world::PhysicalKind::MpmParticle)
            throw std::runtime_error("captured material verifier requires MPM-particle physical bindings");
        if (!known.contains(binding.index))
            throw std::runtime_error("captured material verifier binding is not a stable captured particle ID");
        if (std::find(ids.begin(), ids.end(), binding.index) == ids.end()) ids.push_back(binding.index);
    }
    if (ids.empty()) throw std::runtime_error("captured material verifier requires a non-empty particle region");
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace

world::PhysicalRewriteVerifier makeCapturedMaterialRewriteVerifier(
    CapturedMaterialRewriteVerifierSettings settings) {
    if (settings.evidenceDirectory.empty())
        throw std::invalid_argument("captured material verifier requires an evidence directory");
    if (!std::isfinite(settings.maximumRelativeLinearizationError) ||
        settings.maximumRelativeLinearizationError < 0.0 ||
        !std::isfinite(settings.maximumAdjointAbsoluteError) ||
        settings.maximumAdjointAbsoluteError < 0.0 ||
        !std::isfinite(settings.maximumAdjointRelativeError) ||
        settings.maximumAdjointRelativeError < 0.0 ||
        !std::isfinite(settings.minimumReferenceDerivativeForRelativeCheck) ||
        settings.minimumReferenceDerivativeForRelativeCheck < 0.0 ||
        !std::isfinite(settings.rewriteScaleTolerance) || settings.rewriteScaleTolerance < 0.0)
        throw std::invalid_argument("captured material verifier thresholds must be finite and non-negative");

    return [settings = std::move(settings)](
               const world::WorldIR& committedWorld,
               const world::WorldCorrespondenceGraph& graph,
               const world::WorldTransaction& transaction,
               const world::TransactionReceipt& receipt) -> world::PhysicalRewriteEvidence {
        const auto& edit = requireYoungModulusEdit(transaction);
        const auto* entity = committedWorld.findEntity(edit.entity);
        if (entity == nullptr) throw std::runtime_error("captured material verifier lost committed entity");
        const auto current = entity->materialParameters.find("young_modulus");
        if (current == entity->materialParameters.end())
            throw std::runtime_error("captured material verifier cannot read committed young_modulus");

        const double previousYoung = previousYoungModulus(receipt, edit.entity);
        const double currentYoung = current->second;
        if (!std::isfinite(previousYoung) || previousYoung <= 0.0 ||
            !std::isfinite(currentYoung) || currentYoung <= 0.0)
            throw std::runtime_error("captured material verifier requires positive finite Young's modulus values");

        const double baselineRelativeMismatch =
            std::abs(settings.worldSettings.material.youngModulus - previousYoung) /
            std::max(1.0, std::abs(previousYoung));
        if (baselineRelativeMismatch > settings.rewriteScaleTolerance)
            throw std::runtime_error("captured material verifier baseline Young's modulus does not match transaction snapshot");

        const double requestedScaleDelta = currentYoung / previousYoung - 1.0;
        if (std::abs(requestedScaleDelta - settings.influenceSettings.verificationScaleDelta) >
            settings.rewriteScaleTolerance)
            throw std::runtime_error("captured material verifier perturbation does not match transaction rewrite scale");

        CapturedMaterialInfluenceRegion region;
        region.id = "transaction_" + transaction.id;
        region.particleIds = particleIdsForEntity(graph, edit.entity, settings.dataset);
        const std::vector<CapturedMaterialInfluenceRegion> regions{region};

        const auto reference = computeCapturedMaterialInfluenceReference(
            committedWorld.appearance,
            settings.activeGaussianIndices,
            settings.dataset,
            settings.grid,
            settings.worldSettings,
            regions,
            settings.influenceSettings);
        const auto adjoint = computeCapturedMaterialInfluenceAdjoint(
            committedWorld.appearance,
            settings.activeGaussianIndices,
            settings.dataset,
            settings.grid,
            settings.worldSettings,
            regions,
            settings.influenceSettings);
        const auto comparison = compareCapturedMaterialInfluenceDerivatives(reference, adjoint);

        if (reference.field.size() != 1U || reference.verification.size() != 1U ||
            adjoint.field.size() != 1U || comparison.size() != 1U)
            throw std::runtime_error("captured material verifier expected exactly one region result");

        const auto& nonlinear = reference.verification.front();
        const auto& derivative = comparison.front();
        const bool nonlinearPassed =
            std::isfinite(nonlinear.actualObservable) &&
            std::isfinite(nonlinear.predictedObservable) &&
            std::isfinite(nonlinear.relativeLinearizationError) &&
            nonlinear.relativeLinearizationError <= settings.maximumRelativeLinearizationError;
        bool derivativePassed =
            std::isfinite(derivative.absoluteError) &&
            derivative.absoluteError <= settings.maximumAdjointAbsoluteError;
        if (std::abs(derivative.referenceDerivative) > settings.minimumReferenceDerivativeForRelativeCheck) {
            derivativePassed = derivativePassed &&
                               std::isfinite(derivative.relativeError) &&
                               derivative.relativeError <= settings.maximumAdjointRelativeError;
        }

        std::filesystem::create_directories(settings.evidenceDirectory);
        const auto referencePath = settings.evidenceDirectory / "reference.csv";
        const auto counterfactualPath = settings.evidenceDirectory / "counterfactual.csv";
        const auto adjointPath = settings.evidenceDirectory / "adjoint.csv";
        const auto comparisonPath = settings.evidenceDirectory / "derivative_comparison.csv";
        writeCapturedMaterialInfluenceCsv(reference, referencePath);
        writeCapturedMaterialCounterfactualCsv(reference, counterfactualPath);
        writeCapturedMaterialAdjointInfluenceCsv(adjoint, adjointPath);
        writeCapturedMaterialInfluenceDerivativeComparisonCsv(comparison, comparisonPath);

        std::ostringstream summary;
        summary << "captured APIC material rewrite: region_particles=" << region.particleIds.size()
                << " nonlinear_relative_error=" << nonlinear.relativeLinearizationError
                << " adjoint_absolute_error=" << derivative.absoluteError
                << " adjoint_relative_error=" << derivative.relativeError;

        world::PhysicalRewriteEvidence evidence;
        evidence.rerunCompleted = true;
        evidence.rerunPassed = nonlinearPassed;
        evidence.independentOracleCompleted = true;
        evidence.independentOraclePassed = derivativePassed;
        evidence.observableError = nonlinear.relativeLinearizationError;
        evidence.observableTolerance = settings.maximumRelativeLinearizationError;
        evidence.artifact = counterfactualPath.string();
        evidence.summary = summary.str();
        return evidence;
    };
}

} // namespace vulkax::research
