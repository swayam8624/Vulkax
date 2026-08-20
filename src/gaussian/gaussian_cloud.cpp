#include "vulkax/gaussian/gaussian_cloud.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace vulkax::gaussian {
namespace {

enum class PlyFormat { Ascii, BinaryLittleEndian };
enum class ScalarType { Int8, UInt8, Int16, UInt16, Int32, UInt32, Float32, Float64 };

struct Property {
    ScalarType type{};
    std::string name;
};

struct Header {
    PlyFormat format{};
    std::size_t vertexCount{};
    std::vector<Property> properties;
    std::size_t dataOffset{};
};

[[nodiscard]] ScalarType parseType(const std::string& name) {
    if (name == "char" || name == "int8") return ScalarType::Int8;
    if (name == "uchar" || name == "uint8") return ScalarType::UInt8;
    if (name == "short" || name == "int16") return ScalarType::Int16;
    if (name == "ushort" || name == "uint16") return ScalarType::UInt16;
    if (name == "int" || name == "int32") return ScalarType::Int32;
    if (name == "uint" || name == "uint32") return ScalarType::UInt32;
    if (name == "float" || name == "float32") return ScalarType::Float32;
    if (name == "double" || name == "float64") return ScalarType::Float64;
    throw std::runtime_error("unsupported PLY scalar type: " + name);
}

[[nodiscard]] std::size_t typeSize(ScalarType type) {
    switch (type) {
        case ScalarType::Int8:
        case ScalarType::UInt8: return 1;
        case ScalarType::Int16:
        case ScalarType::UInt16: return 2;
        case ScalarType::Int32:
        case ScalarType::UInt32:
        case ScalarType::Float32: return 4;
        case ScalarType::Float64: return 8;
    }
    return 0;
}

[[nodiscard]] Header parseHeader(std::string_view bytes) {
    constexpr std::string_view endToken = "end_header";
    const auto end = bytes.find(endToken);
    if (end == std::string_view::npos) throw std::runtime_error("PLY header is missing end_header");
    auto dataOffset = end + endToken.size();
    if (dataOffset < bytes.size() && bytes[dataOffset] == '\r') ++dataOffset;
    if (dataOffset < bytes.size() && bytes[dataOffset] == '\n') ++dataOffset;

    std::istringstream input(std::string(bytes.substr(0, end)));
    std::string line;
    Header header;
    bool sawPly = false;
    bool inVertex = false;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "ply") {
            sawPly = true;
            continue;
        }
        std::istringstream fields(line);
        std::string keyword;
        fields >> keyword;
        if (keyword == "format") {
            std::string format;
            std::string version;
            fields >> format >> version;
            if (version != "1.0") throw std::runtime_error("unsupported PLY version: " + version);
            if (format == "ascii") header.format = PlyFormat::Ascii;
            else if (format == "binary_little_endian") header.format = PlyFormat::BinaryLittleEndian;
            else throw std::runtime_error("only ASCII and binary_little_endian PLY are supported");
        } else if (keyword == "element") {
            std::string element;
            std::size_t count = 0;
            fields >> element >> count;
            inVertex = element == "vertex";
            if (inVertex) header.vertexCount = count;
        } else if (keyword == "property" && inVertex) {
            std::string typeName;
            fields >> typeName;
            if (typeName == "list") throw std::runtime_error("list-valued vertex properties are unsupported");
            std::string propertyName;
            fields >> propertyName;
            if (propertyName.empty()) throw std::runtime_error("malformed PLY property declaration");
            header.properties.push_back({parseType(typeName), propertyName});
        }
    }

    if (!sawPly) throw std::runtime_error("not a PLY file");
    if (header.vertexCount == 0) throw std::runtime_error("PLY contains no vertices");
    if (header.properties.empty()) throw std::runtime_error("PLY vertex element has no scalar properties");
    header.dataOffset = dataOffset;
    return header;
}

template <typename T>
[[nodiscard]] T readLittleEndian(const char* source) {
    std::array<std::byte, sizeof(T)> storage{};
    std::memcpy(storage.data(), source, sizeof(T));
    if constexpr (std::endian::native == std::endian::big) std::reverse(storage.begin(), storage.end());
    T value{};
    std::memcpy(&value, storage.data(), sizeof(T));
    return value;
}

[[nodiscard]] double readScalar(ScalarType type, const char* source) {
    switch (type) {
        case ScalarType::Int8: return static_cast<double>(readLittleEndian<std::int8_t>(source));
        case ScalarType::UInt8: return static_cast<double>(readLittleEndian<std::uint8_t>(source));
        case ScalarType::Int16: return static_cast<double>(readLittleEndian<std::int16_t>(source));
        case ScalarType::UInt16: return static_cast<double>(readLittleEndian<std::uint16_t>(source));
        case ScalarType::Int32: return static_cast<double>(readLittleEndian<std::int32_t>(source));
        case ScalarType::UInt32: return static_cast<double>(readLittleEndian<std::uint32_t>(source));
        case ScalarType::Float32: return static_cast<double>(readLittleEndian<float>(source));
        case ScalarType::Float64: return readLittleEndian<double>(source);
    }
    return 0.0;
}

