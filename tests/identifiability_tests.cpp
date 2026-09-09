#include "vulkax/world/reality_loop.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace vulkax;

world::ParameterAddress global(std::string name) {
    return {world::ParameterSpace::Global, std::nullopt, std::move(name)};
}

world::ObservationRecord observation(
    std::string id,
    double value,
    world::ObservationSpace space = world::ObservationSpace::Dimensionless) {
    world::ObservationRecord result;
    result.id = std::move(id);
    result.observableId = result.id;
    result.values = {value};
    result.standardDeviation = {1.0};
    result.evidence = world::EvidenceClass::Synthetic;
    result.source = "identifiability-regression";
    result.role = world::ObservationRole::Fit;
    result.space = space;
    return result;
}

} // namespace

int main() {
    using namespace vulkax::world;

    // A well-conditioned two-parameter model should expose full local rank.
    {
        WorldIR world;
        const auto a = global("a");
        const auto b = global("b");
        world.globalParameters = {{"a", 2.0}, {"b", -1.0}};
        world.parameterBeliefs.push_back({
            a, -10.0, 10.0, std::nullopt, EvidenceClass::ModelProxy, "test"});
        world.parameterBeliefs.push_back({
            b, -10.0, 10.0, std::nullopt, EvidenceClass::ModelProxy, "test"});
        world.observations.push_back(observation("sum", 1.0));
        world.observations.push_back(observation("difference", 3.0));

        const ForwardModel forward = [=](const WorldIR& candidate) {
            const double av = *candidate.parameterValue(a);
            const double bv = *candidate.parameterValue(b);
            return std::vector<ObservationPrediction>{
                {"sum", {av + bv}},
                {"difference", {av - bv}},
            };
        };
        const auto report = analyzeLocalIdentifiability(world, {a, b}, forward);
        assert(report.scalarObservationCount == 2U);
        assert(report.numericalRank == 2U);
        assert(report.locallyIdentifiable);
        assert(report.weakCombinations.empty());
        assert(report.singularValuesDescending.size() == 2U);
        assert(std::isfinite(report.conditionNumber));
        assert(std::abs(report.conditionNumber - 1.0) < 1.0e-8);
    }

    // Monocular free-fall without a metric scale anchor only observes the product
    // pixels_per_metre * gravity. Many physically different worlds therefore
    // produce exactly the same image trajectory. Optimizer convergence must not
    // be mistaken for identifiability.
    {
        WorldIR world;
        const auto scale = global("pixels_per_metre");
        const auto gravity = global("gravity_magnitude");
        world.globalParameters = {{"pixels_per_metre", 100.0}, {"gravity_magnitude", 9.8}};
        world.parameterBeliefs.push_back({
            scale, 50.0, 150.0, std::nullopt, EvidenceClass::ModelProxy, "uncalibrated-camera"});
        world.parameterBeliefs.push_back({
            gravity, 5.0, 15.0, std::nullopt, EvidenceClass::ModelProxy, "physics-prior"});
        world.observations.push_back(observation("pixel_t1", 980.0, ObservationSpace::ImagePixels));
        world.observations.push_back(observation("pixel_t2", 3920.0, ObservationSpace::ImagePixels));

        const ForwardModel unanchored = [=](const WorldIR& candidate) {
            const double s = *candidate.parameterValue(scale);
            const double g = *candidate.parameterValue(gravity);
            return std::vector<ObservationPrediction>{
                {"pixel_t1", {s * g}, ObservationSpace::ImagePixels},
                {"pixel_t2", {4.0 * s * g}, ObservationSpace::ImagePixels},
            };
        };
        const auto report = analyzeLocalIdentifiability(world, {scale, gravity}, unanchored);
        assert(report.scalarObservationCount == 2U);
        assert(report.numericalRank == 1U);
        assert(!report.locallyIdentifiable);
        assert(std::isinf(report.conditionNumber));
        assert(report.weakCombinations.size() == 1U);
        assert(report.weakCombinations[0].normalizedCoefficients.size() == 2U);
        assert(std::abs(report.weakCombinations[0].normalizedCoefficients[0]) > 0.05);
        assert(std::abs(report.weakCombinations[0].normalizedCoefficients[1]) > 0.05);

        // Add one independent calibration datum. The same image dynamics now have
        // enough local information to separate scale from metric acceleration.
        world.observations.push_back(observation("metric_scale_anchor", 100.0));
        const ForwardModel anchored = [=](const WorldIR& candidate) {
            const double s = *candidate.parameterValue(scale);
            const double g = *candidate.parameterValue(gravity);
            return std::vector<ObservationPrediction>{
                {"pixel_t1", {s * g}, ObservationSpace::ImagePixels},
                {"pixel_t2", {4.0 * s * g}, ObservationSpace::ImagePixels},
                {"metric_scale_anchor", {s}, ObservationSpace::Dimensionless},
            };
        };
        const auto anchoredReport = analyzeLocalIdentifiability(world, {scale, gravity}, anchored);
        assert(anchoredReport.scalarObservationCount == 3U);
        assert(anchoredReport.numericalRank == 2U);
        assert(anchoredReport.locallyIdentifiable);
        assert(anchoredReport.weakCombinations.empty());
        assert(std::isfinite(anchoredReport.conditionNumber));
    }

    return 0;
}
