#pragma once

#include "vulkax/core/math.hpp"
#include "vulkax/world/correspondence_graph.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vulkax::world {

struct TranslateEntity {
    EntityId entity{};
    math::Vec3 delta{};
};

struct SetMaterialParameter {
    EntityId entity{};
    std::string name;
    double value{};
};

struct SetConstraintParameter {
    EntityId entity{};
    std::string name;
    double value{};
};

using WorldEdit = std::variant<TranslateEntity, SetMaterialParameter, SetConstraintParameter>;

struct WorldTransaction {
    std::string id;
    std::string author;
    std::string summary;
    std::vector<WorldEdit> edits;
    std::optional<std::uint64_t> expectedRevision;
};

struct TransactionValidation {
    bool valid{true};
    bool requiresAppearancePropagation{};
    bool requiresPhysicalRerun{};
    bool requiresIndependentOracle{};
    std::vector<std::string> errors;
    std::vector<gaussian::GaussianId> touchedGaussians;
    std::vector<EntityId> touchedEntities;
};

struct GaussianPositionSnapshot {
    gaussian::GaussianId id{};
    math::Vec3 position{};
};

struct EntityMaterialSnapshot {
    EntityId entity{};
    std::unordered_map<std::string, double> materialParameters;
};

struct EntityConstraintSnapshot {
    EntityId entity{};
    std::unordered_map<std::string, double> constraintParameters;
};

struct TransactionReceipt {
    std::uint64_t revisionBefore{};
    std::uint64_t revisionAfter{};
    std::vector<gaussian::GaussianId> touchedGaussians;
    std::vector<EntityId> touchedEntities;
    std::vector<GaussianPositionSnapshot> previousPositions;
    std::vector<EntityMaterialSnapshot> previousMaterials;
    std::vector<EntityConstraintSnapshot> previousConstraints;
};

[[nodiscard]] TransactionValidation validateTransaction(const WorldIR& world,
                                                        const WorldCorrespondenceGraph& graph,
                                                        const WorldTransaction& transaction);
[[nodiscard]] TransactionReceipt applyTransaction(WorldIR& world,
                                                  const WorldCorrespondenceGraph& graph,
                                                  const WorldTransaction& transaction);
void rollbackTransaction(WorldIR& world, const TransactionReceipt& receipt);

// Measures the maximum positional change outside a transaction's stable-ID
// touched set. Cloud storage order may differ between before/after.
[[nodiscard]] double unaffectedPositionDrift(
    const gaussian::GaussianCloud& before,
    const gaussian::GaussianCloud& after,
    const std::vector<gaussian::GaussianId>& touchedGaussians);

} // namespace vulkax::world
