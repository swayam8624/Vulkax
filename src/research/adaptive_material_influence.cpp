#include "vulkax/research/adaptive_material_influence.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vulkax::research {
namespace {

struct RankedParticle {
    std::size_t index{};
    std::uint64_t id{};
    double score{};
};

struct Component {
    std::vector<std::size_t> particleIndices;
    double absoluteGradient{};
    std::uint64_t minimumParticleId{};
};

[[nodiscard]] std::unordered_map<std::uint64_t, std::size_t> particleIndexById(
    const capture::CapturedDeformableDataset& dataset) {
    std::unordered_map<std::uint64_t, std::size_t> result;
    result.reserve(dataset.particles.size());
    for (std::size_t index = 0; index < dataset.particles.size(); ++index) {
        const auto id = dataset.particles[index].particleId;
        if (id == 0U || !result.emplace(id, index).second)
            throw std::invalid_argument("adaptive material influence requires unique non-zero particle IDs");
    }
    return result;
}

void validateAdjointField(
    const capture::CapturedDeformableDataset& dataset,
    const CapturedMaterialAdjointInfluenceResult& adjoint) {
    if (dataset.particles.empty())
        throw std::invalid_argument("adaptive material influence requires particles");
    if (adjoint.particleIds.size() != dataset.particles.size() ||
        adjoint.particleScaleGradient.size() != dataset.particles.size())
        throw std::invalid_argument("adaptive material influence particle field size mismatch");
    for (std::size_t index = 0; index < dataset.particles.size(); ++index) {
        if (adjoint.particleIds[index] != dataset.particles[index].particleId)
            throw std::invalid_argument("adaptive material influence stable particle ID order mismatch");
        if (!std::isfinite(adjoint.particleScaleGradient[index]))
            throw std::invalid_argument("adaptive material influence gradient is non-finite");
    }
}

void validateSettings(const CapturedMaterialAdaptiveRegionSettings& settings) {
    if (!std::isfinite(settings.cumulativeAbsoluteGradientFraction) ||
        settings.cumulativeAbsoluteGradientFraction <= 0.0 ||
        settings.cumulativeAbsoluteGradientFraction > 1.0)
        throw std::invalid_argument("adaptive gradient fraction must lie in (0, 1]");
    if (!std::isfinite(settings.relativeParticleGradientThreshold) ||
        settings.relativeParticleGradientThreshold < 0.0 ||
        settings.relativeParticleGradientThreshold > 1.0)
        throw std::invalid_argument("adaptive relative gradient threshold must lie in [0, 1]");
    if (!std::isfinite(settings.adjacencyRadiusMultiplier) ||
        settings.adjacencyRadiusMultiplier <= 0.0)
        throw std::invalid_argument("adaptive adjacency multiplier must be positive");
    if (settings.maximumRegions == 0U)
        throw std::invalid_argument("adaptive maximum region count must be positive");
}

[[nodiscard]] double characteristicSpacing(
    const capture::CapturedDeformableDataset& dataset) {
    if (dataset.particles.size() < 2U)
        throw std::invalid_argument("adaptive material influence requires multiple particles");
    std::vector<double> nearest;
    nearest.reserve(dataset.particles.size());
    for (std::size_t i = 0; i < dataset.particles.size(); ++i) {
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < dataset.particles.size(); ++j) {
            if (i == j) continue;
            const double distance = math::length(
                dataset.particles[i].restPosition - dataset.particles[j].restPosition);
            if (distance > 0.0) best = std::min(best, distance);
        }
        if (std::isfinite(best)) nearest.push_back(best);
    }
    if (nearest.empty())
        throw std::invalid_argument("adaptive material influence particle rest positions are coincident");
    std::sort(nearest.begin(), nearest.end());
    const std::size_t middle = nearest.size() / 2U;
    if (nearest.size() % 2U == 1U) return nearest[middle];
    return 0.5 * (nearest[middle - 1U] + nearest[middle]);
}

