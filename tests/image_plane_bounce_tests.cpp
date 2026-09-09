#include "vulkax/world/image_plane_bounce.hpp"
#include "vulkax/world/video_observation.hpp"

#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

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

    constexpr double x0 = 50.0;
    constexpr double y0 = 100.0;
    constexpr double vx = 30.0;
    constexpr double vy = -120.0;
    constexpr double acceleration = 500.0;
    constexpr double restitution = 0.70;
    constexpr double ground = 300.0;
    constexpr double fps = 30.0;
    constexpr std::size_t frameCount = 120;
    constexpr std::size_t releaseFrame = 15;
    constexpr double releaseTime = static_cast<double>(releaseFrame) / fps;

    BouncingPointDynamics truth;
    truth.initialPosition = {x0, ground - y0, 0.0};
    truth.initialVelocity = {vx, -vy, 0.0};
    truth.gravityY = -acceleration;
    truth.restitution = restitution;
    truth.groundY = 0.0;

    capture::VideoPointTrack track;
    track.source = "synthetic:image-plane-bounce-with-preroll";
    track.widthPixels = 640;
    track.heightPixels = 480;
    track.nominalFps = fps;
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const double time = static_cast<double>(frame) / fps;
        const auto state = simulateBouncingPointObservationTime(truth, time, releaseTime);
        capture::VideoPointTrackSample sample;
        sample.frameIndex = frame;
        sample.timeSeconds = time;
        sample.xPixels = state.position.x;
        sample.yPixels = ground - state.position.y;
        sample.confidence = 1.0;
        sample.split = (frame + 1U) % 5U == 0U
                           ? capture::VideoTrackSplit::Validation
                           : capture::VideoTrackSplit::Fit;
        track.samples.push_back(sample);
    }

    const auto seed = estimateImagePlaneBounceSeed(track);
    assert(seed.releaseSample.has_value());
    assert(seed.firstBounceSample.has_value());
    assert(*seed.releaseSample >= releaseFrame - 2U && *seed.releaseSample <= releaseFrame + 2U);
    assert(near(seed.releaseTimeSeconds, releaseTime, 0.08));
    assert(*seed.firstBounceSample > releaseFrame + 20U);
    assert(near(seed.velocityXPixelsPerSecond, vx, 1.0e-6));
    assert(near(seed.accelerationYPixelsPerSecond2, acceleration, 3.0));
    assert(near(seed.restitution, restitution, 0.06));
    assert(near(seed.groundYPixels, ground, 2.0));

    const ImagePlaneBounceParameterBinding binding{
        global("image.x0"), global("image.y0"), global("image.vx"), global("image.vy"),
        global("image.acceleration_y"), global("image.restitution"), global("image.ground_y"),
        global("image.release_time"),
    };

    WorldIR world;
    world.id = "image-plane-bounce-initializer";
    world.globalParameters = {
        {"image.x0", x0}, {"image.y0", y0}, {"image.vx", vx}, {"image.vy", vy},
        {"image.acceleration_y", 430.0}, {"image.restitution", 0.55}, {"image.ground_y", ground},
        {"image.release_time", releaseTime},
    };
    world.parameterBeliefs.push_back({
        binding.accelerationY, 100.0, 900.0, std::nullopt,
        EvidenceClass::ModelProxy, "image-plane-bootstrap"});
    world.parameterBeliefs.push_back({
        binding.restitution, 0.05, 0.99, std::nullopt,
        EvidenceClass::ModelProxy, "image-plane-bootstrap"});

    VideoObservationImportOptions import;
    import.baseStandardDeviationPixels = 1.0;
    import.evidence = EvidenceClass::Synthetic;
    appendVideoPointTrackObservations(world, track, import);
    for (auto& observation : world.observations) {
        if (observation.role == ObservationRole::Validation &&
            observation.timeSeconds > 2.0) {
            observation.values[0] += 5.0;
            break;
        }
    }

    const ImagePlaneBounceForwardModel forward(binding);
    RealityLoopSettings settings;
    settings.maxIterations = 20;
    settings.relativeImprovementTolerance = 1.0e-12;
    settings.parameterStepTolerance = 1.0e-12;
    const auto result = fitWorldHypothesis(
        world, {binding.accelerationY, binding.restitution}, forward, settings);

    assert(result.finalObjective < result.initialObjective * 1.0e-10);
    assert(result.residual.scalarCount == 192U);
    assert(result.residual.weightedRms < 1.0e-5);
    assert(near(*result.world.parameterValue(binding.accelerationY), acceleration, 1.0e-4));
    assert(near(*result.world.parameterValue(binding.restitution), restitution, 1.0e-4));
    assert(result.validationResidual.scalarCount == 48U);
    assert(result.validationResidual.weightedRms > 0.4);

    return 0;
}
