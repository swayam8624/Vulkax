#include "vulkax/research/captured_verified_rewrite.hpp"
#include "vulkax/research/nonlinear_deformable_world.hpp"
#include "vulkax/world/correspondence_graph.hpp"
#include "vulkax/world/verified_rewrite.hpp"
#include "vulkax/world/world_ir.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace vulkax;

std::vector<solvers::MpmParticle> makeBody() {
    std::vector<solvers::MpmParticle> particles;
    std::uint64_t id = 1;
    constexpr double spacing = 0.12;
    constexpr double volume = spacing * spacing * spacing;
    constexpr double density = 1000.0;
    for (int iz = 0; iz < 4; ++iz)
        for (int iy = 0; iy < 4; ++iy)
            for (int ix = 0; ix < 4; ++ix) {
                solvers::MpmParticle particle;
                particle.id = id++;
                particle.restPosition = {
                    (static_cast<double>(ix) - 1.5) * spacing,
                    (static_cast<double>(iy) - 1.5) * spacing,
                    (static_cast<double>(iz) - 1.5) * spacing,
                };
                particle.position = particle.restPosition;
                particle.mass = density * volume;
                particle.restVolume = volume;
                particles.push_back(particle);
            }
    return particles;
}

gaussian::GaussianSplat makeSplat(math::Vec3 position) {
    gaussian::GaussianSplat result;
    result.position = position;
    result.logScale = {std::log(0.055), std::log(0.045), std::log(0.035)};
    result.rotation = {1.0, 0.0, 0.0, 0.0};
    result.opacityLogit = 4.0;
    return result;
}

gaussian::GaussianCloud makeWorld() {
    gaussian::GaussianCloud world;
    world.splats.push_back(makeSplat({-0.10, -0.05, -0.02}));
    world.splats.push_back(makeSplat({0.11, -0.04, 0.03}));
    world.splats.push_back(makeSplat({-0.03, 0.10, -0.06}));
    world.splats.push_back(makeSplat({0.04, 0.06, 0.09}));
    world.splats.push_back(makeSplat({0.00, 0.00, 0.00}));
    for (std::size_t index = 0; index < world.splats.size(); ++index)
        world.splats[index].id = {17U, static_cast<std::uint32_t>(index + 1U)};
    return world;
}

math::Vec3 applyAffine(const solvers::Matrix3& matrix, math::Vec3 translation, math::Vec3 point) {
    return {
        matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + translation.x,
        matrix[3] * point.x + matrix[4] * point.y + matrix[5] * point.z + translation.y,
        matrix[6] * point.x + matrix[7] * point.y + matrix[8] * point.z + translation.z,
    };
}

solvers::MpmGridSettings makeGrid() {
    solvers::MpmGridSettings grid;
    grid.origin = {-1.0, -1.0, -1.0};
    grid.nx = 26;
    grid.ny = 26;
    grid.nz = 26;
    grid.cellSize = 0.08;
    grid.boundaryCells = 0;
    return grid;
}

capture::CapturedDeformableDataset makeDataset(
    const std::vector<solvers::MpmParticle>& body,
    const solvers::Matrix3& deformation,
    math::Vec3 translation,
    const std::unordered_map<std::size_t, std::vector<math::Vec3>>& trajectories) {
    capture::CapturedDeformableDataset dataset;
    for (const auto& particle : body)
        dataset.particles.push_back({particle.id, particle.restPosition, particle.mass, particle.restVolume});

    const std::array<std::uint64_t, 5> markers{1, 4, 13, 49, 64};
    for (std::size_t marker = 0; marker < markers.size(); ++marker) {
        const auto& particle = body.at(static_cast<std::size_t>(markers[marker] - 1U));
        dataset.observations.push_back({
            "m" + std::to_string(marker), markers[marker], 0.0,
            applyAffine(deformation, translation, particle.restPosition),
            marker < 4U ? capture::ObservationSplit::Fit : capture::ObservationSplit::Validation,
        });
    }
    for (const auto step : {10U, 20U, 30U}) {
        const auto& positions = trajectories.at(step);
        for (std::size_t marker = 0; marker < markers.size(); ++marker) {
            dataset.observations.push_back({
                "m" + std::to_string(marker), markers[marker], static_cast<double>(step) * 1.0e-4,
                positions.at(static_cast<std::size_t>(markers[marker] - 1U)),
                marker < 3U ? capture::ObservationSplit::Fit : capture::ObservationSplit::Validation,
            });
        }
    }
    return dataset;
}

world::WorldIR makeTransactionWorld(const gaussian::GaussianCloud& capturedWorld) {
    world::WorldIR result;
    result.id = "captured-verified-material-world";
    result.appearance = capturedWorld;
    result.entities.push_back({100, "positive-octant-material-region", std::nullopt,
                               {{"young_modulus", 1.5e4}}, {}});
    return result;
}

world::WorldCorrespondenceGraph makeTransactionGraph(
    const capture::CapturedDeformableDataset& dataset) {
    world::WorldCorrespondenceGraph graph;
    for (const auto& particle : dataset.particles) {
        if (particle.restPosition.x >= 0.0 &&
            particle.restPosition.y >= 0.0 &&
            particle.restPosition.z >= 0.0)
            graph.bindPhysical(100, {world::PhysicalKind::MpmParticle, particle.particleId, 1.0});
    }
    return graph;
}

