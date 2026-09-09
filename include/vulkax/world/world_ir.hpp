#pragma once

#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/problem/problem_ir.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vulkax::world {

using EntityId = std::uint64_t;

enum class EvidenceClass : std::uint8_t {
    Unknown,
    Measured,
    Derived,
    ModelProxy,
    LiteratureProxy,
    Synthetic,
};

enum class ParameterSpace : std::uint8_t { Material, Constraint, Global };
enum class ObservationRole : std::uint8_t { Fit, Validation, Initialization, Diagnostic };
enum class ObservationSpace : std::uint8_t {
    Dimensionless,
    PhysicalSI,
    ImagePixels,
    NormalizedImage,
    LinearRadiance,
};

[[nodiscard]] constexpr const char* toString(ObservationSpace space) noexcept {
    switch (space) {
        case ObservationSpace::Dimensionless: return "dimensionless";
        case ObservationSpace::PhysicalSI: return "physical_si";
        case ObservationSpace::ImagePixels: return "image_pixels";
        case ObservationSpace::NormalizedImage: return "normalized_image";
        case ObservationSpace::LinearRadiance: return "linear_radiance";
    }
    return "unknown";
}

struct ParameterAddress {
    ParameterSpace space{ParameterSpace::Material};
    std::optional<EntityId> entityId;
    std::string name;

    friend bool operator==(const ParameterAddress&, const ParameterAddress&) = default;
};

struct ParameterBelief {
    ParameterAddress address;
    std::optional<double> lowerBound;
    std::optional<double> upperBound;
    std::optional<double> standardDeviation;
    EvidenceClass evidence{EvidenceClass::Unknown};
    std::string source;
};

struct ObservationRecord {
    std::string id;
    std::string observableId;
    std::optional<EntityId> entityId;
    double timeSeconds{};
    std::vector<double> values;
    std::vector<double> standardDeviation;
    EvidenceClass evidence{EvidenceClass::Unknown};
    std::string source;
    ObservationRole role{ObservationRole::Fit};
    ObservationSpace space{ObservationSpace::Dimensionless};
};

struct Entity {
    EntityId id{};
    std::string name;
    std::optional<EntityId> parent;
    std::unordered_map<std::string, double> materialParameters;
    std::unordered_map<std::string, double> constraintParameters;
};

struct ProvenanceRecord {
    std::uint64_t revision{};
    std::string transactionId;
    std::string author;
    std::string summary;
};

struct HypothesisValidation {
    bool valid{true};
    std::vector<std::string> errors;
};

struct WorldIR {
    std::string id;
    gaussian::GaussianCloud appearance;
    std::vector<Entity> entities;
    std::uint64_t revision{};
    std::vector<ProvenanceRecord> provenance;

    // Schema 3 makes the observation coordinate/value space explicit. That is
    // required before image-space evidence can coexist with physical SI evidence
    // inside one executable hypothesis without silently mixing incompatible units.
    std::uint32_t schemaVersion{3};
    std::optional<problem::ProblemIR> physics;
    std::vector<ObservationRecord> observations;
    std::vector<ParameterBelief> parameterBeliefs;
    std::unordered_map<std::string, double> globalParameters;

    [[nodiscard]] Entity* findEntity(EntityId id) noexcept;
    [[nodiscard]] const Entity* findEntity(EntityId id) const noexcept;
    [[nodiscard]] ObservationRecord* findObservation(std::string_view id) noexcept;
    [[nodiscard]] const ObservationRecord* findObservation(std::string_view id) const noexcept;
    [[nodiscard]] ParameterBelief* findParameterBelief(const ParameterAddress& address) noexcept;
    [[nodiscard]] const ParameterBelief* findParameterBelief(const ParameterAddress& address) const noexcept;
    [[nodiscard]] std::optional<double> parameterValue(const ParameterAddress& address) const noexcept;
    [[nodiscard]] bool setParameterValue(const ParameterAddress& address, double value) noexcept;
    [[nodiscard]] double clampToBelief(const ParameterAddress& address, double value) const noexcept;
    [[nodiscard]] HypothesisValidation validateHypothesis() const;
};

} // namespace vulkax::world
