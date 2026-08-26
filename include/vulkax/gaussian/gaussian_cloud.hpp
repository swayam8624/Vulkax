#pragma once

#include "vulkax/core/math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vulkax::gaussian {

struct GaussianId {
    std::uint32_t namespaceId{};
    std::uint32_t localId{};

    [[nodiscard]] bool valid() const noexcept { return namespaceId != 0U && localId != 0U; }
    [[nodiscard]] std::uint64_t packed() const noexcept {
        return (static_cast<std::uint64_t>(namespaceId) << 32U) |
               static_cast<std::uint64_t>(localId);
    }

    bool operator==(const GaussianId&) const noexcept = default;
};

struct GaussianIdHash {
    [[nodiscard]] std::size_t operator()(GaussianId id) const noexcept;
};

[[nodiscard]] std::string toString(GaussianId id);

struct GaussianSplat {
    math::Vec3 position{};
    std::array<double, 3> logScale{};
    std::array<double, 4> rotation{1.0, 0.0, 0.0, 0.0};
    double opacityLogit{};
    std::array<double, 3> shDC{};
    std::vector<double> shRest;
    GaussianId id{};

    [[nodiscard]] std::array<double, 3> linearScale() const noexcept;
    [[nodiscard]] double opacity() const noexcept;
};

struct GaussianCloud {
    std::vector<GaussianSplat> splats;
    std::size_t shRestCoefficientsPerSplat{};

    [[nodiscard]] bool empty() const noexcept { return splats.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return splats.size(); }
};

// A transient lookup view from stable Gaussian identity to the cloud's current
// storage index. Rebuild this view after reorder/filter operations; callers must
// not persist the returned indices as identity.
class GaussianIndexView {
public:
    explicit GaussianIndexView(const GaussianCloud& cloud);

    [[nodiscard]] std::optional<std::size_t> index(GaussianId id) const noexcept;
    [[nodiscard]] std::size_t requireIndex(GaussianId id) const;
    [[nodiscard]] bool contains(GaussianId id) const noexcept { return index(id).has_value(); }
    [[nodiscard]] std::size_t size() const noexcept { return idToIndex_.size(); }

private:
    std::unordered_map<GaussianId, std::size_t, GaussianIdHash> idToIndex_;
};

// Loads the scalar vertex layout emitted by the original 3D Gaussian Splatting
// implementation and compatible tools. Both ASCII and binary_little_endian PLY
// are accepted. Stored scale and opacity values remain in their optimization
// parameterization (log scale and logit); helpers expose physical values.
//
// External PLY files without Vulkax identity properties receive deterministic
// fallback IDs in namespace 1, local IDs 1..N, in source vertex order. Vulkax
// PLY serialization writes explicit `vulkax_id_namespace` and
// `vulkax_id_local` properties so IDs survive filtering and serialization.
[[nodiscard]] GaussianCloud load3dgsPly(const std::string& path);

// std::filesystem::path does not portably convert to std::string (notably on
// Windows, where its native character type differs). Keep the historical
// string overload unambiguous for string literals/argv while accepting an exact
// filesystem::path through this constrained forwarding overload.
template <typename Path>
requires std::is_same_v<std::remove_cvref_t<Path>, std::filesystem::path>
[[nodiscard]] GaussianCloud load3dgsPly(Path&& path) {
    return load3dgsPly(path.string());
}

[[nodiscard]] GaussianCloud parse3dgsPly(std::string_view bytes);

// Writes a deterministic ASCII PLY containing the numerical Gaussian fields
// represented by GaussianCloud plus explicit Vulkax stable-ID properties.
[[nodiscard]] std::string serialize3dgsPly(const GaussianCloud& cloud);
void write3dgsPly(const GaussianCloud& cloud, const std::filesystem::path& path);

} // namespace vulkax::gaussian
