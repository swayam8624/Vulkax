#pragma once

#include "vulkax/research/captured_operator_influence.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace vulkax::research {

struct CapturedMaterialAdaptiveRegionSettings {
    // Smallest ranked particle set whose |dJ/ds_p| mass reaches this fraction
    // is always considered for proposal before spatial component capping.
    double cumulativeAbsoluteGradientFraction{0.90};
    // Also include every particle whose |dJ/ds_p| is at least this fraction of
    // the strongest particle. This prevents a sharp local peak from being cut
    // apart solely by a cumulative-mass boundary.
    double relativeParticleGradientThreshold{0.05};
    // Rest-space particles within multiplier * characteristic nearest-neighbor
    // spacing are connected into one proposed material region.
    double adjacencyRadiusMultiplier{1.05};
    std::size_t maximumRegions{8};
};

struct CapturedMaterialAdaptiveRegionProposal {
    CapturedMaterialAdaptiveRegionSettings settings{};
    std::vector<CapturedMaterialInfluenceRegion> regions;
    double characteristicSpacing{};
    double adjacencyRadius{};
    double totalAbsoluteGradient{};
    std::size_t candidateParticleCount{};
    double candidateAbsoluteGradient{};
    double candidateAbsoluteGradientFraction{};
    std::size_t proposedParticleCount{};
    double proposedAbsoluteGradient{};
    double proposedAbsoluteGradientFraction{};
};

// Propose spatially coherent material regions from the stable-ID particle
// adjoint field. This is a prioritization mechanism only: a proposed region is
// not VERIFIED until the retained nonlinear finite-difference/rerun oracle is
// evaluated independently on that region.
[[nodiscard]] CapturedMaterialAdaptiveRegionProposal proposeCapturedMaterialInfluenceRegions(
    const capture::CapturedDeformableDataset& dataset,
    const CapturedMaterialAdjointInfluenceResult& adjoint,
    const CapturedMaterialAdaptiveRegionSettings& settings = {});

// Re-aggregate an already computed particle adjoint field onto arbitrary
// regions without rerunning the reverse trajectory.
[[nodiscard]] CapturedMaterialAdjointInfluenceResult aggregateCapturedMaterialInfluenceAdjoint(
    const capture::CapturedDeformableDataset& dataset,
    CapturedMaterialAdjointInfluenceResult adjoint,
    const std::vector<CapturedMaterialInfluenceRegion>& regions);

void writeCapturedMaterialAdaptiveRegionProposalSummaryCsv(
    const CapturedMaterialAdaptiveRegionProposal& proposal,
    const std::filesystem::path& path);

} // namespace vulkax::research
