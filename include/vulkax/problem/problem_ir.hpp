#pragma once

#include "vulkax/core/units.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vulkax::problem {

enum class DomainKind : std::uint8_t {
    Volume,
    Surface,
    Curve,
    ParticleSet,
    RigidAssembly,
    RayBundle,
};

enum class FieldRank : std::uint8_t { Scalar, Vector, Tensor };
enum class ObjectiveDirection : std::uint8_t { Observe, Minimize, Maximize, MatchTarget };

struct Domain {
    std::string id;
    DomainKind kind{DomainKind::Volume};
    std::uint8_t spatialDimensions{3};
};

struct Field {
    std::string id;
    std::string domainId;
    FieldRank rank{FieldRank::Scalar};
    std::uint32_t components{1};
    units::Dimension physicalDimension{units::dimensionless};
};

// A governing law is represented as residual operators instead of a solver-specific kernel.
// The decomposition is intentionally first-class so future adjoint/operator-attribution work can
// ask how individual mechanisms influence an observable.
struct ResidualOperator {
    std::string id;
    std::string label;
    std::string outputFieldId;
    std::vector<std::string> inputFieldIds;
    std::string expression;
    std::string family;
};

struct MaterialProperty {
    std::string name;
    units::Quantity value;
};

struct Material {
    std::string id;
    std::vector<MaterialProperty> properties;
};

struct BoundaryCondition {
    std::string id;
    std::string domainId;
    std::string fieldId;
    std::string kind;
    std::vector<double> valuesSI;
    units::Dimension physicalDimension{units::dimensionless};
};

struct Objective {
    std::string id;
    std::string label;
    std::string expression;
    ObjectiveDirection direction{ObjectiveDirection::Observe};
};

struct AccuracyTarget {
    std::string observableId;
    double relativeTolerance{0.0};
    std::optional<double> absoluteTolerance;
};

struct ComputeBudget {
    std::optional<double> wallSeconds;
    std::optional<std::uint64_t> gpuMemoryBytes;
};

struct ProblemIR {
    std::uint32_t schemaVersion{1};
    std::string id;
    std::string name;
    std::vector<Domain> domains;
    std::vector<Field> fields;
    std::vector<ResidualOperator> operators;
    std::vector<Material> materials;
    std::vector<BoundaryCondition> boundaryConditions;
    std::vector<Objective> objectives;
    std::vector<AccuracyTarget> accuracyTargets;
    ComputeBudget computeBudget;
};

[[nodiscard]] std::uint64_t stableProblemHash(const ProblemIR& problem);

} // namespace vulkax::problem
