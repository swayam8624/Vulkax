#include "vulkax/problem/document.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace vulkax::problem {

namespace {

DomainKind parseDomainKind(const std::string& value) {
    if (value == "volume") return DomainKind::Volume;
    if (value == "surface") return DomainKind::Surface;
    if (value == "curve") return DomainKind::Curve;
    if (value == "particles") return DomainKind::ParticleSet;
    if (value == "rigid") return DomainKind::RigidAssembly;
    if (value == "rays") return DomainKind::RayBundle;
    throw std::invalid_argument("unknown domain kind: " + value);
}

const char* domainKindName(DomainKind value) {
    switch (value) {
    case DomainKind::Volume: return "volume";
    case DomainKind::Surface: return "surface";
    case DomainKind::Curve: return "curve";
    case DomainKind::ParticleSet: return "particles";
    case DomainKind::RigidAssembly: return "rigid";
    case DomainKind::RayBundle: return "rays";
    }
    return "unknown";
}

FieldRank parseFieldRank(const std::string& value) {
    if (value == "scalar") return FieldRank::Scalar;
    if (value == "vector") return FieldRank::Vector;
    if (value == "tensor") return FieldRank::Tensor;
    throw std::invalid_argument("unknown field rank: " + value);
}

const char* fieldRankName(FieldRank value) {
    switch (value) {
    case FieldRank::Scalar: return "scalar";
    case FieldRank::Vector: return "vector";
    case FieldRank::Tensor: return "tensor";
    }
    return "unknown";
}

ObjectiveDirection parseDirection(const std::string& value) {
    if (value == "observe") return ObjectiveDirection::Observe;
    if (value == "minimize") return ObjectiveDirection::Minimize;
    if (value == "maximize") return ObjectiveDirection::Maximize;
    if (value == "match") return ObjectiveDirection::MatchTarget;
    throw std::invalid_argument("unknown objective direction: " + value);
}

const char* directionName(ObjectiveDirection value) {
    switch (value) {
    case ObjectiveDirection::Observe: return "observe";
    case ObjectiveDirection::Minimize: return "minimize";
    case ObjectiveDirection::Maximize: return "maximize";
    case ObjectiveDirection::MatchTarget: return "match";
    }
    return "unknown";
}

units::Dimension readDimension(std::istringstream& stream) {
    units::Dimension dimension;
    for (auto& exponent : dimension.exponent) {
        int value = 0;
        if (!(stream >> value) || value < -127 || value > 127) {
            throw std::invalid_argument("expected seven valid SI base-dimension exponents");
        }
        exponent = static_cast<std::int8_t>(value);
    }
    return dimension;
}

void writeDimension(std::ostream& stream, units::Dimension dimension) {
    for (const auto exponent : dimension.exponent) stream << ' ' << static_cast<int>(exponent);
}

std::runtime_error lineError(std::size_t line, const std::string& message) {
    return std::runtime_error("problem document line " + std::to_string(line) + ": " + message);
}

} // namespace

ProblemIR parseProblemDocument(std::string_view source) {
    ProblemIR result;
    std::istringstream input{std::string(source)};
    std::string line;
    std::size_t lineNumber = 0;
    bool headerSeen = false;
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream stream(line.substr(first));
        std::string command;
        stream >> command;
        try {
            if (command == "vulkax") {
                unsigned version = 0;
                if (!(stream >> version) || version != 1) throw std::invalid_argument("only document schema 1 is supported");
                result.schemaVersion = version;
                headerSeen = true;
            } else if (command == "id") {
                if (!(stream >> std::quoted(result.id))) throw std::invalid_argument("expected quoted problem id");
            } else if (command == "name") {
                if (!(stream >> std::quoted(result.name))) throw std::invalid_argument("expected quoted problem name");
            } else if (command == "domain") {
                Domain domain;
                std::string kind;
                unsigned dimensions = 0;
                if (!(stream >> std::quoted(domain.id) >> kind >> dimensions)) throw std::invalid_argument("invalid domain record");
                domain.kind = parseDomainKind(kind);
                domain.spatialDimensions = static_cast<std::uint8_t>(dimensions);
                result.domains.push_back(std::move(domain));
            } else if (command == "field") {
                Field field;
                std::string rank;
                if (!(stream >> std::quoted(field.id) >> std::quoted(field.domainId) >> rank >> field.components))
                    throw std::invalid_argument("invalid field record");
                field.rank = parseFieldRank(rank);
                field.physicalDimension = readDimension(stream);
                result.fields.push_back(std::move(field));
            } else if (command == "operator") {
                ResidualOperator op;
                if (!(stream >> std::quoted(op.id) >> std::quoted(op.label) >> std::quoted(op.outputFieldId)
                             >> std::quoted(op.family) >> std::quoted(op.expression)))
                    throw std::invalid_argument("invalid operator record");
                std::string inputId;
                while (stream >> std::quoted(inputId)) op.inputFieldIds.push_back(inputId);
                result.operators.push_back(std::move(op));
            } else if (command == "material") {
                Material material;
                if (!(stream >> std::quoted(material.id))) throw std::invalid_argument("invalid material record");
                result.materials.push_back(std::move(material));
            } else if (command == "property") {
                std::string materialId;
                MaterialProperty property;
                if (!(stream >> std::quoted(materialId) >> std::quoted(property.name) >> property.value.valueSI))
                    throw std::invalid_argument("invalid material property record");
                property.value.dimension = readDimension(stream);
                const auto material = std::find_if(result.materials.begin(), result.materials.end(), [&](const auto& candidate) {
                    return candidate.id == materialId;
                });
                if (material == result.materials.end()) throw std::invalid_argument("property references material declared later or missing: " + materialId);
                material->properties.push_back(std::move(property));
            } else if (command == "bc") {
                BoundaryCondition boundary;
                if (!(stream >> std::quoted(boundary.id) >> std::quoted(boundary.domainId)
                             >> std::quoted(boundary.fieldId) >> std::quoted(boundary.kind)))
                    throw std::invalid_argument("invalid boundary-condition record");
                boundary.physicalDimension = readDimension(stream);
                std::size_t count = 0;
                if (!(stream >> count)) throw std::invalid_argument("boundary condition requires value count");
                boundary.valuesSI.resize(count);
                for (double& value : boundary.valuesSI) if (!(stream >> value)) throw std::invalid_argument("missing boundary value");
                result.boundaryConditions.push_back(std::move(boundary));
            } else if (command == "objective") {
                Objective objective;
                std::string direction;
                if (!(stream >> std::quoted(objective.id) >> std::quoted(objective.label) >> direction
                             >> std::quoted(objective.expression))) throw std::invalid_argument("invalid objective record");
                objective.direction = parseDirection(direction);
                result.objectives.push_back(std::move(objective));
            } else if (command == "accuracy") {
                AccuracyTarget target;
                int hasAbsolute = 0;
                double absolute = 0.0;
                if (!(stream >> std::quoted(target.observableId) >> target.relativeTolerance >> hasAbsolute >> absolute))
                    throw std::invalid_argument("invalid accuracy record");
                if (hasAbsolute != 0) target.absoluteTolerance = absolute;
                result.accuracyTargets.push_back(std::move(target));
            } else if (command == "budget_wall") {
                double value = 0.0;
                if (!(stream >> value)) throw std::invalid_argument("invalid wall-time budget");
                result.computeBudget.wallSeconds = value;
            } else if (command == "budget_memory") {
                std::uint64_t value = 0;
                if (!(stream >> value)) throw std::invalid_argument("invalid memory budget");
                result.computeBudget.gpuMemoryBytes = value;
            } else {
                throw std::invalid_argument("unknown command: " + command);
            }
        } catch (const std::exception& error) {
            throw lineError(lineNumber, error.what());
        }
    }
    if (!headerSeen) throw std::runtime_error("problem document is missing 'vulkax 1' header");
    return result;
}

