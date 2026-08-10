#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace vulkax::units {

// SI base-dimension order: length, mass, time, electric current, temperature,
// amount of substance, luminous intensity.
struct Dimension {
    std::array<std::int8_t, 7> exponent{};

    constexpr bool operator==(const Dimension&) const = default;
};

constexpr Dimension makeDimension(int length, int mass, int time, int current = 0,
                                  int temperature = 0, int amount = 0, int luminous = 0) {
    return Dimension{{static_cast<std::int8_t>(length), static_cast<std::int8_t>(mass),
                      static_cast<std::int8_t>(time), static_cast<std::int8_t>(current),
                      static_cast<std::int8_t>(temperature), static_cast<std::int8_t>(amount),
                      static_cast<std::int8_t>(luminous)}};
}

constexpr Dimension multiply(Dimension lhs, Dimension rhs) {
    for (std::size_t i = 0; i < lhs.exponent.size(); ++i) {
        lhs.exponent[i] = static_cast<std::int8_t>(lhs.exponent[i] + rhs.exponent[i]);
    }
    return lhs;
}

constexpr Dimension divide(Dimension lhs, Dimension rhs) {
    for (std::size_t i = 0; i < lhs.exponent.size(); ++i) {
        lhs.exponent[i] = static_cast<std::int8_t>(lhs.exponent[i] - rhs.exponent[i]);
    }
    return lhs;
}

inline constexpr Dimension dimensionless = makeDimension(0, 0, 0);
inline constexpr Dimension length = makeDimension(1, 0, 0);
inline constexpr Dimension mass = makeDimension(0, 1, 0);
inline constexpr Dimension time = makeDimension(0, 0, 1);
inline constexpr Dimension temperature = makeDimension(0, 0, 0, 0, 1);
inline constexpr Dimension velocity = makeDimension(1, 0, -1);
inline constexpr Dimension acceleration = makeDimension(1, 0, -2);
inline constexpr Dimension force = makeDimension(1, 1, -2);
inline constexpr Dimension pressure = makeDimension(-1, 1, -2);

struct Unit {
    std::string_view symbol;
    double scaleToSI;
    Dimension dimension;
};

inline constexpr Unit one{"1", 1.0, dimensionless};
inline constexpr Unit metre{"m", 1.0, length};
inline constexpr Unit millimetre{"mm", 1.0e-3, length};
inline constexpr Unit kilogram{"kg", 1.0, mass};
inline constexpr Unit second{"s", 1.0, time};
inline constexpr Unit kelvin{"K", 1.0, temperature};
inline constexpr Unit metrePerSecond{"m/s", 1.0, velocity};
inline constexpr Unit kilometrePerHour{"km/h", 1000.0 / 3600.0, velocity};
inline constexpr Unit newton{"N", 1.0, force};
inline constexpr Unit pascal{"Pa", 1.0, pressure};

struct Quantity {
    double valueSI{};
    Dimension dimension{};

    static constexpr Quantity from(double value, Unit unit) {
        return Quantity{value * unit.scaleToSI, unit.dimension};
    }

    double in(Unit unit) const {
        if (!(dimension == unit.dimension)) {
            throw std::invalid_argument("incompatible physical dimensions");
        }
        return valueSI / unit.scaleToSI;
    }
};

} // namespace vulkax::units
