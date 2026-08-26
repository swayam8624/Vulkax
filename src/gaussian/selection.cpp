#include "vulkax/gaussian/selection.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace vulkax::gaussian {
namespace {

[[nodiscard]] bool idLess(GaussianId lhs, GaussianId rhs) noexcept {
    return lhs.packed() < rhs.packed();
}

void validateGroupDefinition(const std::string& id, const std::vector<GaussianId>& members) {
    if (id.empty()) throw std::invalid_argument("Gaussian selection group ID must not be empty");
    if (members.empty()) throw std::invalid_argument("Gaussian selection group must contain at least one stable ID");
    std::unordered_set<GaussianId, GaussianIdHash> seen;
    for (const auto member : members) {
        if (!member.valid()) throw std::invalid_argument("Gaussian selection contains an invalid stable ID");
        if (!seen.insert(member).second)
            throw std::invalid_argument("Gaussian selection contains duplicate stable ID " + toString(member));
    }
}

[[nodiscard]] GaussianId parseMemberId(std::istringstream& fields) {
    unsigned long long namespaceValue = 0;
    unsigned long long localValue = 0;
    if (!(fields >> namespaceValue >> localValue))
        throw std::runtime_error("Gaussian selection member requires namespace and local ID components");
    std::string trailing;
    if (fields >> trailing) throw std::runtime_error("Gaussian selection member has unexpected trailing fields");
    const auto maximum = static_cast<unsigned long long>(std::numeric_limits<std::uint32_t>::max());
    if (namespaceValue == 0 || localValue == 0 || namespaceValue > maximum || localValue > maximum)
        throw std::runtime_error("Gaussian selection member is outside the valid stable-ID range");
    return {
        static_cast<std::uint32_t>(namespaceValue),
        static_cast<std::uint32_t>(localValue),
    };
}

} // namespace

void GaussianSelectionSet::setGroup(std::string id, std::vector<GaussianId> members) {
    validateGroupDefinition(id, members);
    std::sort(members.begin(), members.end(), idLess);
    const auto existing = std::find_if(groups_.begin(), groups_.end(), [&](const GaussianSelectionGroup& group) {
        return group.id == id;
    });
    if (existing != groups_.end()) {
        existing->members = std::move(members);
        return;
    }
    groups_.push_back({std::move(id), std::move(members)});
}

const GaussianSelectionGroup* GaussianSelectionSet::findGroup(std::string_view id) const noexcept {
    const auto it = std::find_if(groups_.begin(), groups_.end(), [&](const GaussianSelectionGroup& group) {
        return group.id == id;
    });
    return it == groups_.end() ? nullptr : &*it;
}

std::vector<std::size_t> GaussianSelectionSet::resolveIndices(
    std::string_view id, const GaussianCloud& cloud) const {
    const auto* group = findGroup(id);
    if (group == nullptr) throw std::out_of_range("Gaussian selection group is absent: " + std::string(id));
    const GaussianIndexView view(cloud);
    std::vector<std::size_t> result;
    result.reserve(group->members.size());
    for (const auto member : group->members) result.push_back(view.requireIndex(member));
    return result;
}

void GaussianSelectionSet::validate(const GaussianCloud& cloud) const {
    const GaussianIndexView view(cloud);
    std::unordered_set<std::string> groupIds;
    for (const auto& group : groups_) {
        validateGroupDefinition(group.id, group.members);
        if (!groupIds.insert(group.id).second)
            throw std::invalid_argument("Gaussian selection contains duplicate group ID: " + group.id);
        for (const auto member : group.members) {
            if (!view.contains(member))
                throw std::invalid_argument("Gaussian selection group " + group.id +
                                            " references absent stable ID " + toString(member));
        }
    }
}

