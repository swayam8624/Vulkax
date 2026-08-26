#include "vulkax/world/correspondence_graph.hpp"
#include "vulkax/world/transaction.hpp"
#include "vulkax/world/verified_rewrite.hpp"
#include "vulkax/world/world_ir.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(double a, double b, double tolerance = 1.0e-12) {
    return std::abs(a - b) <= tolerance;
}

vulkax::world::WorldIR makeWorld() {
    using namespace vulkax;
    world::WorldIR world;
    world.id = "verified-rewrite-controlled";
    world.entities.push_back({10, "deformable", std::nullopt, {{"young_modulus", 1.0e6}}, {}});
    world.entities.push_back({20, "background", std::nullopt, {}, {}});
    world.appearance.splats.resize(3);
    world.appearance.splats[0].position = {0.0, 0.0, 0.0};
    world.appearance.splats[1].position = {1.0, 0.0, 0.0};
    world.appearance.splats[2].position = {10.0, 0.0, 0.0};
    return world;
}

vulkax::world::WorldCorrespondenceGraph makeGraph() {
    using namespace vulkax;
    world::WorldCorrespondenceGraph graph;
    graph.bindGaussian(0, 10);
    graph.bindGaussian(1, 10);
    graph.bindGaussian(2, 20);
    graph.bindPhysical(10, {world::PhysicalKind::MpmParticle, 42, 0.65});
    graph.bindPhysical(10, {world::PhysicalKind::MpmParticle, 43, 0.35});
    return graph;
}

vulkax::world::PhysicalRewriteEvidence passingPhysicalEvidence(bool withOracle) {
    vulkax::world::PhysicalRewriteEvidence evidence;
    evidence.rerunCompleted = true;
    evidence.rerunPassed = true;
    evidence.independentOracleCompleted = withOracle;
    evidence.independentOraclePassed = withOracle;
    evidence.observableError = 1.0e-8;
    evidence.observableTolerance = 1.0e-6;
    evidence.artifact = "controlled://verified-rewrite/rerun.csv";
    evidence.summary = "controlled deterministic rerun evidence";
    return evidence;
}

void testInvalidTransactionIsAtomic() {
    using namespace vulkax;
    auto world = makeWorld();
    const auto graph = makeGraph();
    const auto before = world;

    const world::WorldTransaction transaction{
        "atomic-invalid",
        "test",
        "valid geometry edit followed by invalid material target",
        {world::TranslateEntity{10, {0.0, 2.0, 0.0}},
         world::SetMaterialParameter{999, "young_modulus", 2.0e6}},
        0U};

    bool threw = false;
    try {
        (void)world::applyTransaction(world, graph, transaction);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "invalid transaction must throw before commit");
    check(world.revision == before.revision && world.provenance.empty(),
          "invalid transaction must not advance revision/provenance");
    check(near(world.appearance.splats[0].position.y, before.appearance.splats[0].position.y),
          "invalid transaction must not leave an earlier geometry edit applied");
    check(near(world.findEntity(10)->materialParameters.at("young_modulus"), 1.0e6),
          "invalid transaction must preserve material metadata");
}

void testStaleRevisionIsRejected() {
    using namespace vulkax;
    auto world = makeWorld();
    const auto graph = makeGraph();
    world.revision = 3;
    const world::WorldTransaction transaction{
        "stale-revision",
        "test",
        "stale rewrite",
        {world::TranslateEntity{10, {0.0, 1.0, 0.0}}},
        2U};
    const auto validation = world::validateTransaction(world, graph, transaction);
    check(!validation.valid, "stale expected revision must fail preconditions");
    const auto result = world::executeVerifiedRewrite(world, graph, transaction);
    check(result.status == world::RewriteVerificationStatus::Rejected,
          "stale verified rewrite must be rejected");
    check(!result.evidence.transactionApplied && world.revision == 3,
          "stale verified rewrite must not mutate world state");
}

