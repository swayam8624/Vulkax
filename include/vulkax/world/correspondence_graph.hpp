#pragma once

#include "vulkax/world/world_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vulkax::world {

enum class PhysicalKind {
    SurfaceVertex,
    FemNode,
    MpmParticle,
    DemParticle,
    GridCell,
    RigidBody,
};

struct PhysicalBinding {
    PhysicalKind kind{PhysicalKind::SurfaceVertex};
    std::uint64_t index{};
    double weight{1.0};
};

struct CorrespondenceValidation {
    bool valid{true};
    std::vector<std::string> errors;
};

class WorldCorrespondenceGraph {
public:
    void bindGaussian(std::size_t gaussianIndex, EntityId entity);
    void bindPhysical(EntityId entity, PhysicalBinding binding);

    [[nodiscard]] std::optional<EntityId> entityForGaussian(std::size_t gaussianIndex) const;
    [[nodiscard]] const std::vector<std::size_t>& gaussiansForEntity(EntityId entity) const noexcept;
    [[nodiscard]] const std::vector<PhysicalBinding>& physicalBindings(EntityId entity) const noexcept;
    [[nodiscard]] std::size_t gaussianBindingCount() const noexcept { return gaussianToEntity_.size(); }
    [[nodiscard]] CorrespondenceValidation validate(const WorldIR& world) const;

private:
    std::unordered_map<std::size_t, EntityId> gaussianToEntity_;
    std::unordered_map<EntityId, std::vector<std::size_t>> entityToGaussians_;
    std::unordered_map<EntityId, std::vector<PhysicalBinding>> entityToPhysical_;
};

} // namespace vulkax::world