GaussianCloud filterGaussianCloudByIds(
    const GaussianCloud& cloud, const std::vector<GaussianId>& keepIds) {
    const GaussianIndexView view(cloud);
    std::unordered_set<GaussianId, GaussianIdHash> keep;
    keep.reserve(keepIds.size());
    for (const auto id : keepIds) {
        if (!id.valid()) throw std::invalid_argument("Gaussian filter contains an invalid stable ID");
        if (!view.contains(id))
            throw std::invalid_argument("Gaussian filter references absent stable ID " + toString(id));
        if (!keep.insert(id).second)
            throw std::invalid_argument("Gaussian filter contains duplicate stable ID " + toString(id));
    }

    GaussianCloud filtered;
    filtered.shRestCoefficientsPerSplat = cloud.shRestCoefficientsPerSplat;
    filtered.splats.reserve(keep.size());
    for (const auto& splat : cloud.splats) {
        if (keep.contains(splat.id)) filtered.splats.push_back(splat);
    }
    return filtered;
}

std::string serializeGaussianSelections(const GaussianSelectionSet& selections) {
    std::vector<const GaussianSelectionGroup*> ordered;
    ordered.reserve(selections.groups().size());
    for (const auto& group : selections.groups()) ordered.push_back(&group);
    std::sort(ordered.begin(), ordered.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->id < rhs->id;
    });

    std::ostringstream output;
    output << "vulkax_gaussian_selection 1\n";
    for (const auto* group : ordered) {
        validateGroupDefinition(group->id, group->members);
        output << "group " << std::quoted(group->id) << '\n';
        for (const auto member : group->members)
            output << "member " << member.namespaceId << ' ' << member.localId << '\n';
        output << "end\n";
    }
    return output.str();
}

GaussianSelectionSet parseGaussianSelections(std::string_view text) {
    std::istringstream input{std::string(text)};
    std::string line;
    if (!std::getline(input, line) || line != "vulkax_gaussian_selection 1")
        throw std::runtime_error("unsupported or missing Gaussian selection document header");

    GaussianSelectionSet selections;
    std::unordered_set<std::string> parsedGroups;
    std::string currentGroup;
    std::vector<GaussianId> members;
    std::size_t lineNumber = 1U;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        std::istringstream fields(line);
        std::string keyword;
        fields >> keyword;
        if (keyword == "group") {
            if (!currentGroup.empty())
                throw std::runtime_error("nested Gaussian selection group at line " + std::to_string(lineNumber));
            if (!(fields >> std::quoted(currentGroup)) || currentGroup.empty())
                throw std::runtime_error("invalid Gaussian selection group declaration at line " + std::to_string(lineNumber));
            std::string trailing;
            if (fields >> trailing)
                throw std::runtime_error("Gaussian selection group has trailing fields at line " + std::to_string(lineNumber));
            if (!parsedGroups.insert(currentGroup).second)
                throw std::runtime_error("duplicate Gaussian selection group: " + currentGroup);
            members.clear();
        } else if (keyword == "member") {
            if (currentGroup.empty())
                throw std::runtime_error("Gaussian selection member appears outside a group at line " + std::to_string(lineNumber));
            members.push_back(parseMemberId(fields));
        } else if (keyword == "end") {
            if (currentGroup.empty())
                throw std::runtime_error("Gaussian selection end appears outside a group at line " + std::to_string(lineNumber));
            std::string trailing;
            if (fields >> trailing)
                throw std::runtime_error("Gaussian selection end has trailing fields at line " + std::to_string(lineNumber));
            selections.setGroup(currentGroup, members);
            currentGroup.clear();
            members.clear();
        } else {
            throw std::runtime_error("unknown Gaussian selection keyword at line " + std::to_string(lineNumber) + ": " + keyword);
        }
    }
    if (!currentGroup.empty()) throw std::runtime_error("Gaussian selection document ends before group terminator");
    return selections;
}

void writeGaussianSelections(
    const GaussianSelectionSet& selections, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("failed to write Gaussian selections: " + path.string());
    output << serializeGaussianSelections(selections);
    if (!output) throw std::runtime_error("failed while writing Gaussian selections: " + path.string());
}

GaussianSelectionSet loadGaussianSelections(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("failed to open Gaussian selections: " + path.string());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseGaussianSelections(buffer.str());
}

} // namespace vulkax::gaussian
