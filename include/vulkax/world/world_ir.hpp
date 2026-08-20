#pragma once

#include "vulkax/gaussian/gaussian_cloud.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vulkax::world {

using EntityId = std::uint64_t;

struct Entity {
    EntityId id{};
    std::string name;
    std::optional<EntityId> parent;
    std::unordered_map<std::string, double> materialParameters;
};

struct ProvenanceRecord {
    std::uint64_t revision{};
    std::string transactionId;
    std::string author;
    std::string summary;
};

struct WorldIR {
    std::string id;
    gaussian::GaussianCloud appearance;
    std::vector<Entity> entities;
    std::uint64_t revision{};
    std::vector<ProvenanceRecord> provenance;

    [[nodiscard]] Entity* findEntity(EntityId id) noexcept;
    [[nodiscard]] const Entity* findEntity(EntityId id) const noexcept;
};

} // namespace vulkax::world
