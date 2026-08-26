#pragma once

#include "vulkax/gaussian/gaussian_cloud.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vulkax::gaussian {

struct GaussianSelectionGroup {
    std::string id;
    std::vector<GaussianId> members;
};

class GaussianSelectionSet {
public:
    void setGroup(std::string id, std::vector<GaussianId> members);

    [[nodiscard]] const GaussianSelectionGroup* findGroup(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<GaussianSelectionGroup>& groups() const noexcept { return groups_; }
    [[nodiscard]] std::vector<std::size_t> resolveIndices(
        std::string_view id, const GaussianCloud& cloud) const;

    // Throws if a group ID is empty/duplicated, a member ID is invalid/duplicated,
    // or a member is absent from the supplied cloud.
    void validate(const GaussianCloud& cloud) const;

private:
    std::vector<GaussianSelectionGroup> groups_;
};

// Returns a filtered cloud in the original storage order. Every requested ID
// must exist exactly once in the source cloud; stable IDs are preserved verbatim.
[[nodiscard]] GaussianCloud filterGaussianCloudByIds(
    const GaussianCloud& cloud, const std::vector<GaussianId>& keepIds);

// Deterministic text format used for durable stable-ID selection groups.
// Group order is serialized lexicographically by group ID; members are stored in
// stable-ID order, independent of current Gaussian storage order.
[[nodiscard]] std::string serializeGaussianSelections(const GaussianSelectionSet& selections);
[[nodiscard]] GaussianSelectionSet parseGaussianSelections(std::string_view text);
void writeGaussianSelections(
    const GaussianSelectionSet& selections, const std::filesystem::path& path);
[[nodiscard]] GaussianSelectionSet loadGaussianSelections(const std::filesystem::path& path);

} // namespace vulkax::gaussian
