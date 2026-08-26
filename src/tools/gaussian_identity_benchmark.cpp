#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/gaussian/hierarchy.hpp"
#include "vulkax/gaussian/selection.hpp"
#include "vulkax/world/correspondence_graph.hpp"
#include "vulkax/world/world_ir.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] std::size_t parsePositiveSize(std::string_view text, const char* label) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        throw std::invalid_argument(std::string(label) + " must be a positive integer");
    const std::string owned(text);
    std::size_t consumed = 0;
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(owned, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " is outside the supported integer range");
    }
    if (consumed != owned.size() || parsed == 0U || parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
        throw std::invalid_argument(std::string(label) + " must be a positive supported integer");
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] double elapsedMilliseconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

[[nodiscard]] vulkax::gaussian::GaussianCloud makeCloud(std::size_t count) {
    using namespace vulkax;
    if (count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::invalid_argument("benchmark count exceeds Gaussian local-ID capacity");
    const auto side = static_cast<std::size_t>(std::ceil(std::cbrt(static_cast<double>(count))));
    gaussian::GaussianCloud cloud;
    cloud.splats.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t ix = index % side;
        const std::size_t iy = (index / side) % side;
        const std::size_t iz = index / (side * side);
        const double denominator = side > 1U ? static_cast<double>(side - 1U) : 1.0;
        gaussian::GaussianSplat splat;
        splat.id = {70U, static_cast<std::uint32_t>(index + 1U)};
        splat.position = {
            static_cast<double>(ix) / denominator,
            static_cast<double>(iy) / denominator,
            static_cast<double>(iz) / denominator,
        };
        cloud.splats.push_back(std::move(splat));
    }
    return cloud;
}

[[nodiscard]] std::vector<std::size_t> sampleCounts(
    std::size_t minimum, std::size_t maximum, std::size_t samples) {
    if (minimum > maximum) throw std::invalid_argument("minimum splat count must not exceed maximum splat count");
    if (samples == 1U) return {maximum};
    std::vector<std::size_t> counts;
    counts.reserve(samples);
    const double logMinimum = std::log(static_cast<double>(minimum));
    const double logMaximum = std::log(static_cast<double>(maximum));
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const double t = static_cast<double>(sample) / static_cast<double>(samples - 1U);
        std::size_t value = static_cast<std::size_t>(std::llround(std::exp(logMinimum + t * (logMaximum - logMinimum))));
        if (!counts.empty()) value = std::max(value, counts.back() + 1U);
        value = std::min(value, maximum);
        counts.push_back(value);
    }
    counts.front() = minimum;
    counts.back() = maximum;
    if (std::adjacent_find(counts.begin(), counts.end(), [](std::size_t a, std::size_t b) { return a >= b; }) != counts.end())
        throw std::invalid_argument("requested benchmark range cannot produce strictly increasing sample counts");
    return counts;
}

struct Row {
    std::size_t splats{};
    std::size_t selectionCount{};
    std::size_t queryCount{};
    std::size_t stableIdPayloadBytes{};
    double indexBuildMs{};
    double hierarchyBuildMs{};
    double hierarchyQueryMs{};
    double selectionResolveMs{};
    double correspondenceValidateMs{};
    bool reorderIdentityOk{};
    bool selectionReorderOk{};
    bool correspondenceReorderOk{};
    bool hierarchyReorderOk{};
};

