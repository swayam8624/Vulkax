#include "vulkax/world/world_ir.hpp"

namespace vulkax::world {

Entity* WorldIR::findEntity(EntityId id) noexcept {
    for (auto& entity : entities) {
        if (entity.id == id) return &entity;
    }
    return nullptr;
}

const Entity* WorldIR::findEntity(EntityId id) const noexcept {
    for (const auto& entity : entities) {
        if (entity.id == id) return &entity;
    }
    return nullptr;
}

} // namespace vulkax::world
