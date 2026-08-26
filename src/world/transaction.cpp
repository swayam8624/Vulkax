#include "vulkax/world/transaction.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace vulkax::world {
namespace {

template <typename T>
void appendUnique(std::vector<T>& values, const T& value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

[[nodiscard]] bool finiteVec3(math::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] std::string validationMessage(const TransactionValidation& validation) {
    std::ostringstream stream;
    stream << "world transaction validation failed";
    for (const auto& error : validation.errors) stream << "; " << error;
    return stream.str();
}

void snapshotEntityMaterial(const WorldIR& world, TransactionReceipt& receipt, EntityId entityId) {
    if (std::find_if(receipt.previousMaterials.begin(), receipt.previousMaterials.end(),
                     [entityId](const EntityMaterialSnapshot& snapshot) { return snapshot.entity == entityId; }) != receipt.previousMaterials.end())
        return;
    const auto* entity = world.findEntity(entityId);
    if (entity == nullptr) throw std::runtime_error("transaction snapshot references missing entity " + std::to_string(entityId));
    receipt.previousMaterials.push_back({entityId, entity->materialParameters});
}

void snapshotEntityConstraints(const WorldIR& world, TransactionReceipt& receipt, EntityId entityId) {
    if (std::find_if(receipt.previousConstraints.begin(), receipt.previousConstraints.end(),
                     [entityId](const EntityConstraintSnapshot& snapshot) { return snapshot.entity == entityId; }) != receipt.previousConstraints.end())
        return;
    const auto* entity = world.findEntity(entityId);
    if (entity == nullptr) throw std::runtime_error("transaction snapshot references missing entity " + std::to_string(entityId));
    receipt.previousConstraints.push_back({entityId, entity->constraintParameters});
}

void snapshotGaussian(const WorldIR& world,
                      const gaussian::GaussianIndexView& indexView,
                      TransactionReceipt& receipt,
                      gaussian::GaussianId id) {
    if (std::find_if(receipt.previousPositions.begin(), receipt.previousPositions.end(),
                     [id](const GaussianPositionSnapshot& snapshot) { return snapshot.id == id; }) != receipt.previousPositions.end())
        return;
    const auto index = indexView.requireIndex(id);
    receipt.previousPositions.push_back({id, world.appearance.splats[index].position});
}

} // namespace

TransactionValidation validateTransaction(const WorldIR& world,
                                          const WorldCorrespondenceGraph& graph,
                                          const WorldTransaction& transaction) {
    TransactionValidation validation;

    if (transaction.id.empty()) validation.errors.push_back("transaction id must not be empty");
    if (transaction.author.empty()) validation.errors.push_back("transaction author must not be empty");
    if (transaction.summary.empty()) validation.errors.push_back("transaction summary must not be empty");
    if (transaction.edits.empty()) validation.errors.push_back("transaction must contain at least one edit");
    if (transaction.expectedRevision.has_value() && *transaction.expectedRevision != world.revision)
        validation.errors.push_back("expected revision does not match current world revision");
    if (!transaction.id.empty() &&
        std::any_of(world.provenance.begin(), world.provenance.end(),
                    [&](const ProvenanceRecord& record) { return record.transactionId == transaction.id; }))
        validation.errors.push_back("transaction id has already been committed");

    const auto graphValidation = graph.validate(world);
    if (!graphValidation.valid) {
        for (const auto& error : graphValidation.errors)
            validation.errors.push_back("correspondence graph: " + error);
    }

    std::optional<gaussian::GaussianIndexView> indexView;
    try {
        indexView.emplace(world.appearance);
    } catch (const std::exception& error) {
        validation.errors.push_back(std::string("appearance identity: ") + error.what());
    }

    for (const auto& edit : transaction.edits) {
        std::visit(
            [&](const auto& operation) {
                using Operation = std::decay_t<decltype(operation)>;
                const auto* entity = world.findEntity(operation.entity);
                if (entity == nullptr) {
                    validation.errors.push_back("edit references missing entity " + std::to_string(operation.entity));
                    return;
                }
                appendUnique(validation.touchedEntities, operation.entity);

                if constexpr (std::is_same_v<Operation, TranslateEntity>) {
                    if (!finiteVec3(operation.delta)) {
                        validation.errors.push_back("geometry rewrite requires a finite translation delta");
                        return;
                    }
                    const auto& ids = graph.gaussiansForEntity(operation.entity);
                    if (ids.empty()) {
                        validation.errors.push_back("geometry rewrite requires appearance correspondence");
                        return;
                    }
                    for (const auto id : ids) {
                        if (!indexView.has_value() || !indexView->contains(id)) {
                            validation.errors.push_back("geometry rewrite correspondence references missing Gaussian ID " +
                                                        gaussian::toString(id));
                            continue;
                        }
                        appendUnique(validation.touchedGaussians, id);
                    }
                    validation.requiresAppearancePropagation = true;
                    if (!graph.physicalBindings(operation.entity).empty()) validation.requiresPhysicalRerun = true;
                } else if constexpr (std::is_same_v<Operation, SetMaterialParameter>) {
                    if (operation.name.empty() || !std::isfinite(operation.value)) {
                        validation.errors.push_back("material rewrite must use a finite named parameter");
                        return;
                    }
                    if (graph.physicalBindings(operation.entity).empty()) {
                        validation.errors.push_back("material rewrite requires physical correspondence");
                        return;
                    }
                    validation.requiresPhysicalRerun = true;
                    validation.requiresIndependentOracle = true;
                } else if constexpr (std::is_same_v<Operation, SetConstraintParameter>) {
                    if (operation.name.empty() || !std::isfinite(operation.value)) {
                        validation.errors.push_back("constraint rewrite must use a finite named parameter");
                        return;
                    }
                    if (graph.physicalBindings(operation.entity).empty()) {
                        validation.errors.push_back("constraint rewrite requires physical correspondence");
                        return;
                    }
                    validation.requiresPhysicalRerun = true;
                }
            },
            edit);
    }

    validation.valid = validation.errors.empty();
    return validation;
}

TransactionReceipt applyTransaction(WorldIR& world,
                                    const WorldCorrespondenceGraph& graph,
                                    const WorldTransaction& transaction) {
    const auto validation = validateTransaction(world, graph, transaction);
    if (!validation.valid) throw std::runtime_error(validationMessage(validation));

    TransactionReceipt receipt;
    receipt.revisionBefore = world.revision;
    receipt.revisionAfter = world.revision + 1U;
    receipt.touchedGaussians = validation.touchedGaussians;
    receipt.touchedEntities = validation.touchedEntities;

    const gaussian::GaussianIndexView worldIndex(world.appearance);
    for (const auto id : validation.touchedGaussians) snapshotGaussian(world, worldIndex, receipt, id);
    for (const auto& edit : transaction.edits) {
        std::visit(
            [&](const auto& operation) {
                using Operation = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<Operation, SetMaterialParameter>)
                    snapshotEntityMaterial(world, receipt, operation.entity);
                else if constexpr (std::is_same_v<Operation, SetConstraintParameter>)
                    snapshotEntityConstraints(world, receipt, operation.entity);
            },
            edit);
    }

