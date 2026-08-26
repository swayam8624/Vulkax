#include "vulkax/capture/deformable_bundle.hpp"
#include "vulkax/research/captured_verified_rewrite.hpp"
#include "vulkax/world/correspondence_graph.hpp"
#include "vulkax/world/verified_rewrite.hpp"
#include "vulkax/world/world_ir.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

using namespace vulkax;

[[nodiscard]] double parseFiniteDouble(std::string_view text, const char* label) {
    const std::string owned(text);
    std::size_t consumed = 0;
    double value = 0.0;
    try {
        value = std::stod(owned, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(label) + " must be numeric");
    }
    if (consumed != owned.size() || !std::isfinite(value))
        throw std::invalid_argument(std::string(label) + " must be finite");
    return value;
}

[[nodiscard]] double parsePositiveDouble(std::string_view text, const char* label) {
    const double value = parseFiniteDouble(text, label);
    if (!(value > 0.0)) throw std::invalid_argument(std::string(label) + " must be positive");
    return value;
}

[[nodiscard]] std::vector<std::uint64_t> parseParticleIds(std::string_view text) {
    if (text.empty()) throw std::invalid_argument("particle ID region must not be empty");
    std::vector<std::uint64_t> result;
    std::unordered_set<std::uint64_t> seen;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto comma = text.find(',', begin);
        const auto end = comma == std::string_view::npos ? text.size() : comma;
        const auto token = text.substr(begin, end - begin);
        if (token.empty() || !std::all_of(token.begin(), token.end(), [](char character) {
                return character >= '0' && character <= '9';
            }))
            throw std::invalid_argument("particle IDs must be comma-separated positive integers");
        const std::string owned(token);
        std::size_t consumed = 0;
        unsigned long long parsed = 0;
        try {
            parsed = std::stoull(owned, &consumed);
        } catch (const std::exception&) {
            throw std::invalid_argument("particle ID is outside the supported integer range");
        }
        if (consumed != owned.size() || parsed == 0U)
            throw std::invalid_argument("particle IDs must be positive integers");
        const auto id = static_cast<std::uint64_t>(parsed);
        if (!seen.insert(id).second)
            throw std::invalid_argument("particle ID region contains a duplicate ID");
        result.push_back(id);
        if (comma == std::string_view::npos) break;
        begin = comma + 1U;
    }
    std::sort(result.begin(), result.end());
    return result;
}

[[nodiscard]] double characteristicParticleSpacing(
    const std::vector<capture::CapturedParticleSpec>& particles) {
    if (particles.size() < 2U) throw std::invalid_argument("captured body needs multiple particles");
    std::vector<double> nearest;
    nearest.reserve(particles.size());
    for (std::size_t i = 0; i < particles.size(); ++i) {
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < particles.size(); ++j) {
            if (i == j) continue;
            best = std::min(best, math::length(particles[i].restPosition - particles[j].restPosition));
        }
        if (std::isfinite(best) && best > 0.0) nearest.push_back(best);
    }
    if (nearest.empty()) throw std::invalid_argument("captured particle rest positions are coincident");
    std::sort(nearest.begin(), nearest.end());
    return nearest[nearest.size() / 2U];
}

[[nodiscard]] solvers::MpmGridSettings makeGrid(
    const capture::CapturedDeformableDataset& dataset,
    double cellSize) {
    math::Vec3 minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    math::Vec3 maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    const auto include = [&](math::Vec3 point) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    };
    for (const auto& particle : dataset.particles) include(particle.restPosition);
    for (const auto& observation : dataset.observations) include(observation.position);

    constexpr std::size_t paddingCells = 8U;
    const double padding = static_cast<double>(paddingCells) * cellSize;
    solvers::MpmGridSettings grid;
    grid.origin = {minimum.x - padding, minimum.y - padding, minimum.z - padding};
    const auto nodesFor = [&](double minValue, double maxValue) {
        const double extent = (maxValue - minValue) + 2.0 * padding;
        return std::max<std::size_t>(
            8U, static_cast<std::size_t>(std::ceil(extent / cellSize)) + 3U);
    };
    grid.nx = nodesFor(minimum.x, maximum.x);
    grid.ny = nodesFor(minimum.y, maximum.y);
    grid.nz = nodesFor(minimum.z, maximum.z);
    grid.cellSize = cellSize;
    grid.boundaryCells = 0U;
    return grid;
}

