#include "vulkax/world/correspondence_graph.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace vulkax::world {
namespace {

const std::vector<std::size_t> emptyGaussianBindings;
const std::vector<PhysicalBinding> emptyPhysicalBindings;

} // namespace

void WorldCorrespondenceGraph::bindGaussian(std::size_t gaussianIndex, EntityId entity) {
    const auto existing = gaussianToEntity_.find(gaussianIndex);
    if (existing != gaussianToEntity_.end()) {
        if (existing->second == entity) return;
        auto& previous = entityToGaussians_[existing->second];
        previous.erase(std::remove(previous.begin(), previous.end(), gaussianIndex), previous.end());
    }

    gaussianToEntity_[gaussianIndex] = entity;
    auto& reverse = entityToGaussians_[entity];
    if (std::find(reverse.begin(), reverse.end(), gaussianIndex) == reverse.end()) reverse.push_back(gaussianIndex);
}

void WorldCorrespondenceGraph::bindPhysical(EntityId entity, PhysicalBinding binding) {
    entityToPhysical_[entity].push_back(binding);
}

std::optional<EntityId> WorldCorrespondenceGraph::entityForGaussian(std::size_t gaussianIndex) const {
    const auto it = gaussianToEntity_.find(gaussianIndex);
    if (it == gaussianToEntity_.end()) return std::nullopt;
    return it->second;
}

const std::vector<std::size_t>& WorldCorrespondenceGraph::gaussiansForEntity(EntityId entity) const noexcept {
    const auto it = entityToGaussians_.find(entity);
    return it == entityToGaussians_.end() ? emptyGaussianBindings : it->second;
}

const std::vector<PhysicalBinding>& WorldCorrespondenceGraph::physicalBindings(EntityId entity) const noexcept {
    const auto it = entityToPhysical_.find(entity);
    return it == entityToPhysical_.end() ? emptyPhysicalBindings : it->second;
}

CorrespondenceValidation WorldCorrespondenceGraph::validate(const WorldIR& world) const {
    CorrespondenceValidation report;

    for (const auto& [gaussianIndex, entityId] : gaussianToEntity_) {
        if (gaussianIndex >= world.appearance.size()) {
            report.valid = false;
            report.errors.push_back("Gaussian binding index " + std::to_string(gaussianIndex) + " is outside the appearance cloud");
        }
        if (world.findEntity(entityId) == nullptr) {
            report.valid = false;
            report.errors.push_back("Gaussian binding references missing entity " + std::to_string(entityId));
        }
    }

    for (const auto& [entityId, indices] : entityToGaussians_) {
        if (world.findEntity(entityId) == nullptr) {
            report.valid = false;
            report.errors.push_back("Reverse Gaussian map references missing entity " + std::to_string(entityId));
        }
        for (const auto index : indices) {
            const auto forward = gaussianToEntity_.find(index);
            if (forward == gaussianToEntity_.end() || forward->second != entityId) {
                report.valid = false;
                report.errors.push_back("Forward/reverse Gaussian correspondence is inconsistent at index " + std::to_string(index));
            }
        }
    }

    for (const auto& [entityId, bindings] : entityToPhysical_) {
        if (world.findEntity(entityId) == nullptr) {
            report.valid = false;
            report.errors.push_back("Physical map references missing entity " + std::to_string(entityId));
        }
        for (const auto& binding : bindings) {
            if (!std::isfinite(binding.weight) || binding.weight < 0.0) {
                report.valid = false;
                report.errors.push_back("Physical binding has invalid weight for entity " + std::to_string(entityId));
            }
        }
    }

    return report;
}

} // namespace vulkax::world
