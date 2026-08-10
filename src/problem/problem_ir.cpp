#include "vulkax/problem/problem_ir.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

namespace vulkax::problem {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kFnvPrime;
    }
}

void hashString(std::uint64_t& hash, std::string_view text) {
    const std::uint64_t length = static_cast<std::uint64_t>(text.size());
    hashBytes(hash, &length, sizeof(length));
    hashBytes(hash, text.data(), text.size());
}

template <typename T> void hashPod(std::uint64_t& hash, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    hashBytes(hash, &value, sizeof(T));
}

void hashDimension(std::uint64_t& hash, const units::Dimension& dimension) {
    hashBytes(hash, dimension.exponent.data(), dimension.exponent.size());
}

template <typename T> std::vector<const T*> sortedById(const std::vector<T>& values) {
    std::vector<const T*> sorted;
    sorted.reserve(values.size());
    for (const auto& value : values) {
        sorted.push_back(&value);
    }
    std::sort(sorted.begin(), sorted.end(), [](const T* lhs, const T* rhs) {
        return lhs->id < rhs->id;
    });
    return sorted;
}

} // namespace

std::uint64_t stableProblemHash(const ProblemIR& problem) {
    std::uint64_t hash = kFnvOffset;
    hashPod(hash, problem.schemaVersion);
    hashString(hash, problem.id);

    for (const Domain* domain : sortedById(problem.domains)) {
        hashString(hash, domain->id);
        hashPod(hash, domain->kind);
        hashPod(hash, domain->spatialDimensions);
    }

    for (const Field* field : sortedById(problem.fields)) {
        hashString(hash, field->id);
        hashString(hash, field->domainId);
        hashPod(hash, field->rank);
        hashPod(hash, field->components);
        hashDimension(hash, field->physicalDimension);
    }

    for (const ResidualOperator* op : sortedById(problem.operators)) {
        hashString(hash, op->id);
        hashString(hash, op->outputFieldId);
        hashString(hash, op->expression);
        hashString(hash, op->family);
        auto inputs = op->inputFieldIds;
        std::sort(inputs.begin(), inputs.end());
        for (const auto& input : inputs) {
            hashString(hash, input);
        }
    }

    for (const Material* material : sortedById(problem.materials)) {
        hashString(hash, material->id);
        auto properties = material->properties;
        std::sort(properties.begin(), properties.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.name < rhs.name;
        });
        for (const auto& property : properties) {
            hashString(hash, property.name);
            const auto bits = std::bit_cast<std::uint64_t>(property.value.valueSI);
            hashPod(hash, bits);
            hashDimension(hash, property.value.dimension);
        }
    }

    for (const BoundaryCondition* boundary : sortedById(problem.boundaryConditions)) {
        hashString(hash, boundary->id);
        hashString(hash, boundary->domainId);
        hashString(hash, boundary->fieldId);
        hashString(hash, boundary->kind);
        hashDimension(hash, boundary->physicalDimension);
        for (double value : boundary->valuesSI) {
            const auto bits = std::bit_cast<std::uint64_t>(value);
            hashPod(hash, bits);
        }
    }

    for (const Objective* objective : sortedById(problem.objectives)) {
        hashString(hash, objective->id);
        hashString(hash, objective->expression);
        hashPod(hash, objective->direction);
    }

    auto accuracy = problem.accuracyTargets;
    std::sort(accuracy.begin(), accuracy.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.observableId < rhs.observableId;
    });
    for (const auto& target : accuracy) {
        hashString(hash, target.observableId);
        hashPod(hash, std::bit_cast<std::uint64_t>(target.relativeTolerance));
        const bool hasAbsolute = target.absoluteTolerance.has_value();
        hashPod(hash, hasAbsolute);
        if (target.absoluteTolerance) {
            hashPod(hash, std::bit_cast<std::uint64_t>(*target.absoluteTolerance));
        }
    }

    if (problem.computeBudget.wallSeconds) {
        hashPod(hash, std::bit_cast<std::uint64_t>(*problem.computeBudget.wallSeconds));
    }
    if (problem.computeBudget.gpuMemoryBytes) {
        hashPod(hash, *problem.computeBudget.gpuMemoryBytes);
    }

    return hash;
}

} // namespace vulkax::problem
