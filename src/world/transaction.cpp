#include "vulkax/world/transaction.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace vulkax::world {
namespace {

template <typename T>
void appendUnique(std::vector<T>& values, T value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

void snapshotEntityMaterial(WorldIR& world, TransactionReceipt& receipt, EntityId entityId) {
    if (std::find_if(receipt.previousMaterials.begin(), receipt.previousMaterials.end(),
                     [entityId](const EntityMaterialSnapshot& snapshot) { return snapshot.entity == entityId; }) != receipt.previousMaterials.end())
        return;
    const auto* entity = world.findEntity(entityId);
    if (entity == nullptr) throw std::runtime_error("transaction references missing entity " + std::to_string(entityId));
    receipt.previousMaterials.push_back({entityId, entity->materialParameters});
}

void snapshotGaussian(WorldIR& world, TransactionReceipt& receipt, std::size_t index) {
    if (index >= world.appearance.size()) throw std::runtime_error("transaction correspondence references invalid Gaussian index");
    if (std::find_if(receipt.previousPositions.begin(), receipt.previousPositions.end(),
                     [index](const GaussianPositionSnapshot& snapshot) { return snapshot.index == index; }) == receipt.previousPositions.end())
        receipt.previousPositions.push_back({index, world.appearance.splats[index].position});
}

} // namespace

TransactionReceipt applyTransaction(WorldIR& world,
                                    const WorldCorrespondenceGraph& graph,
                                    const WorldTransaction& transaction) {
    if (transaction.id.empty()) throw std::runtime_error("world transaction id must not be empty");

    TransactionReceipt receipt;
    receipt.revisionBefore = world.revision;

    for (const auto& edit : transaction.edits) {
        std::visit(
            [&](const auto& operation) {
                using Operation = std::decay_t<decltype(operation)>;
                auto* entity = world.findEntity(operation.entity);
                if (entity == nullptr) throw std::runtime_error("transaction references missing entity " + std::to_string(operation.entity));
                appendUnique(receipt.touchedEntities, operation.entity);

                if constexpr (std::is_same_v<Operation, TranslateEntity>) {
                    const auto& indices = graph.gaussiansForEntity(operation.entity);
                    if (indices.empty()) throw std::runtime_error("cannot translate an entity with no appearance correspondence");
                    for (const auto index : indices) {
                        snapshotGaussian(world, receipt, index);
                        world.appearance.splats[index].position += operation.delta;
                        appendUnique(receipt.touchedGaussians, index);
                    }
                } else if constexpr (std::is_same_v<Operation, SetMaterialParameter>) {
                    if (operation.name.empty() || !std::isfinite(operation.value))
                        throw std::runtime_error("material rewrite must use a finite named parameter");
                    snapshotEntityMaterial(world, receipt, operation.entity);
                    entity->materialParameters[operation.name] = operation.value;
                }
            },
            edit);
    }

    ++world.revision;
    receipt.revisionAfter = world.revision;
    world.provenance.push_back({world.revision, transaction.id, transaction.author, transaction.summary});
    return receipt;
}

void rollbackTransaction(WorldIR& world, const TransactionReceipt& receipt) {
    if (world.revision != receipt.revisionAfter)
        throw std::runtime_error("transaction rollback requires the receipt to match the current world revision");

    for (const auto& snapshot : receipt.previousPositions) {
        if (snapshot.index >= world.appearance.size()) throw std::runtime_error("rollback Gaussian index is out of range");
        world.appearance.splats[snapshot.index].position = snapshot.position;
    }
    for (const auto& snapshot : receipt.previousMaterials) {
        auto* entity = world.findEntity(snapshot.entity);
        if (entity == nullptr) throw std::runtime_error("rollback references missing entity");
        entity->materialParameters = snapshot.materialParameters;
    }
    if (!world.provenance.empty() && world.provenance.back().revision == receipt.revisionAfter) world.provenance.pop_back();
    world.revision = receipt.revisionBefore;
}

double unaffectedPositionDrift(const gaussian::GaussianCloud& before,
                               const gaussian::GaussianCloud& after,
                               const std::vector<std::size_t>& touchedGaussians) {
    if (before.size() != after.size()) throw std::invalid_argument("unaffected-region drift requires clouds with equal splat counts");
    std::unordered_set<std::size_t> touched(touchedGaussians.begin(), touchedGaussians.end());
    double maximum = 0.0;
    for (std::size_t index = 0; index < before.size(); ++index) {
        if (touched.contains(index)) continue;
        maximum = std::max(maximum, math::length(after.splats[index].position - before.splats[index].position));
    }
    return maximum;
}

} // namespace vulkax::world