[[nodiscard]] math::Vec3 regionCentroid(
    const capture::CapturedDeformableDataset& dataset,
    const std::unordered_map<std::uint64_t, std::size_t>& particleIndices,
    const CapturedMaterialInfluenceRegion& region) {
    if (region.particleIds.empty())
        throw std::invalid_argument("adaptive material influence region cannot be empty");
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(region.particleIds.size());
    math::Vec3 centroid{};
    for (const auto id : region.particleIds) {
        if (!seen.insert(id).second)
            throw std::invalid_argument("adaptive material influence region contains duplicate particle IDs");
        const auto iterator = particleIndices.find(id);
        if (iterator == particleIndices.end())
            throw std::invalid_argument("adaptive material influence region references unknown particle ID");
        centroid += dataset.particles[iterator->second].restPosition;
    }
    return centroid / static_cast<double>(region.particleIds.size());
}

} // namespace

CapturedMaterialAdaptiveRegionProposal proposeCapturedMaterialInfluenceRegions(
    const capture::CapturedDeformableDataset& dataset,
    const CapturedMaterialAdjointInfluenceResult& adjoint,
    const CapturedMaterialAdaptiveRegionSettings& settings) {
    validateSettings(settings);
    validateAdjointField(dataset, adjoint);

    std::vector<RankedParticle> ranked;
    ranked.reserve(dataset.particles.size());
    double totalAbsoluteGradient = 0.0;
    for (std::size_t index = 0; index < dataset.particles.size(); ++index) {
        const double score = std::abs(adjoint.particleScaleGradient[index]);
        ranked.push_back({index, dataset.particles[index].particleId, score});
        totalAbsoluteGradient += score;
    }
    if (!std::isfinite(totalAbsoluteGradient) || totalAbsoluteGradient <= 1.0e-18)
        throw std::runtime_error("adaptive material influence particle field is numerically empty");

    std::sort(ranked.begin(), ranked.end(), [](const RankedParticle& lhs, const RankedParticle& rhs) {
        if (lhs.score != rhs.score) return lhs.score > rhs.score;
        return lhs.id < rhs.id;
    });

    const double target = settings.cumulativeAbsoluteGradientFraction * totalAbsoluteGradient;
    const double threshold = settings.relativeParticleGradientThreshold * ranked.front().score;
    std::vector<bool> selected(dataset.particles.size(), false);
    double cumulative = 0.0;
    for (const auto& particle : ranked) {
        if (cumulative < target) {
            selected[particle.index] = true;
            cumulative += particle.score;
        }
    }
    for (const auto& particle : ranked) {
        if (particle.score + 1.0e-30 >= threshold) selected[particle.index] = true;
    }

    CapturedMaterialAdaptiveRegionProposal proposal;
    proposal.settings = settings;
    proposal.totalAbsoluteGradient = totalAbsoluteGradient;
    for (std::size_t index = 0; index < selected.size(); ++index) {
        if (!selected[index]) continue;
        ++proposal.candidateParticleCount;
        proposal.candidateAbsoluteGradient += std::abs(adjoint.particleScaleGradient[index]);
    }
    proposal.candidateAbsoluteGradientFraction =
        proposal.candidateAbsoluteGradient / proposal.totalAbsoluteGradient;
    proposal.characteristicSpacing = characteristicSpacing(dataset);
    proposal.adjacencyRadius = settings.adjacencyRadiusMultiplier * proposal.characteristicSpacing;

    std::vector<bool> visited(dataset.particles.size(), false);
    std::vector<Component> components;
    const double adjacencyTolerance = proposal.adjacencyRadius * (1.0 + 1.0e-12);
    for (std::size_t seed = 0; seed < dataset.particles.size(); ++seed) {
        if (!selected[seed] || visited[seed]) continue;
        Component component;
        component.minimumParticleId = std::numeric_limits<std::uint64_t>::max();
        std::vector<std::size_t> frontier{seed};
        visited[seed] = true;
        for (std::size_t cursor = 0; cursor < frontier.size(); ++cursor) {
            const std::size_t current = frontier[cursor];
            component.particleIndices.push_back(current);
            component.absoluteGradient += std::abs(adjoint.particleScaleGradient[current]);
            component.minimumParticleId = std::min(
                component.minimumParticleId, dataset.particles[current].particleId);
            for (std::size_t candidate = 0; candidate < dataset.particles.size(); ++candidate) {
                if (!selected[candidate] || visited[candidate]) continue;
                const double distance = math::length(
                    dataset.particles[current].restPosition - dataset.particles[candidate].restPosition);
                if (distance <= adjacencyTolerance) {
                    visited[candidate] = true;
                    frontier.push_back(candidate);
                }
            }
        }
        components.push_back(std::move(component));
    }

    std::sort(components.begin(), components.end(), [](const Component& lhs, const Component& rhs) {
        if (lhs.absoluteGradient != rhs.absoluteGradient)
            return lhs.absoluteGradient > rhs.absoluteGradient;
        return lhs.minimumParticleId < rhs.minimumParticleId;
    });
    if (components.size() > settings.maximumRegions) components.resize(settings.maximumRegions);

    proposal.regions.reserve(components.size());
    for (std::size_t regionIndex = 0; regionIndex < components.size(); ++regionIndex) {
        CapturedMaterialInfluenceRegion region;
        region.id = "adaptive_" + std::to_string(regionIndex);
        region.particleIds.reserve(components[regionIndex].particleIndices.size());
        for (const auto particleIndex : components[regionIndex].particleIndices)
            region.particleIds.push_back(dataset.particles[particleIndex].particleId);
        std::sort(region.particleIds.begin(), region.particleIds.end());
        proposal.proposedParticleCount += region.particleIds.size();
        proposal.proposedAbsoluteGradient += components[regionIndex].absoluteGradient;
        proposal.regions.push_back(std::move(region));
    }
    proposal.proposedAbsoluteGradientFraction =
        proposal.proposedAbsoluteGradient / proposal.totalAbsoluteGradient;
    if (proposal.regions.empty())
        throw std::runtime_error("adaptive material influence produced no spatial regions");
    return proposal;
}

