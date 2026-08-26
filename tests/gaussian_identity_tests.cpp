#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/gaussian/hierarchy.hpp"
#include "vulkax/gaussian/selection.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(double lhs, double rhs, double tolerance = 1.0e-12) {
    return std::abs(lhs - rhs) <= tolerance;
}

vulkax::gaussian::GaussianCloud makeCloud() {
    using namespace vulkax;
    gaussian::GaussianCloud cloud;
    cloud.shRestCoefficientsPerSplat = 2U;
    for (std::size_t index = 0; index < 5U; ++index) {
        gaussian::GaussianSplat splat;
        splat.id = {50U, static_cast<std::uint32_t>(101U + index)};
        splat.position = {static_cast<double>(index), 0.25 * static_cast<double>(index), -0.1 * static_cast<double>(index)};
        splat.logScale = {0.01 * static_cast<double>(index), -0.02, 0.03};
        splat.rotation = {1.0, 0.0, 0.0, 0.0};
        splat.opacityLogit = 1.0 + 0.1 * static_cast<double>(index);
        splat.shDC = {0.1 * static_cast<double>(index), 0.2, 0.3};
        splat.shRest = {0.01 * static_cast<double>(index + 1U), -0.02 * static_cast<double>(index + 1U)};
        cloud.splats.push_back(std::move(splat));
    }
    return cloud;
}

void testLegacyFallbackIds() {
    using namespace vulkax;
    const std::string ply =
        "ply\nformat ascii 1.0\nelement vertex 2\n"
        "property float x\nproperty float y\nproperty float z\nend_header\n"
        "0 0 0\n1 2 3\n";
    const auto cloud = gaussian::parse3dgsPly(ply);
    check(cloud.size() == 2U, "legacy PLY must still parse");
    check(cloud.splats[0].id == gaussian::GaussianId{1U, 1U} &&
          cloud.splats[1].id == gaussian::GaussianId{1U, 2U},
          "legacy PLY must receive deterministic source-order fallback IDs");
}

void testExplicitIdentityRoundTrip() {
    using namespace vulkax;
    const auto source = makeCloud();
    const auto encoded = gaussian::serialize3dgsPly(source);
    check(encoded.find("property uint vulkax_id_namespace") != std::string::npos &&
          encoded.find("property uint vulkax_id_local") != std::string::npos,
          "Vulkax PLY must serialize explicit stable-ID properties");
    const auto decoded = gaussian::parse3dgsPly(encoded);
    check(decoded.size() == source.size(), "stable-ID PLY round-trip must preserve splat count");
    check(decoded.shRestCoefficientsPerSplat == source.shRestCoefficientsPerSplat,
          "stable-ID PLY round-trip must preserve SH-rest layout");
    for (std::size_t index = 0; index < source.size(); ++index) {
        check(decoded.splats[index].id == source.splats[index].id,
              "stable-ID PLY round-trip must preserve identity");
        check(near(decoded.splats[index].position.x, source.splats[index].position.x) &&
              near(decoded.splats[index].position.y, source.splats[index].position.y) &&
              near(decoded.splats[index].position.z, source.splats[index].position.z),
              "stable-ID PLY round-trip must preserve position");
        check(decoded.splats[index].shRest == source.splats[index].shRest,
              "stable-ID PLY round-trip must preserve represented SH-rest coefficients");
    }
}

void testMalformedIdentityRejected() {
    using namespace vulkax;
    const std::string partial =
        "ply\nformat ascii 1.0\nelement vertex 1\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property uint vulkax_id_namespace\nend_header\n"
        "0 0 0 9\n";
    bool partialRejected = false;
    try {
        (void)gaussian::parse3dgsPly(partial);
    } catch (const std::runtime_error&) {
        partialRejected = true;
    }
    check(partialRejected, "PLY with only half of a stable ID must be rejected");

    const std::string duplicate =
        "ply\nformat ascii 1.0\nelement vertex 2\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property uint vulkax_id_namespace\nproperty uint vulkax_id_local\nend_header\n"
        "0 0 0 9 7\n1 0 0 9 7\n";
    bool duplicateRejected = false;
    try {
        (void)gaussian::parse3dgsPly(duplicate);
    } catch (const std::invalid_argument&) {
        duplicateRejected = true;
    }
    check(duplicateRejected, "PLY with duplicate stable IDs must be rejected");
}

void testSelectionFilteringAndReorder() {
    using namespace vulkax;
    auto cloud = makeCloud();
    const std::vector<gaussian::GaussianId> wanted{
        cloud.splats[1].id,
        cloud.splats[3].id,
    };

    gaussian::GaussianSelectionSet selections;
    selections.setGroup("material-core", wanted);
    selections.validate(cloud);
    const auto serialized = gaussian::serializeGaussianSelections(selections);
    const auto parsed = gaussian::parseGaussianSelections(serialized);
    parsed.validate(cloud);

    std::reverse(cloud.splats.begin(), cloud.splats.end());
    parsed.validate(cloud);
    const auto indices = parsed.resolveIndices("material-core", cloud);
    check(indices.size() == 2U, "stable selection must resolve every member after reorder");
    check(cloud.splats[indices[0]].id == wanted[0] && cloud.splats[indices[1]].id == wanted[1],
          "stable selection resolution must target IDs rather than original indices");

    const auto filtered = gaussian::filterGaussianCloudByIds(cloud, wanted);
    check(filtered.size() == 2U, "ID filter must keep exactly the requested splats");
    check(filtered.splats[0].id == wanted[1] && filtered.splats[1].id == wanted[0],
          "ID filtering must preserve current source order while retaining stable IDs");
    parsed.validate(filtered);
}

void testHierarchyStableIdQueryAfterReorder() {
    using namespace vulkax;
    auto cloud = makeCloud();
    const math::Vec3 minimum{0.5, -1.0, -1.0};
    const math::Vec3 maximum{2.5, 1.0, 1.0};
    const auto firstHierarchy = gaussian::buildGaussianHierarchy(cloud, 2U);
    const auto firstIds = gaussian::queryGaussianHierarchyAabbIds(
        firstHierarchy, cloud, minimum, maximum);
    check(firstIds.size() == 2U &&
          firstIds[0] == gaussian::GaussianId{50U, 102U} &&
          firstIds[1] == gaussian::GaussianId{50U, 103U},
          "stable hierarchy query must return the expected IDs");

    std::reverse(cloud.splats.begin(), cloud.splats.end());
    const auto secondHierarchy = gaussian::buildGaussianHierarchy(cloud, 2U);
    const auto secondIds = gaussian::queryGaussianHierarchyAabbIds(
        secondHierarchy, cloud, minimum, maximum);
    check(secondIds == firstIds,
          "stable hierarchy AABB result must be invariant to storage reorder");
}

} // namespace

int main() {
    testLegacyFallbackIds();
    testExplicitIdentityRoundTrip();
    testMalformedIdentityRejected();
    testSelectionFilteringAndReorder();
    testHierarchyStableIdQueryAfterReorder();

    if (failures != 0) {
        std::cerr << failures << " Gaussian identity/selection test(s) failed\n";
        return 1;
    }
    std::cout << "All Gaussian identity/selection tests passed\n";
    return 0;
}
