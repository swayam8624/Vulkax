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
    void bindGaussian(gaussian::GaussianId gaussianId, EntityId entity);
    void bindPhysical(EntityId entity, PhysicalBinding binding);

    [[nodiscard]] std::optional<EntityId> entityForGaussian(gaussian::GaussianId gaussianId) const;
    [[nodiscard]] const std::vector<gaussian::GaussianId>& gaussiansForEntity(EntityId entity) const noexcept;
    [[nodiscard]] const std::vector<PhysicalBinding>& physicalBindings(EntityId entity) const noexcept;
    [[nodiscard]] std::size_t gaussianBindingCount() const noexcept { return gaussianToEntity_.size(); }

    // Drops only appearance bindings whose stable Gaussian IDs are absent from
    // the supplied filtered cloud. Entity and physical bindings remain intact.
    // Returns the number of removed Gaussian bindings.
    std::size_t pruneMissingGaussians(const gaussian::GaussianCloud& cloud);

    [[nodiscard]] CorrespondenceValidation validate(const WorldIR& world) const;

private:
    std::unordered_map<gaussian::GaussianId, EntityId, gaussian::GaussianIdHash> gaussianToEntity_;
    std::unordered_map<EntityId, std::vector<gaussian::GaussianId>> entityToGaussians_;
    std::unordered_map<EntityId, std::vector<PhysicalBinding>> entityToPhysical_;
};

} // namespace vulkax::world