research::CapturedMaterialRewriteVerifierSettings makeVerifierSettings(
    const capture::CapturedDeformableDataset& dataset,
    const std::filesystem::path& evidenceDirectory) {
    research::CapturedMaterialRewriteVerifierSettings result;
    result.activeGaussianIndices = {0, 1, 2, 3, 4};
    result.dataset = dataset;
    result.grid = makeGrid();
    result.worldSettings.dt = 1.0e-4;
    result.worldSettings.material = {1000.0, 1.5e4, 0.30};
    result.worldSettings.couplingNeighborCount = 20;
    result.worldSettings.transferScheme = solvers::MpmTransferScheme::APIC;
    result.influenceSettings.objectiveMarkerId = "m4";
    result.influenceSettings.objectiveTime = 30.0e-4;
    result.influenceSettings.objectiveDirection = {1.0, 1.0, 1.0};
    result.influenceSettings.finiteDifferenceScaleStep = 0.01;
    result.influenceSettings.verificationScaleDelta = 0.02;
    result.evidenceDirectory = evidenceDirectory;
    result.maximumRelativeLinearizationError = 0.25;
    result.maximumAdjointAbsoluteError = 1.0e-8;
    result.maximumAdjointRelativeError = 5.0e-3;
    return result;
}

} // namespace

int main() {
    using namespace vulkax;

    const auto body = makeBody();
    const solvers::Matrix3 deformation{
        1.04, 0.04, 0.00,
        0.01, 0.97, 0.02,
        0.00, 0.01, 1.02,
    };
    const math::Vec3 translation{0.035, -0.021, 0.014};

    research::NonlinearDeformableWorldSettings truth;
    truth.steps = 30;
    truth.dt = 1.0e-4;
    truth.material = {1000.0, 1.5e4, 0.30};
    truth.initialDeformation = deformation;
    truth.initialTranslation = translation;
    truth.couplingNeighborCount = 20;
    truth.transferScheme = solvers::MpmTransferScheme::APIC;

    std::unordered_map<std::size_t, std::vector<math::Vec3>> trajectories;
    const auto observer = [&](const research::NonlinearDeformableWorldFrameEvidence& frame,
                              const gaussian::GaussianCloud&,
                              const std::vector<solvers::MpmParticle>& particles) {
        if (frame.step != 10U && frame.step != 20U && frame.step != 30U) return;
        auto& output = trajectories[frame.step];
        for (const auto& particle : particles) output.push_back(particle.position);
    };
    const std::vector<std::size_t> active{0, 1, 2, 3, 4};
    const auto restWorld = makeWorld();
    (void)research::runNonlinearDeformableWorld(
        restWorld, active, body, makeGrid(), truth, {}, observer);
    assert(trajectories.size() == 3U);

    auto capturedWorld = restWorld;
    for (auto& splat : capturedWorld.splats)
        splat.position = applyAffine(deformation, translation, splat.position);
    const auto dataset = makeDataset(body, deformation, translation, trajectories);
    const auto graph = makeTransactionGraph(dataset);
    assert(graph.physicalBindings(100).size() == 8U);

    const auto evidenceDirectory =
        std::filesystem::temp_directory_path() / "vulkax_captured_verified_rewrite_test";
    std::filesystem::remove_all(evidenceDirectory);

    auto transactionWorld = makeTransactionWorld(capturedWorld);
    const world::WorldTransaction verifiedTransaction{
        "captured-material-plus-two-percent",
        "controlled-test",
        "increase one captured MPM region Young's modulus by two percent",
        {world::SetMaterialParameter{100, "young_modulus", 1.53e4}},
        0U};
    const auto verifier = research::makeCapturedMaterialRewriteVerifier(
        makeVerifierSettings(dataset, evidenceDirectory));
    const auto verified = world::executeVerifiedRewrite(
        transactionWorld, graph, verifiedTransaction, {}, verifier);

    assert(verified.status == world::RewriteVerificationStatus::Verified);
    assert(verified.evidence.worldCommitted);
    assert(!verified.rollbackPerformed);
    assert(verified.evidence.physicalRerunCompleted && verified.evidence.physicalRerunPassed);
    assert(verified.evidence.independentOracleCompleted && verified.evidence.independentOraclePassed);
    assert(verified.evidence.physicalObservableError <= verified.evidence.physicalObservableTolerance);
    assert(std::abs(transactionWorld.findEntity(100)->materialParameters.at("young_modulus") - 1.53e4) < 1.0e-12);
    assert(transactionWorld.revision == 1U && transactionWorld.provenance.size() == 1U);
    assert(std::filesystem::is_regular_file(evidenceDirectory / "reference.csv"));
    assert(std::filesystem::is_regular_file(evidenceDirectory / "counterfactual.csv"));
    assert(std::filesystem::is_regular_file(evidenceDirectory / "adjoint.csv"));
    assert(std::filesystem::is_regular_file(evidenceDirectory / "derivative_comparison.csv"));

    // Evidence for a +2% nonlinear perturbation must not be reusable for an
    // unrelated +1% transaction. The adapter rejects the mismatch and the
    // envelope restores material/revision/provenance automatically.
    auto mismatchedWorld = makeTransactionWorld(capturedWorld);
    const world::WorldTransaction mismatchedTransaction{
        "captured-material-plus-one-percent",
        "controlled-test",
        "attempt to reuse two-percent evidence for a one-percent rewrite",
        {world::SetMaterialParameter{100, "young_modulus", 1.515e4}},
        0U};
    const auto mismatched = world::executeVerifiedRewrite(
        mismatchedWorld, graph, mismatchedTransaction, {}, verifier);
    assert(mismatched.status == world::RewriteVerificationStatus::Rejected);
    assert(mismatched.rollbackPerformed);
    assert(!mismatched.evidence.worldCommitted);
    assert(std::abs(mismatchedWorld.findEntity(100)->materialParameters.at("young_modulus") - 1.5e4) < 1.0e-12);
    assert(mismatchedWorld.revision == 0U && mismatchedWorld.provenance.empty());

    std::filesystem::remove_all(evidenceDirectory);
    return 0;
}