[[nodiscard]] std::size_t numberedSuffix(std::string_view name, std::string_view prefix) {
    if (!name.starts_with(prefix)) return std::numeric_limits<std::size_t>::max();
    const std::string suffix(name.substr(prefix.size()));
    if (suffix.empty()) return std::numeric_limits<std::size_t>::max();
    std::size_t consumed = 0;
    const auto value = std::stoull(suffix, &consumed);
    return consumed == suffix.size() ? static_cast<std::size_t>(value) : std::numeric_limits<std::size_t>::max();
}

void normalizeQuaternion(std::array<double, 4>& rotation) {
    double norm2 = 0.0;
    for (const double value : rotation) norm2 += value * value;
    if (norm2 <= 1.0e-30) {
        rotation = {1.0, 0.0, 0.0, 0.0};
        return;
    }
    const double inv = 1.0 / std::sqrt(norm2);
    for (double& value : rotation) value *= inv;
}

[[nodiscard]] GaussianSplat buildSplat(const std::vector<Property>& properties,
                                        const std::vector<double>& values,
                                        std::size_t restCount) {
    GaussianSplat splat;
    splat.shRest.assign(restCount, 0.0);
    bool haveX = false;
    bool haveY = false;
    bool haveZ = false;

    for (std::size_t index = 0; index < properties.size(); ++index) {
        const auto& name = properties[index].name;
        const double value = values[index];
        if (name == "x") { splat.position.x = value; haveX = true; }
        else if (name == "y") { splat.position.y = value; haveY = true; }
        else if (name == "z") { splat.position.z = value; haveZ = true; }
        else if (name == "opacity") splat.opacityLogit = value;
        else if (name == "scale_0") splat.logScale[0] = value;
        else if (name == "scale_1") splat.logScale[1] = value;
        else if (name == "scale_2") splat.logScale[2] = value;
        else if (name == "rot_0") splat.rotation[0] = value;
        else if (name == "rot_1") splat.rotation[1] = value;
        else if (name == "rot_2") splat.rotation[2] = value;
        else if (name == "rot_3") splat.rotation[3] = value;
        else if (name == "f_dc_0") splat.shDC[0] = value;
        else if (name == "f_dc_1") splat.shDC[1] = value;
        else if (name == "f_dc_2") splat.shDC[2] = value;
        else {
            const auto rest = numberedSuffix(name, "f_rest_");
            if (rest < splat.shRest.size()) splat.shRest[rest] = value;
        }
    }

    if (!haveX || !haveY || !haveZ) throw std::runtime_error("3DGS PLY must contain x, y and z properties");
    normalizeQuaternion(splat.rotation);
    return splat;
}

[[nodiscard]] std::size_t restCoefficientCount(const std::vector<Property>& properties) {
    std::size_t count = 0;
    for (const auto& property : properties) {
        const auto suffix = numberedSuffix(property.name, "f_rest_");
        if (suffix != std::numeric_limits<std::size_t>::max()) count = std::max(count, suffix + 1);
    }
    return count;
}

} // namespace

std::array<double, 3> GaussianSplat::linearScale() const noexcept {
    return {std::exp(logScale[0]), std::exp(logScale[1]), std::exp(logScale[2])};
}

double GaussianSplat::opacity() const noexcept {
    if (opacityLogit >= 0.0) {
        const double z = std::exp(-opacityLogit);
        return 1.0 / (1.0 + z);
    }
    const double z = std::exp(opacityLogit);
    return z / (1.0 + z);
}

GaussianCloud load3dgsPly(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("failed to open 3DGS PLY: " + path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parse3dgsPly(buffer.str());
}

GaussianCloud parse3dgsPly(std::string_view bytes) {
    const Header header = parseHeader(bytes);
    const std::size_t restCount = restCoefficientCount(header.properties);
    GaussianCloud cloud;
    cloud.shRestCoefficientsPerSplat = restCount;
    cloud.splats.reserve(header.vertexCount);

    if (header.format == PlyFormat::Ascii) {
        std::istringstream input(std::string(bytes.substr(header.dataOffset)));
        std::vector<double> values(header.properties.size(), 0.0);
        for (std::size_t vertex = 0; vertex < header.vertexCount; ++vertex) {
            for (double& value : values) {
                if (!(input >> value)) throw std::runtime_error("unexpected end of ASCII PLY vertex data");
            }
            cloud.splats.push_back(buildSplat(header.properties, values, restCount));
        }
        return cloud;
    }

    std::size_t stride = 0;
    for (const auto& property : header.properties) stride += typeSize(property.type);
    if (stride == 0 || header.vertexCount > (bytes.size() - header.dataOffset) / stride)
        throw std::runtime_error("binary PLY vertex payload is truncated");

    std::vector<double> values(header.properties.size(), 0.0);
    std::size_t cursor = header.dataOffset;
    for (std::size_t vertex = 0; vertex < header.vertexCount; ++vertex) {
        for (std::size_t property = 0; property < header.properties.size(); ++property) {
            const auto type = header.properties[property].type;
            values[property] = readScalar(type, bytes.data() + cursor);
            cursor += typeSize(type);
        }
        cloud.splats.push_back(buildSplat(header.properties, values, restCount));
    }
    return cloud;
}

} // namespace vulkax::gaussian
