#include "vulkax/world/metric_video_bounce.hpp"
#include "vulkax/world/reality_loop.hpp"

#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace vulkax;

world::ParameterAddress global(std::string name) {
    return {world::ParameterSpace::Global, std::nullopt, std::move(name)};
}

bool near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main() {
    using namespace vulkax::world;

    const auto scale = global("camera.pixels_per_metre");
    const auto originX = global("camera.image_origin_x");
    const auto groundPixels = global("camera.ground_y_pixels");
    const auto initialX = global("ball.initial_x");
    const auto initialY = global("ball.initial_y");
    const auto initialZ = global("ball.initial_z");
    const auto velocityX = global("ball.velocity_x");
    const auto velocityY = global("ball.velocity_y");
    const auto velocityZ = global("ball.velocity_z");
    const auto gravityY = global("world.gravity_y");
    const auto restitution = global("ball.restitution");
    const auto groundY = global("world.ground_y");
    const auto releaseTime = global("ball.release_time");

    const BouncingPointParameterBinding dynamicsBinding{
        initialX, initialY, initialZ,
        velocityX, velocityY, velocityZ,
        gravityY, restitution, groundY,
        releaseTime,
    };
    const MetricImagePlaneBounceParameterBinding binding{
        scale, originX, groundPixels, dynamicsBinding};

    constexpr double truthScale = 200.0;
    constexpr double truthInitialHeight = 1.2;
    constexpr double truthGravity = -9.81;
    constexpr double truthOriginX = 220.0;
    constexpr double truthGroundPixels = 430.0;

    BouncingPointDynamics truthDynamics;
    truthDynamics.initialPosition = {0.0, truthInitialHeight, 0.0};
    truthDynamics.initialVelocity = {0.0, 0.0, 0.0};
    truthDynamics.gravityY = truthGravity;
    truthDynamics.restitution = 0.72;
    truthDynamics.groundY = 0.0;

    WorldIR world;
    world.id = "metric-video-bounce";
    world.globalParameters = {
        {"camera.pixels_per_metre", 155.0},
        {"camera.image_origin_x", truthOriginX},
        {"camera.ground_y_pixels", truthGroundPixels},
        {"ball.initial_x", 0.0},
        {"ball.initial_y", 0.95},
        {"ball.initial_z", 0.0},
        {"ball.velocity_x", 0.0},
        {"ball.velocity_y", 0.0},
        {"ball.velocity_z", 0.0},
        {"world.gravity_y", -7.5},
        {"ball.restitution", truthDynamics.restitution},
        {"world.ground_y", 0.0},
        {"ball.release_time", 0.0},
    };
    world.parameterBeliefs.push_back({
        scale, 80.0, 350.0, std::nullopt,
        EvidenceClass::ModelProxy, "uncalibrated-video"});
    world.parameterBeliefs.push_back({
        initialY, 0.4, 2.0, std::nullopt,
        EvidenceClass::ModelProxy, "monocular-height"});
    world.parameterBeliefs.push_back({
        gravityY, -15.0, -3.0, std::nullopt,
        EvidenceClass::ModelProxy, "gravity-prior"});

    // Free-flight samples before the first impact. With scale, metric initial height
    // and gravity all unknown, these image points constrain only scale*height and
    // scale*gravity. That intentionally creates the monocular gauge that the anchor
    // must remove.
    for (std::size_t frame = 0; frame <= 8U; ++frame) {
        const double time = 0.05 * static_cast<double>(frame);
        const auto state = simulateBouncingPoint(truthDynamics, time);
        ObservationRecord observation;
        observation.id = "video/frame-" + std::to_string(frame);
        observation.observableId = "image_point";
        observation.timeSeconds = time;
        observation.values = {
            truthOriginX + truthScale * state.position.x,
            truthGroundPixels - truthScale * state.position.y,
        };
        observation.standardDeviation = {0.25, 0.25};
        observation.evidence = EvidenceClass::Synthetic;
        observation.source = "metric-video-identifiability-regression";
        observation.role = ObservationRole::Fit;
        observation.space = ObservationSpace::ImagePixels;
        world.observations.push_back(std::move(observation));
    }

    const MetricImagePlaneBounceForwardModel unanchored(binding);
    const auto unanchoredReport = analyzeLocalIdentifiability(
        world, {scale, initialY, gravityY}, unanchored);
    assert(unanchoredReport.scalarObservationCount == 18U);
    assert(unanchoredReport.numericalRank == 2U);
    assert(!unanchoredReport.locallyIdentifiable);
    assert(unanchoredReport.weakCombinations.size() == 1U);

    MetricLengthAnchor anchor;
    anchor.observationId = "calibration/ruler-25cm";
    anchor.imageLengthPixels = 50.0;
    anchor.physicalLengthMeters = 0.25;
    anchor.imageStandardDeviationPixels = 0.25;
    anchor.physicalStandardDeviationMeters = 0.0005;
    anchor.evidence = EvidenceClass::Measured;
    anchor.source = "synthetic-calibrated-ruler";
    appendMetricLengthAnchorObservation(world, anchor);

    const MetricImagePlaneBounceForwardModel anchored(binding, {anchor});
    const auto anchoredReport = analyzeLocalIdentifiability(
        world, {scale, initialY, gravityY}, anchored);
    assert(anchoredReport.scalarObservationCount == 19U);
    assert(anchoredReport.numericalRank == 3U);
    assert(anchoredReport.locallyIdentifiable);
    assert(anchoredReport.weakCombinations.empty());
    assert(std::isfinite(anchoredReport.conditionNumber));

    RealityLoopSettings settings;
    settings.maxIterations = 30;
    settings.relativeImprovementTolerance = 1.0e-12;
    settings.parameterStepTolerance = 1.0e-12;
    const auto fit = fitWorldHypothesis(
        world, {scale, initialY, gravityY}, anchored, settings);
    assert(fit.finalObjective < fit.initialObjective * 1.0e-8);
    assert(fit.residual.weightedRms < 1.0e-3);
    assert(near(*fit.world.parameterValue(scale), truthScale, 1.0e-3));
    assert(near(*fit.world.parameterValue(initialY), truthInitialHeight, 1.0e-4));
    assert(near(*fit.world.parameterValue(gravityY), truthGravity, 1.0e-3));

    // Verify the explicit image-plane -> SI promotion path. The image acceleration
    // is a proxy until the independent ruler anchor is applied; afterwards the seed
    // is expressed in metres and metres/s^2 under the orthographic assumption.
    ImagePlaneBounceSeed imageSeed;
    imageSeed.initialXPixels = truthOriginX;
    imageSeed.initialYPixels = truthGroundPixels - truthScale * truthInitialHeight;
    imageSeed.velocityXPixelsPerSecond = 40.0;
    imageSeed.velocityYPixelsPerSecond = -30.0;
    imageSeed.accelerationYPixelsPerSecond2 = -truthGravity * truthScale;
    imageSeed.restitution = 0.68;
    imageSeed.groundYPixels = truthGroundPixels;
    imageSeed.releaseTimeSeconds = 0.4;

    const auto promoted = promoteImagePlaneBounceSeedToMetric(imageSeed, anchor);
    assert(near(promoted.pixelsPerMeter, truthScale, 1.0e-12));
    assert(near(promoted.dynamics.initialPosition.y, truthInitialHeight, 1.0e-12));
    assert(near(promoted.dynamics.initialVelocity.x, 0.2, 1.0e-12));
    assert(near(promoted.dynamics.initialVelocity.y, 0.15, 1.0e-12));
    assert(near(promoted.dynamics.gravityY, truthGravity, 1.0e-12));
    assert(near(promoted.releaseTimeSeconds, 0.4, 1.0e-12));

    return 0;
}