    WorldIR candidate = world;
    const gaussian::GaussianIndexView candidateIndex(candidate.appearance);
    for (const auto& edit : transaction.edits) {
        std::visit(
            [&](const auto& operation) {
                using Operation = std::decay_t<decltype(operation)>;
                auto* entity = candidate.findEntity(operation.entity);
                if (entity == nullptr) throw std::runtime_error("validated transaction lost its target entity");

                if constexpr (std::is_same_v<Operation, TranslateEntity>) {
                    for (const auto id : graph.gaussiansForEntity(operation.entity))
                        candidate.appearance.splats[candidateIndex.requireIndex(id)].position += operation.delta;
                } else if constexpr (std::is_same_v<Operation, SetMaterialParameter>) {
                    entity->materialParameters[operation.name] = operation.value;
                } else if constexpr (std::is_same_v<Operation, SetConstraintParameter>) {
                    entity->constraintParameters[operation.name] = operation.value;
                }
            },
            edit);
    }

    candidate.revision = receipt.revisionAfter;
    candidate.provenance.push_back({candidate.revision, transaction.id, transaction.author, transaction.summary});
    world = std::move(candidate);
    return receipt;
}

void rollbackTransaction(WorldIR& world, const TransactionReceipt& receipt) {
    if (world.revision != receipt.revisionAfter)
        throw std::runtime_error("transaction rollback requires the receipt to match the current world revision");

    const gaussian::GaussianIndexView indexView(world.appearance);
    for (const auto& snapshot : receipt.previousPositions)
        world.appearance.splats[indexView.requireIndex(snapshot.id)].position = snapshot.position;
    for (const auto& snapshot : receipt.previousMaterials) {
        auto* entity = world.findEntity(snapshot.entity);
        if (entity == nullptr) throw std::runtime_error("rollback references missing material entity");
        entity->materialParameters = snapshot.materialParameters;
    }
    for (const auto& snapshot : receipt.previousConstraints) {
        auto* entity = world.findEntity(snapshot.entity);
        if (entity == nullptr) throw std::runtime_error("rollback references missing constraint entity");
        entity->constraintParameters = snapshot.constraintParameters;
    }
    if (!world.provenance.empty() && world.provenance.back().revision == receipt.revisionAfter) world.provenance.pop_back();
    world.revision = receipt.revisionBefore;
}

double unaffectedPositionDrift(const gaussian::GaussianCloud& before,
                               const gaussian::GaussianCloud& after,
                               const std::vector<gaussian::GaussianId>& touchedGaussians) {
    if (before.size() != after.size())
        throw std::invalid_argument("unaffected-region drift requires clouds with equal stable-ID cardinality");
    const gaussian::GaussianIndexView beforeIndex(before);
    const gaussian::GaussianIndexView afterIndex(after);
    std::unordered_set<gaussian::GaussianId, gaussian::GaussianIdHash> touched(
        touchedGaussians.begin(), touchedGaussians.end());
    double maximum = 0.0;
    for (const auto& splat : before.splats) {
        if (touched.contains(splat.id)) continue;
        const auto afterStorageIndex = afterIndex.index(splat.id);
        if (!afterStorageIndex.has_value())
            throw std::invalid_argument("unaffected-region drift found a missing Gaussian ID " +
                                        gaussian::toString(splat.id));
        const auto beforeStorageIndex = beforeIndex.requireIndex(splat.id);
        maximum = std::max(
            maximum,
            math::length(after.splats[*afterStorageIndex].position - before.splats[beforeStorageIndex].position));
    }
    return maximum;
}

} // namespace vulkax::world
