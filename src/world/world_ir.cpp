#include "vulkax/world/world_ir.hpp"

#include <cmath>
#include <unordered_set>

namespace vulkax::world {
namespace {

std::string parameterLabel(const ParameterAddress& address) {
    const char* space = "material";
    if (address.space == ParameterSpace::Constraint) space = "constraint";
    if (address.space == ParameterSpace::Global) space = "global";
    if (address.entityId.has_value()) {
        return std::string(space) + ":" + std::to_string(*address.entityId) + ":" + address.name;
    }
    return std::string(space) + ":" + address.name;
}

} // namespace

Entity* WorldIR::findEntity(EntityId entityId) noexcept {
    for (auto& entity : entities) {
        if (entity.id == entityId) return &entity;
    }
    return nullptr;
}

const Entity* WorldIR::findEntity(EntityId entityId) const noexcept {
    for (const auto& entity : entities) {
        if (entity.id == entityId) return &entity;
    }
    return nullptr;
}

ObservationRecord* WorldIR::findObservation(std::string_view observationId) noexcept {
    for (auto& observation : observations) {
        if (observation.id == observationId) return &observation;
    }
    return nullptr;
}

const ObservationRecord* WorldIR::findObservation(std::string_view observationId) const noexcept {
    for (const auto& observation : observations) {
        if (observation.id == observationId) return &observation;
    }
    return nullptr;
}

ParameterBelief* WorldIR::findParameterBelief(const ParameterAddress& address) noexcept {
    for (auto& belief : parameterBeliefs) {
        if (belief.address == address) return &belief;
    }
    return nullptr;
}

const ParameterBelief* WorldIR::findParameterBelief(const ParameterAddress& address) const noexcept {
    for (const auto& belief : parameterBeliefs) {
        if (belief.address == address) return &belief;
    }
    return nullptr;
}

std::optional<double> WorldIR::parameterValue(const ParameterAddress& address) const noexcept {
    if (address.space == ParameterSpace::Global) {
        const auto it = globalParameters.find(address.name);
        if (it == globalParameters.end()) return std::nullopt;
        return it->second;
    }
    if (!address.entityId.has_value()) return std::nullopt;
    const Entity* entity = findEntity(*address.entityId);
    if (entity == nullptr) return std::nullopt;
    const auto& parameters = address.space == ParameterSpace::Material
                                 ? entity->materialParameters
                                 : entity->constraintParameters;
    const auto it = parameters.find(address.name);
    if (it == parameters.end()) return std::nullopt;
    return it->second;
}

bool WorldIR::setParameterValue(const ParameterAddress& address, double value) noexcept {
    if (!std::isfinite(value)) return false;
    if (address.space == ParameterSpace::Global) {
        const auto it = globalParameters.find(address.name);
        if (it == globalParameters.end()) return false;
        it->second = value;
        return true;
    }
    if (!address.entityId.has_value()) return false;
    Entity* entity = findEntity(*address.entityId);
    if (entity == nullptr) return false;
    auto& parameters = address.space == ParameterSpace::Material
                           ? entity->materialParameters
                           : entity->constraintParameters;
    const auto it = parameters.find(address.name);
    if (it == parameters.end()) return false;
    it->second = value;
    return true;
}

double WorldIR::clampToBelief(const ParameterAddress& address, double value) const noexcept {
    const ParameterBelief* belief = findParameterBelief(address);
    if (belief == nullptr) return value;
    if (belief->lowerBound.has_value() && value < *belief->lowerBound) value = *belief->lowerBound;
    if (belief->upperBound.has_value() && value > *belief->upperBound) value = *belief->upperBound;
    return value;
}

HypothesisValidation WorldIR::validateHypothesis() const {
    HypothesisValidation validation;
    auto fail = [&](std::string message) {
        validation.valid = false;
        validation.errors.push_back(std::move(message));
    };

    std::unordered_set<EntityId> entityIds;
    for (const auto& entity : entities) {
        if (!entityIds.insert(entity.id).second) {
            fail("duplicate entity id: " + std::to_string(entity.id));
        }
        if (entity.parent.has_value() && *entity.parent == entity.id) {
            fail("entity cannot parent itself: " + std::to_string(entity.id));
        }
        for (const auto& [name, value] : entity.materialParameters) {
            if (name.empty() || !std::isfinite(value)) {
                fail("invalid material parameter on entity " + std::to_string(entity.id));
            }
        }
        for (const auto& [name, value] : entity.constraintParameters) {
            if (name.empty() || !std::isfinite(value)) {
                fail("invalid constraint parameter on entity " + std::to_string(entity.id));
            }
        }
    }
    for (const auto& entity : entities) {
        if (entity.parent.has_value() && findEntity(*entity.parent) == nullptr) {
            fail("entity references missing parent: " + std::to_string(entity.id));
        }
    }
    for (const auto& [name, value] : globalParameters) {
        if (name.empty() || !std::isfinite(value)) fail("invalid global parameter: " + name);
    }

    std::unordered_set<std::string> observationIds;
    for (const auto& observation : observations) {
        if (observation.id.empty() || !observationIds.insert(observation.id).second) {
            fail("observation ids must be non-empty and unique: " + observation.id);
        }
        if (observation.observableId.empty()) fail("observation has empty observable id: " + observation.id);
        if (!std::isfinite(observation.timeSeconds)) fail("observation has non-finite time: " + observation.id);
        if (observation.values.empty()) fail("observation has no scalar values: " + observation.id);
        for (const double value : observation.values) {
            if (!std::isfinite(value)) fail("observation contains non-finite value: " + observation.id);
        }
        if (!observation.standardDeviation.empty() &&
            observation.standardDeviation.size() != observation.values.size()) {
            fail("observation uncertainty dimension mismatch: " + observation.id);
        }
        for (const double sigma : observation.standardDeviation) {
            if (!std::isfinite(sigma) || sigma <= 0.0) {
                fail("observation uncertainty must be finite and positive: " + observation.id);
            }
        }
        if (observation.entityId.has_value() && findEntity(*observation.entityId) == nullptr) {
            fail("observation references missing entity: " + observation.id);
        }
    }

    std::vector<ParameterAddress> seenAddresses;
    for (const auto& belief : parameterBeliefs) {
        if (belief.address.name.empty()) fail("parameter belief has empty parameter name");
        for (const auto& seen : seenAddresses) {
            if (seen == belief.address) fail("duplicate parameter belief: " + parameterLabel(belief.address));
        }
        seenAddresses.push_back(belief.address);
        if (belief.address.space == ParameterSpace::Global && belief.address.entityId.has_value()) {
            fail("global parameter belief cannot carry an entity id: " + parameterLabel(belief.address));
        }
        if (belief.address.space != ParameterSpace::Global && !belief.address.entityId.has_value()) {
            fail("entity-scoped parameter belief is missing entity id: " + parameterLabel(belief.address));
        }
        if (belief.lowerBound.has_value() && !std::isfinite(*belief.lowerBound)) {
            fail("parameter lower bound is non-finite: " + parameterLabel(belief.address));
        }
        if (belief.upperBound.has_value() && !std::isfinite(*belief.upperBound)) {
            fail("parameter upper bound is non-finite: " + parameterLabel(belief.address));
        }
        if (belief.lowerBound.has_value() && belief.upperBound.has_value() &&
            *belief.lowerBound > *belief.upperBound) {
            fail("parameter bounds are inverted: " + parameterLabel(belief.address));
        }
        if (belief.standardDeviation.has_value() &&
            (!std::isfinite(*belief.standardDeviation) || *belief.standardDeviation <= 0.0)) {
            fail("parameter standard deviation must be finite and positive: " + parameterLabel(belief.address));
        }
        const auto value = parameterValue(belief.address);
        if (!value.has_value()) {
            fail("parameter belief references missing parameter: " + parameterLabel(belief.address));
        } else {
            if (belief.lowerBound.has_value() && *value < *belief.lowerBound) {
                fail("parameter value is below belief lower bound: " + parameterLabel(belief.address));
            }
            if (belief.upperBound.has_value() && *value > *belief.upperBound) {
                fail("parameter value is above belief upper bound: " + parameterLabel(belief.address));
            }
        }
    }

    return validation;
}

} // namespace vulkax::world