CapturedMaterialAdjointInfluenceResult aggregateCapturedMaterialInfluenceAdjoint(
    const capture::CapturedDeformableDataset& dataset,
    CapturedMaterialAdjointInfluenceResult adjoint,
    const std::vector<CapturedMaterialInfluenceRegion>& regions) {
    validateAdjointField(dataset, adjoint);
    if (regions.empty())
        throw std::invalid_argument("adaptive material influence aggregation requires regions");
    const auto particleIndices = particleIndexById(dataset);
    std::unordered_set<std::string> regionIds;
    regionIds.reserve(regions.size());
    adjoint.field.clear();
    adjoint.field.reserve(regions.size());
    for (const auto& region : regions) {
        if (region.id.empty() || !regionIds.insert(region.id).second)
            throw std::invalid_argument("adaptive material influence region IDs must be unique and non-empty");
        CapturedMaterialAdjointInfluenceFieldSample field;
        field.regionId = region.id;
        field.particleCount = region.particleIds.size();
        field.restCentroid = regionCentroid(dataset, particleIndices, region);
        for (const auto id : region.particleIds)
            field.derivative += adjoint.particleScaleGradient.at(particleIndices.at(id));
        if (!std::isfinite(field.derivative))
            throw std::runtime_error("adaptive material influence derivative is non-finite");
        adjoint.field.push_back(field);
    }
    return adjoint;
}

void writeCapturedMaterialAdaptiveRegionProposalSummaryCsv(
    const CapturedMaterialAdaptiveRegionProposal& proposal,
    const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("failed to open adaptive material influence proposal summary CSV");
    stream << "region_count,candidate_particle_count,proposed_particle_count,total_absolute_gradient,"
              "candidate_absolute_gradient,candidate_absolute_gradient_fraction,proposed_absolute_gradient,"
              "proposed_absolute_gradient_fraction,characteristic_spacing,adjacency_radius,"
              "target_absolute_gradient_fraction,relative_particle_gradient_threshold,"
              "adjacency_radius_multiplier,maximum_regions\n";
    stream << std::setprecision(17)
           << proposal.regions.size() << ','
           << proposal.candidateParticleCount << ','
           << proposal.proposedParticleCount << ','
           << proposal.totalAbsoluteGradient << ','
           << proposal.candidateAbsoluteGradient << ','
           << proposal.candidateAbsoluteGradientFraction << ','
           << proposal.proposedAbsoluteGradient << ','
           << proposal.proposedAbsoluteGradientFraction << ','
           << proposal.characteristicSpacing << ','
           << proposal.adjacencyRadius << ','
           << proposal.settings.cumulativeAbsoluteGradientFraction << ','
           << proposal.settings.relativeParticleGradientThreshold << ','
           << proposal.settings.adjacencyRadiusMultiplier << ','
           << proposal.settings.maximumRegions << '\n';
    if (!stream) throw std::runtime_error("failed while writing adaptive material influence proposal summary CSV");
}

} // namespace vulkax::research
