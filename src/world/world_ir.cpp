#include "vulkax/world/world_ir.hpp"

namespace vulkax::world {

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

} // namespace vulkax::world