void validateRequestedIds(const capture::CapturedDeformableDataset& dataset,
                          const std::vector<std::uint64_t>& ids) {
    std::unordered_set<std::uint64_t> known;
    for (const auto& particle : dataset.particles) known.insert(particle.particleId);
    for (const auto id : ids) {
        if (!known.contains(id))
            throw std::invalid_argument("requested rewrite particle ID is not present in the capture bundle: " +
                                        std::to_string(id));
    }
}

void writeSummary(const std::filesystem::path& path,
                  const world::VerifiedRewriteResult& result,
                  double youngBefore,
                  double youngAfter,
                  const std::vector<std::uint64_t>& ids) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("failed to write verified rewrite summary: " + path.string());
    output << "status,revision_before,revision_after,particle_count,young_modulus_before,young_modulus_after,"
              "unaffected_position_drift,physical_observable_error,physical_observable_tolerance,rollback_performed\n";
    output << world::toString(result.status) << ','
           << result.receipt.revisionBefore << ','
           << result.receipt.revisionAfter << ','
           << ids.size() << ','
           << std::setprecision(17) << youngBefore << ',' << youngAfter << ','
           << result.evidence.unaffectedPositionDrift << ','
           << result.evidence.physicalObservableError << ','
           << result.evidence.physicalObservableTolerance << ','
           << (result.rollbackPerformed ? 1 : 0) << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 10) {
            std::cerr
                << "usage: vulkax_captured_rewrite <capture.vkcap> <output-dir> <marker-id> <time> "
                   "<dir-x> <dir-y> <dir-z> <particle-ids-csv> "
                   "[young-modulus] [poisson-ratio] [cell-size] [fd-scale-step] [rewrite-scale-delta]\n";
            return 2;
        }

        const auto bundle = capture::loadAndValidateCapturedDeformableBundle(argv[1]);
        capture::validateCapturedObservationTrajectoryContract(bundle.dataset);
        const std::filesystem::path outputDirectory(argv[2]);
        const std::string markerId(argv[3]);
        const double objectiveTime = parsePositiveDouble(argv[4], "objective time");
        const math::Vec3 direction{
            parseFiniteDouble(argv[5], "direction x"),
            parseFiniteDouble(argv[6], "direction y"),
            parseFiniteDouble(argv[7], "direction z"),
        };
        if (math::length(direction) <= 1.0e-15)
            throw std::invalid_argument("objective direction must be non-zero");
        const auto particleIds = parseParticleIds(argv[8]);
        validateRequestedIds(bundle.dataset, particleIds);

        const double young = argc >= 10 ? parsePositiveDouble(argv[9], "Young's modulus") : 1.5e4;
        const double poisson = argc >= 11 ? parseFiniteDouble(argv[10], "Poisson ratio") : 0.30;
        if (!(poisson > -1.0 && poisson < 0.5))
            throw std::invalid_argument("Poisson ratio must lie in (-1, 0.5)");
        const double cellSize = argc >= 12
            ? parsePositiveDouble(argv[11], "cell size")
            : characteristicParticleSpacing(bundle.dataset.particles) * (2.0 / 3.0);
        const double finiteDifferenceStep = argc >= 13
            ? parsePositiveDouble(argv[12], "finite-difference scale step") : 0.025;
        const double rewriteDelta = argc >= 14
            ? parseFiniteDouble(argv[13], "rewrite scale delta") : 0.05;
        if (std::abs(rewriteDelta) <= 1.0e-15 || !(1.0 + rewriteDelta > 0.0))
            throw std::invalid_argument("rewrite scale delta must be non-zero and keep Young's modulus positive");

        constexpr world::EntityId rewriteEntity = 1U;
        world::WorldIR worldState;
        worldState.id = bundle.manifest.id + "-verified-rewrite";
        worldState.appearance = bundle.appearance;
        worldState.entities.push_back({
            rewriteEntity,
            "captured-material-rewrite-region",
            std::nullopt,
            {{"young_modulus", young}},
            {},
        });

        world::WorldCorrespondenceGraph graph;
        for (const auto id : particleIds)
            graph.bindPhysical(rewriteEntity, {world::PhysicalKind::MpmParticle, id, 1.0});

        research::CapturedMaterialRewriteVerifierSettings verifierSettings;
        verifierSettings.activeGaussianIndices.resize(bundle.appearance.size());
        std::iota(verifierSettings.activeGaussianIndices.begin(),
                  verifierSettings.activeGaussianIndices.end(), 0U);
        verifierSettings.dataset = bundle.dataset;
        verifierSettings.grid = makeGrid(bundle.dataset, cellSize);
        verifierSettings.worldSettings.dt = bundle.manifest.timeStep;
        verifierSettings.worldSettings.material = {1000.0, young, poisson};
        verifierSettings.worldSettings.couplingNeighborCount =
            std::min<std::size_t>(20U, bundle.dataset.particles.size());
        verifierSettings.worldSettings.transferScheme = solvers::MpmTransferScheme::APIC;
        verifierSettings.worldSettings.flipBlend = 0.0;
        verifierSettings.influenceSettings.objectiveMarkerId = markerId;
        verifierSettings.influenceSettings.objectiveTime = objectiveTime;
        verifierSettings.influenceSettings.objectiveDirection = direction;
        verifierSettings.influenceSettings.finiteDifferenceScaleStep = finiteDifferenceStep;
        verifierSettings.influenceSettings.verificationScaleDelta = rewriteDelta;
        verifierSettings.evidenceDirectory = outputDirectory / "physical_evidence";

        const double rewrittenYoung = young * (1.0 + rewriteDelta);
        const world::WorldTransaction transaction{
            "captured-material-rewrite",
            "vulkax_captured_rewrite",
            "solver-backed local captured material rewrite",
            {world::SetMaterialParameter{rewriteEntity, "young_modulus", rewrittenYoung}},
            0U,
        };

        std::filesystem::create_directories(outputDirectory);
        const auto verifier = research::makeCapturedMaterialRewriteVerifier(std::move(verifierSettings));
        const auto result = world::executeVerifiedRewrite(worldState, graph, transaction, {}, verifier);

        {
            std::ofstream evidence(outputDirectory / "transaction_evidence.csv");
            if (!evidence) throw std::runtime_error("failed to write transaction evidence CSV");
            world::writeVerifiedRewriteEvidenceCsv(evidence, result);
        }
        writeSummary(outputDirectory / "transaction_summary.csv",
                     result, young, rewrittenYoung, particleIds);

        std::cout << std::setprecision(12)
                  << "Captured material verified rewrite\n"
                  << "  status: " << world::toString(result.status) << '\n'
                  << "  bundle: " << bundle.manifest.id << '\n'
                  << "  particle_count: " << particleIds.size() << '\n'
                  << "  young_modulus_before: " << young << '\n'
                  << "  young_modulus_requested: " << rewrittenYoung << '\n'
                  << "  rewrite_scale_delta: " << rewriteDelta << '\n'
                  << "  physical_error: " << result.evidence.physicalObservableError << '\n'
                  << "  physical_tolerance: " << result.evidence.physicalObservableTolerance << '\n'
                  << "  unaffected_position_drift: " << result.evidence.unaffectedPositionDrift << '\n'
                  << "  rollback_performed: " << (result.rollbackPerformed ? "yes" : "no") << '\n'
                  << "  output: " << outputDirectory.string() << '\n';
        if (result.status != world::RewriteVerificationStatus::Verified) {
            std::cerr << "rewrite rejected: " << result.evidence.rejectionReason << '\n';
            return 3;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vulkax captured rewrite error: " << error.what() << '\n';
        return 1;
    }
}
