#pragma once

#include <cmath>

namespace vulkax::math {

struct Vec3 {
    double x{};
    double y{};
    double z{};

    constexpr Vec3& operator+=(Vec3 rhs) noexcept {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }

    constexpr Vec3& operator-=(Vec3 rhs) noexcept {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }

    constexpr Vec3& operator*=(double scalar) noexcept {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 lhs, Vec3 rhs) noexcept { return lhs += rhs; }
[[nodiscard]] constexpr Vec3 operator-(Vec3 lhs, Vec3 rhs) noexcept { return lhs -= rhs; }
[[nodiscard]] constexpr Vec3 operator-(Vec3 value) noexcept { return {-value.x, -value.y, -value.z}; }
[[nodiscard]] constexpr Vec3 operator*(Vec3 value, double scalar) noexcept { return value *= scalar; }
[[nodiscard]] constexpr Vec3 operator*(double scalar, Vec3 value) noexcept { return value *= scalar; }
[[nodiscard]] constexpr Vec3 operator/(Vec3 value, double scalar) noexcept {
    return {value.x / scalar, value.y / scalar, value.z / scalar};
}
[[nodiscard]] constexpr double dot(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}
[[nodiscard]] constexpr Vec3 cross(Vec3 lhs, Vec3 rhs) noexcept {
    return {lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}
[[nodiscard]] inline double length(Vec3 value) noexcept { return std::sqrt(dot(value, value)); }
[[nodiscard]] inline Vec3 normalized(Vec3 value) noexcept {
    const double magnitude = length(value);
    return magnitude > 0.0 ? value / magnitude : Vec3{};
}

} // namespace vulkax::math
