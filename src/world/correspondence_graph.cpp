#include "vulkax/world/correspondence_graph.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>

namespace vulkax::world {
namespace {

const std::vector<gaussian::GaussianId> emptyGaussianBindings;
const std::vector<PhysicalBinding> emptyPhysicalBindings;

} // namespace

void WorldCorrespondenceGraph::bindGaussian(gaussian::GaussianId gaussianId, EntityId entity) {
    if (!gaussianId.valid()) throw std::invalid_argument("Gaussian correspondence requires a valid stable ID");
    const auto existing = gaussianToEntity_.find(gaussianId);
    if (existing != gaussianToEntity_.end()) {
        if (existing->second == entity) return;
        auto& previous = entityToGaussians_[existing->second];
        previous.erase(std::remove(previous.begin(), previous.end(), gaussianId), previous.end());
    }

    gaussianToEntity_[gaussianId] = entity;
    auto& reverse = entityToGaussians_[entity];
    if (std::find(reverse.begin(), reverse.end(), gaussianId) == reverse.end()) reverse.push_back(gaussianId);
}

void WorldCorrespondenceGraph::bindPhysical(EntityId entity, PhysicalBinding binding) {
    entityToPhysical_[entity].push_back(binding);
}

std::optional<EntityId> WorldCorrespondenceGraph::entityForGaussian(gaussian::GaussianId gaussianId) const {
    const auto it = gaussianToEntity_.find(gaussianId);
    if (it == gaussianToEntity_.end()) return std::nullopt;
    return it->second;
}

const std::vector<gaussian::GaussianId>& WorldCorrespondenceGraph::gaussiansForEntity(EntityId entity) const noexcept {
    const auto it = entityToGaussians_.find(entity);
    return it == entityToGaussians_.end() ? emptyGaussianBindings : it->second;
}

const std::vector<PhysicalBinding>& WorldCorrespondenceGraph::physicalBindings(EntityId entity) const noexcept {
    const auto it = entityToPhysical_.find(entity);
    return it == entityToPhysical_.end() ? emptyPhysicalBindings : it->second;
}

std::size_t WorldCorrespondenceGraph::pruneMissingGaussians(const gaussian::GaussianCloud& cloud) {
    const gaussian::GaussianIndexView view(cloud);
    const std::size_t before = gaussianToEntity_.size();
    for (auto it = gaussianToEntity_.begin(); it != gaussianToEntity_.end();) {
        if (!view.contains(it->first)) it = gaussianToEntity_.erase(it);
        else ++it;
    }

    entityToGaussians_.clear();
    for (const auto& [id, entity] : gaussianToEntity_)
        entityToGaussians_[entity].push_back(id);
    for (auto& [entity, ids] : entityToGaussians_) {
        (void)entity;
        std::sort(ids.begin(), ids.end(), [](gaussian::GaussianId lhs, gaussian::GaussianId rhs) {
            return lhs.packed() < rhs.packed();
        });
    }
    return before - gaussianToEntity_.size();
}

CorrespondenceValidation WorldCorrespondenceGraph::validate(const WorldIR& world) const {
    CorrespondenceValidation report;
    std::optional<gaussian::GaussianIndexView> indexView;
    try {
        indexView.emplace(world.appearance);
    } catch (const std::exception& error) {
        report.valid = false;
        report.errors.push_back(std::string("appearance identity: ") + error.what());
    }

    for (const auto& [gaussianId, entityId] : gaussianToEntity_) {
        if (!indexView.has_value() || !indexView->contains(gaussianId)) {
            report.valid = false;
            report.errors.push_back("Gaussian binding ID " + gaussian::toString(gaussianId) +
                                    " is absent from the appearance cloud");
        }
        if (world.findEntity(entityId) == nullptr) {
            report.valid = false;
            report.errors.push_back("Gaussian binding references missing entity " + std::to_string(entityId));
        }
    }

    for (const auto& [entityId, ids] : entityToGaussians_) {
        if (world.findEntity(entityId) == nullptr) {
            report.valid = false;
            report.errors.push_back("Reverse Gaussian map references missing entity " + std::to_string(entityId));
        }
        for (const auto id : ids) {
            const auto forward = gaussianToEntity_.find(id);
            if (forward == gaussianToEntity_.end() || forward->second != entityId) {
                report.valid = false;
                report.errors.push_back("Forward/reverse Gaussian correspondence is inconsistent at ID " +
                                        gaussian::toString(id));
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