ProblemIR loadProblemDocument(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("failed to open problem document: " + path);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return parseProblemDocument(contents.str());
}

std::string writeProblemDocument(const ProblemIR& problem) {
    std::ostringstream stream;
    stream << std::setprecision(17);
    stream << "vulkax 1\n";
    stream << "id " << std::quoted(problem.id) << '\n';
    stream << "name " << std::quoted(problem.name) << '\n';
    for (const auto& domain : problem.domains)
        stream << "domain " << std::quoted(domain.id) << ' ' << domainKindName(domain.kind) << ' '
               << static_cast<unsigned>(domain.spatialDimensions) << '\n';
    for (const auto& field : problem.fields) {
        stream << "field " << std::quoted(field.id) << ' ' << std::quoted(field.domainId) << ' '
               << fieldRankName(field.rank) << ' ' << field.components;
        writeDimension(stream, field.physicalDimension); stream << '\n';
    }
    for (const auto& op : problem.operators) {
        stream << "operator " << std::quoted(op.id) << ' ' << std::quoted(op.label) << ' '
               << std::quoted(op.outputFieldId) << ' ' << std::quoted(op.family) << ' '
               << std::quoted(op.expression);
        for (const auto& input : op.inputFieldIds) stream << ' ' << std::quoted(input);
        stream << '\n';
    }
    for (const auto& material : problem.materials) {
        stream << "material " << std::quoted(material.id) << '\n';
        for (const auto& property : material.properties) {
            stream << "property " << std::quoted(material.id) << ' ' << std::quoted(property.name) << ' '
                   << property.value.valueSI;
            writeDimension(stream, property.value.dimension); stream << '\n';
        }
    }
    for (const auto& boundary : problem.boundaryConditions) {
        stream << "bc " << std::quoted(boundary.id) << ' ' << std::quoted(boundary.domainId) << ' '
               << std::quoted(boundary.fieldId) << ' ' << std::quoted(boundary.kind);
        writeDimension(stream, boundary.physicalDimension);
        stream << ' ' << boundary.valuesSI.size();
        for (double value : boundary.valuesSI) stream << ' ' << value;
        stream << '\n';
    }
    for (const auto& objective : problem.objectives)
        stream << "objective " << std::quoted(objective.id) << ' ' << std::quoted(objective.label) << ' '
               << directionName(objective.direction) << ' ' << std::quoted(objective.expression) << '\n';
    for (const auto& accuracy : problem.accuracyTargets)
        stream << "accuracy " << std::quoted(accuracy.observableId) << ' ' << accuracy.relativeTolerance << ' '
               << (accuracy.absoluteTolerance ? 1 : 0) << ' ' << accuracy.absoluteTolerance.value_or(0.0) << '\n';
    if (problem.computeBudget.wallSeconds) stream << "budget_wall " << *problem.computeBudget.wallSeconds << '\n';
    if (problem.computeBudget.gpuMemoryBytes) stream << "budget_memory " << *problem.computeBudget.gpuMemoryBytes << '\n';
    return stream.str();
}

void saveProblemDocument(const ProblemIR& problem, const std::string& path) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("failed to create problem document: " + path);
    stream << writeProblemDocument(problem);
    if (!stream) throw std::runtime_error("failed to write problem document: " + path);
}

} // namespace vulkax::problem