void testConstraintSnapshotAndRollback() {
    using namespace vulkax;
    auto world = makeWorld();
    const auto graph = makeGraph();
    const world::WorldTransaction transaction{
        "constraint-low-level",
        "test",
        "set controlled fixed-y metadata",
        {world::SetConstraintParameter{10, "fixed_y", 1.0}},
        0U};
    const auto receipt = world::applyTransaction(world, graph, transaction);
    check(near(world.findEntity(10)->constraintParameters.at("fixed_y"), 1.0),
          "constraint edit must update entity constraint metadata");
    check(receipt.previousConstraints.size() == 1,
          "constraint edit must preserve rollback snapshot");
    world::rollbackTransaction(world, receipt);
    check(world.findEntity(10)->constraintParameters.empty(),
          "constraint rollback must restore previous metadata exactly");
    check(world.revision == 0 && world.provenance.empty(),
          "constraint rollback must restore revision/provenance");
}

void testMaterialRewriteRequiresVerifier() {
    using namespace vulkax;
    auto world = makeWorld();
    const auto graph = makeGraph();
    const world::WorldTransaction transaction{
        "material-no-verifier",
        "test",
        "attempt material rewrite without physical evidence",
        {world::SetMaterialParameter{10, "young_modulus", 2.0e6}},
        0U};
    const auto result = world::executeVerifiedRewrite(world, graph, transaction);
    check(result.status == world::RewriteVerificationStatus::Rejected,
          "material rewrite without verifier must be rejected");
    check(!result.evidence.transactionApplied && !result.rollbackPerformed,
          "missing verifier must reject before mutation");
    check(near(world.findEntity(10)->materialParameters.at("young_modulus"), 1.0e6),
          "rejected material rewrite must preserve original value");
}

void testVerifiedMaterialRewrite() {
    using namespace vulkax;
    auto world = makeWorld();
    const auto graph = makeGraph();
    const world::WorldTransaction transaction{
        "material-verified",
        "test",
        "verified local stiffness rewrite",
        {world::SetMaterialParameter{10, "young_modulus", 2.0e6}},
        0U};

    const auto result = world::executeVerifiedRewrite(
        world, graph, transaction, {},
        [](const world::WorldIR&, const world::WorldCorrespondenceGraph&,
           const world::WorldTransaction&, const world::TransactionReceipt&) {
            return passingPhysicalEvidence(true);
        });

    check(result.status == world::RewriteVerificationStatus::Verified,
          "material rewrite must verify when rerun and independent oracle pass");
    check(result.evidence.worldCommitted && !result.rollbackPerformed,
          "verified material rewrite must remain committed");
    check(result.evidence.physicalRerunRequired && result.evidence.independentOracleRequired,
          "material rewrite must derive both physical-rerun and independent-oracle requirements");
    check(near(world.findEntity(10)->materialParameters.at("young_modulus"), 2.0e6),
          "verified material rewrite must preserve committed value");
    check(world.revision == 1 && world.provenance.size() == 1,
          "verified material rewrite must record one revision/provenance entry");
}

void testFailedMaterialOracleRollsBack() {
    using namespace vulkax;
    auto world = makeWorld();
    const auto graph = makeGraph();
    const world::WorldTransaction transaction{
        "material-failed-oracle",
        "test",
        "material rewrite with failing independent oracle",
        {world::SetMaterialParameter{10, "young_modulus", 2.0e6}},
        0U};

    const auto result = world::executeVerifiedRewrite(
        world, graph, transaction, {},
        [](const world::WorldIR&, const world::WorldCorrespondenceGraph&,
           const world::WorldTransaction&, const world::TransactionReceipt&) {
            auto evidence = passingPhysicalEvidence(true);
            evidence.independentOraclePassed = false;
            return evidence;
        });

    check(result.status == world::RewriteVerificationStatus::Rejected,
          "failed independent material oracle must reject rewrite");
    check(result.rollbackPerformed && !result.evidence.worldCommitted,
          "failed material oracle must automatically rollback");
    check(near(world.findEntity(10)->materialParameters.at("young_modulus"), 1.0e6),
          "failed material oracle rollback must restore stiffness");
    check(world.revision == 0 && world.provenance.empty(),
          "failed material oracle rollback must restore revision/provenance");
}

