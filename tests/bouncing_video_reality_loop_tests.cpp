#include "vulkax/world/bouncing_point_video.hpp"

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

bool near(double a, double b, double tolerance = 1.0e-4) {
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main() {
    using namespace vulkax::world;

    const PinholeCameraParameterBinding cameraBinding{
        global("camera.fx"), global("camera.fy"), global("camera.cx"), global("camera.cy"),
        global("camera.position_x"), global("camera.position_y"), global("camera.position_z"),
        global("camera.target_x"), global("camera.target_y"), global("camera.target_z"),
    };
    const BouncingPointParameterBinding dynamicsBinding{
        global("ball.initial_x"), global("ball.initial_y"), global("ball.initial_z"),
        global("ball.velocity_x"), global("ball.velocity_y"), global("ball.velocity_z"),
        global("world.gravity_y"), global("ball.restitution"), global("world.ground_y"),
    };

    PinholeCameraModel camera;
    camera.widthPixels = 640;
    camera.heightPixels = 480;
    camera.fxPixels = 600.0;
    camera.fyPixels = 620.0;
    camera.cxPixels = 320.0;
    camera.cyPixels = 240.0;
    camera.position = {0.0, 1.2, 4.0};
    camera.target = {0.0, 0.6, 0.0};

    BouncingPointDynamics truth;
    truth.initialPosition = {-0.6, 1.2, 0.0};
    truth.initialVelocity = {0.35, 1.1, 0.0};
    truth.gravityY = -9.81;
    truth.restitution = 0.72;
    truth.groundY = 0.0;

    WorldIR world;
    world.id = "video-to-physics-bounce";
    world.globalParameters = {
        {"camera.fx", camera.fxPixels}, {"camera.fy", camera.fyPixels},
        {"camera.cx", camera.cxPixels}, {"camera.cy", camera.cyPixels},
        {"camera.position_x", camera.position.x}, {"camera.position_y", camera.position.y},
        {"camera.position_z", camera.position.z}, {"camera.target_x", camera.target.x},
        {"camera.target_y", camera.target.y}, {"camera.target_z", camera.target.z},
        {"ball.initial_x", truth.initialPosition.x}, {"ball.initial_y", truth.initialPosition.y},
        {"ball.initial_z", truth.initialPosition.z}, {"ball.velocity_x", truth.initialVelocity.x},
        {"ball.velocity_y", truth.initialVelocity.y}, {"ball.velocity_z", truth.initialVelocity.z},
        {"world.gravity_y", -8.0}, {"ball.restitution", 0.55}, {"world.ground_y", truth.groundY},
    };
    world.parameterBeliefs.push_back({
        dynamicsBinding.gravityY, -15.0, -4.0, std::nullopt,
        EvidenceClass::ModelProxy, "synthetic-video-inverse"});
    world.parameterBeliefs.push_back({
        dynamicsBinding.restitution, 0.1, 0.99, std::nullopt,
        EvidenceClass::ModelProxy, "synthetic-video-inverse"});

    constexpr std::size_t frameCount = 50;
    constexpr double fps = 20.0;
    for (std::size_t frame = 1; frame <= frameCount; ++frame) {
        const double time = static_cast<double>(frame) / fps;
        const auto state = simulateBouncingPoint(truth, time);
        const auto pixel = projectWorldPoint(camera, state.position);
        assert(pixel.has_value());
        ObservationRecord observation;
        observation.id = "video/image_point/frame-" + std::to_string(frame);
        observation.observableId = "image_point";
        observation.timeSeconds = time;
        observation.values = {pixel->xPixels, pixel->yPixels};
        observation.standardDeviation = {1.0, 1.0};
        observation.evidence = EvidenceClass::Synthetic;
        observation.source = "synthetic-bouncing-point-video";
        observation.role = frame % 5U == 0U ? ObservationRole::Validation : ObservationRole::Fit;
        observation.space = ObservationSpace::ImagePixels;
        if (frame == 25U) observation.values[0] += 7.0;
        world.observations.push_back(std::move(observation));
    }

    assert(world.validateHypothesis().valid);
    const BouncingPointVideoForwardModel forward(
        640, 480, cameraBinding, dynamicsBinding, "image_point");
    RealityLoopSettings settings;
    settings.maxIterations = 20;
    settings.relativeImprovementTolerance = 1.0e-12;
    settings.parameterStepTolerance = 1.0e-12;
    const auto result = fitWorldHypothesis(
        world, {dynamicsBinding.gravityY, dynamicsBinding.restitution}, forward, settings);

    assert(result.finalObjective < result.initialObjective * 1.0e-10);
    assert(result.residual.scalarCount == 80U);
    assert(result.residual.weightedRms < 1.0e-5);
    assert(near(*result.world.parameterValue(dynamicsBinding.gravityY), truth.gravityY));
    assert(near(*result.world.parameterValue(dynamicsBinding.restitution), truth.restitution));
    assert(result.validationResidual.scalarCount == 20U);
    assert(result.validationResidual.weightedRms > 1.0);

    const auto firstImpact = simulateBouncingPoint(truth, 0.7);
    assert(firstImpact.impacts >= 1U);
    assert(firstImpact.position.y >= truth.groundY - 1.0e-10);

    return 0;
}
