#include "vulkax/world/pinhole_camera.hpp"

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

bool near(double a, double b, double tolerance = 1.0e-5) {
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main() {
    using namespace vulkax::world;

    const PinholeCameraParameterBinding binding{
        global("camera.fx"), global("camera.fy"), global("camera.cx"), global("camera.cy"),
        global("camera.position_x"), global("camera.position_y"), global("camera.position_z"),
        global("camera.target_x"), global("camera.target_y"), global("camera.target_z"),
    };

    PinholeCameraModel truth;
    truth.widthPixels = 640;
    truth.heightPixels = 480;
    truth.fxPixels = 600.0;
    truth.fyPixels = 620.0;
    truth.cxPixels = 320.0;
    truth.cyPixels = 240.0;
    truth.position = {0.0, 0.0, 3.0};
    truth.target = {0.0, 0.0, 0.0};

    const std::vector<WorldPointProjectionSample> samples{
        {"p0", {-0.50,  0.20, 0.00}},
        {"p1", { 0.40, -0.30, 0.10}},
        {"p2", {-0.15, -0.45, 0.35}},
        {"p3", { 0.55,  0.35, 0.20}},
        {"p4", { 0.10,  0.50, 0.40}},
    };

    WorldIR world;
    world.id = "camera-calibration";
    world.globalParameters = {
        {"camera.fx", 450.0}, {"camera.fy", 470.0},
        {"camera.cx", 290.0}, {"camera.cy", 265.0},
        {"camera.position_x", 0.0}, {"camera.position_y", 0.0}, {"camera.position_z", 3.0},
        {"camera.target_x", 0.0}, {"camera.target_y", 0.0}, {"camera.target_z", 0.0},
    };
    world.parameterBeliefs.push_back({binding.fx, 100.0, 1200.0, std::nullopt, EvidenceClass::ModelProxy, "calibration"});
    world.parameterBeliefs.push_back({binding.fy, 100.0, 1200.0, std::nullopt, EvidenceClass::ModelProxy, "calibration"});
    world.parameterBeliefs.push_back({binding.cx, 0.0, 640.0, std::nullopt, EvidenceClass::ModelProxy, "calibration"});
    world.parameterBeliefs.push_back({binding.cy, 0.0, 480.0, std::nullopt, EvidenceClass::ModelProxy, "calibration"});

    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto pixel = projectWorldPoint(truth, samples[index].worldPosition);
        assert(pixel.has_value());
        ObservationRecord observation;
        observation.id = samples[index].observationId;
        observation.observableId = "calibration_point";
        observation.values = {pixel->xPixels, pixel->yPixels};
        observation.standardDeviation = {1.0, 1.0};
        observation.evidence = EvidenceClass::Synthetic;
        observation.source = "synthetic-pinhole-truth";
        observation.role = index + 1 == samples.size() ? ObservationRole::Validation : ObservationRole::Fit;
        observation.space = ObservationSpace::ImagePixels;
        if (observation.role == ObservationRole::Validation) observation.values[0] += 9.0;
        world.observations.push_back(std::move(observation));
    }

    assert(world.validateHypothesis().valid);
    const PinholeProjectionForwardModel forward(640, 480, binding, samples);
    RealityLoopSettings settings;
    settings.maxIterations = 20;
    settings.relativeImprovementTolerance = 1.0e-12;
    settings.parameterStepTolerance = 1.0e-12;
    const auto result = fitWorldHypothesis(
        world, {binding.fx, binding.fy, binding.cx, binding.cy}, forward, settings);

    assert(result.finalObjective < result.initialObjective * 1.0e-10);
    assert(result.residual.weightedRms < 1.0e-5);
    assert(near(*result.world.parameterValue(binding.fx), truth.fxPixels));
    assert(near(*result.world.parameterValue(binding.fy), truth.fyPixels));
    assert(near(*result.world.parameterValue(binding.cx), truth.cxPixels));
    assert(near(*result.world.parameterValue(binding.cy), truth.cyPixels));
    assert(result.validationResidual.scalarCount == 2U);
    assert(result.validationResidual.weightedRms > 5.0);

    const auto behind = projectWorldPoint(truth, {0.0, 0.0, 4.0});
    assert(!behind.has_value());

    return 0;
}