[[nodiscard]] Row benchmark(std::size_t count, std::size_t selectionStride) {
    using namespace vulkax;
    Row row;
    row.splats = count;
    row.stableIdPayloadBytes = count * sizeof(gaussian::GaussianId);
    auto cloud = makeCloud(count);

    auto begin = Clock::now();
    const gaussian::GaussianIndexView initialView(cloud);
    auto end = Clock::now();
    row.indexBuildMs = elapsedMilliseconds(begin, end);
    if (initialView.size() != count) throw std::runtime_error("identity index did not contain every splat");

    begin = Clock::now();
    const auto hierarchy = gaussian::buildGaussianHierarchy(cloud, 64U);
    end = Clock::now();
    row.hierarchyBuildMs = elapsedMilliseconds(begin, end);

    const math::Vec3 queryMinimum{0.25, 0.25, 0.0};
    const math::Vec3 queryMaximum{0.75, 0.75, 1.0};
    begin = Clock::now();
    const auto queryBefore = gaussian::queryGaussianHierarchyAabbIds(
        hierarchy, cloud, queryMinimum, queryMaximum);
    end = Clock::now();
    row.hierarchyQueryMs = elapsedMilliseconds(begin, end);
    row.queryCount = queryBefore.size();
    if (row.queryCount == 0U) throw std::runtime_error("benchmark hierarchy query unexpectedly returned no splats");

    std::vector<gaussian::GaussianId> selected;
    for (std::size_t index = 0; index < count; index += selectionStride)
        selected.push_back(cloud.splats[index].id);
    row.selectionCount = selected.size();
    if (selected.empty()) throw std::runtime_error("benchmark selection unexpectedly contains no splats");

    gaussian::GaussianSelectionSet selections;
    selections.setGroup("benchmark-selection", selected);
    begin = Clock::now();
    const auto resolvedBefore = selections.resolveIndices("benchmark-selection", cloud);
    end = Clock::now();
    row.selectionResolveMs = elapsedMilliseconds(begin, end);
    if (resolvedBefore.size() != selected.size()) throw std::runtime_error("selection resolution lost members");

    world::WorldIR worldState;
    worldState.id = "gaussian-identity-benchmark";
    worldState.appearance = cloud;
    worldState.entities.push_back({1U, "benchmark-selection", std::nullopt, {}, {}});
    world::WorldCorrespondenceGraph graph;
    for (const auto id : selected) graph.bindGaussian(id, 1U);
    begin = Clock::now();
    const auto correspondenceBefore = graph.validate(worldState);
    end = Clock::now();
    row.correspondenceValidateMs = elapsedMilliseconds(begin, end);
    if (!correspondenceBefore.valid) throw std::runtime_error("benchmark correspondence failed before reorder");

    std::reverse(worldState.appearance.splats.begin(), worldState.appearance.splats.end());
    const gaussian::GaussianIndexView reorderedView(worldState.appearance);
    row.reorderIdentityOk = reorderedView.size() == count;
    for (const auto id : selected) row.reorderIdentityOk = row.reorderIdentityOk && reorderedView.contains(id);

    const auto resolvedAfter = selections.resolveIndices("benchmark-selection", worldState.appearance);
    row.selectionReorderOk = resolvedAfter.size() == selected.size();
    if (row.selectionReorderOk) {
        for (std::size_t member = 0; member < selected.size(); ++member)
            row.selectionReorderOk = row.selectionReorderOk &&
                worldState.appearance.splats[resolvedAfter[member]].id == selected[member];
    }

    row.correspondenceReorderOk = graph.validate(worldState).valid;

    const auto reorderedHierarchy = gaussian::buildGaussianHierarchy(worldState.appearance, 64U);
    const auto queryAfter = gaussian::queryGaussianHierarchyAabbIds(
        reorderedHierarchy, worldState.appearance, queryMinimum, queryMaximum);
    row.hierarchyReorderOk = queryAfter == queryBefore;

    if (!row.reorderIdentityOk || !row.selectionReorderOk ||
        !row.correspondenceReorderOk || !row.hierarchyReorderOk)
        throw std::runtime_error("stable identity invariant failed after storage reorder");
    return row;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 6) {
            std::cerr << "usage: vulkax_gaussian_identity_benchmark <output.csv> "
                         "[minimum-splats] [maximum-splats] [samples] [selection-stride]\n";
            return 2;
        }
        const std::string outputPath(argv[1]);
        const std::size_t minimum = argc >= 3 ? parsePositiveSize(argv[2], "minimum splats") : 4096U;
        const std::size_t maximum = argc >= 4 ? parsePositiveSize(argv[3], "maximum splats") : 262144U;
        const std::size_t samples = argc >= 5 ? parsePositiveSize(argv[4], "samples") : 4U;
        const std::size_t stride = argc >= 6 ? parsePositiveSize(argv[5], "selection stride") : 32U;
        if (samples < 2U) throw std::invalid_argument("benchmark requires at least two increasing samples");

        std::ofstream output(outputPath);
        if (!output) throw std::runtime_error("failed to open Gaussian identity benchmark output");
        output << "splats,selection_count,query_count,stable_id_payload_bytes,index_build_ms,hierarchy_build_ms,"
                  "hierarchy_query_ms,selection_resolve_ms,correspondence_validate_ms,reorder_identity_ok,"
                  "selection_reorder_ok,correspondence_reorder_ok,hierarchy_reorder_ok\n";
        output << std::setprecision(17);

        for (const auto count : sampleCounts(minimum, maximum, samples)) {
            const auto row = benchmark(count, stride);
            output << row.splats << ',' << row.selectionCount << ',' << row.queryCount << ','
                   << row.stableIdPayloadBytes << ',' << row.indexBuildMs << ',' << row.hierarchyBuildMs << ','
                   << row.hierarchyQueryMs << ',' << row.selectionResolveMs << ',' << row.correspondenceValidateMs << ','
                   << (row.reorderIdentityOk ? 1 : 0) << ',' << (row.selectionReorderOk ? 1 : 0) << ','
                   << (row.correspondenceReorderOk ? 1 : 0) << ',' << (row.hierarchyReorderOk ? 1 : 0) << '\n';
            std::cout << "Gaussian identity benchmark: splats=" << row.splats
                      << " selected=" << row.selectionCount
                      << " query=" << row.queryCount
                      << " index_ms=" << row.indexBuildMs
                      << " hierarchy_ms=" << row.hierarchyBuildMs << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vulkax Gaussian identity benchmark error: " << error.what() << '\n';
        return 1;
    }
}