void testVerifiedGeometryRewrite() {
    using namespace vulkax;
    auto world = makeWorld();
    const auto graph = makeGraph();
    const world::WorldTransaction transaction{
        "geometry-verified",
        "test",
        "verified local geometry translation",
        {world::TranslateEntity{10, {0.0, 2.0, 0.0}}},
        0U};

    const auto result = world::executeVerifiedRewrite(
        world, graph, transaction, {},
        [](const world::WorldIR&, const world::WorldCorrespondenceGraph&,
           const world::WorldTransaction&, const world::TransactionReceipt&) {
            return passingPhysicalEvidence(false);
        });

    check(result.status == world::RewriteVerificationStatus::Verified,
          "geometry rewrite with physical support must verify after rerun evidence");
    check(result.evidence.appearancePropagationRequired && result.evidence.appearancePropagationChecked,
          "geometry rewrite must derive and check appearance propagation");
    check(near(result.evidence.unaffectedPositionDrift, 0.0),
          "local geometry rewrite must report zero unaffected drift");
    check(near(world.appearance.splats[0].position.y, 2.0) &&
          near(world.appearance.splats[1].position.y, 2.0),
          "verified geometry rewrite must move mapped appearance");
    check(near(world.appearance.splats[2].position.x, 10.0) &&
          near(world.appearance.splats[2].position.y, 0.0),
          "verified geometry rewrite must preserve unrelated appearance");
}

void testVerifiedConstraintRewrite() {
    using namespace vulkax;
    auto world = makeWorld();
    const auto graph = makeGraph();
    const world::WorldTransaction transaction{
        "constraint-verified",
        "test",
        "verified fixed-y constraint metadata rewrite",
        {world::SetConstraintParameter{10, "fixed_y", 1.0}},
        0U};

    const auto result = world::executeVerifiedRewrite(
        world, graph, transaction, {},
        [](const world::WorldIR&, const world::WorldCorrespondenceGraph&,
           const world::WorldTransaction&, const world::TransactionReceipt&) {
            return passingPhysicalEvidence(false);
        });

    check(result.status == world::RewriteVerificationStatus::Verified,
          "constraint rewrite must verify after physical rerun evidence");
    check(result.evidence.physicalRerunRequired && !result.evidence.independentOracleRequired,
          "constraint rewrite must require rerun without inventing material-oracle requirements");
    check(near(world.findEntity(10)->constraintParameters.at("fixed_y"), 1.0),
          "verified constraint rewrite must remain committed");
}

void testEvidenceCsv() {
    using namespace vulkax;
    auto world = makeWorld();
    const auto graph = makeGraph();
    const world::WorldTransaction transaction{
        "csv-verified",
        "test",
        "write evidence csv",
        {world::SetMaterialParameter{10, "young_modulus", 2.0e6}},
        0U};
    const auto result = world::executeVerifiedRewrite(
        world, graph, transaction, {},
        [](const world::WorldIR&, const world::WorldCorrespondenceGraph&,
           const world::WorldTransaction&, const world::TransactionReceipt&) {
            return passingPhysicalEvidence(true);
        });
    std::ostringstream stream;
    world::writeVerifiedRewriteEvidenceCsv(stream, result);
    const auto csv = stream.str();
    check(csv.find("status,preconditions_satisfied") == 0,
          "rewrite evidence CSV must expose a stable named schema");
    check(csv.find("verified,1,1,1,0") != std::string::npos,
          "rewrite evidence CSV must record derived verified/commit state");
    check(csv.find("controlled://verified-rewrite/rerun.csv") != std::string::npos,
          "rewrite evidence CSV must retain physical evidence artifact identity");
}

} // namespace

int main() {
    testInvalidTransactionIsAtomic();
    testStaleRevisionIsRejected();
    testConstraintSnapshotAndRollback();
    testMaterialRewriteRequiresVerifier();
    testVerifiedMaterialRewrite();
    testFailedMaterialOracleRollsBack();
    testVerifiedGeometryRewrite();
    testVerifiedConstraintRewrite();
    testEvidenceCsv();

    if (failures != 0) {
        std::cerr << failures << " verified rewrite test(s) failed\n";
        return 1;
    }
    std::cout << "All verified rewrite transaction tests passed\n";
    return 0;
}
