#include "vulkax/gaussian/gaussian_cloud.hpp"
#include "vulkax/world/correspondence_graph.hpp"
#include "vulkax/world/transaction.hpp"
#include "vulkax/world/world_ir.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(double a, double b, double tolerance = 1.0e-6) {
    return std::abs(a - b) <= tolerance;
}

std::string plyHeader(const std::string& format, std::size_t count) {
    return "ply\nformat " + format + " 1.0\n"
           "element vertex " + std::to_string(count) + "\n"
           "property float x\nproperty float y\nproperty float z\n"
           "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n"
           "property float opacity\n"
           "property float scale_0\nproperty float scale_1\nproperty float scale_2\n"
           "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n"
           "end_header\n";
}

void appendFloatLE(std::string& bytes, float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    bytes.push_back(static_cast<char>(bits & 0xffu));
    bytes.push_back(static_cast<char>((bits >> 8u) & 0xffu));
    bytes.push_back(static_cast<char>((bits >> 16u) & 0xffu));
    bytes.push_back(static_cast<char>((bits >> 24u) & 0xffu));
}

void testAscii3dgsPly() {
    const std::string data = plyHeader("ascii", 2) +
        "1 2 3 0.1 0.2 0.3 0 0 0.69314718056 -0.69314718056 2 0 0 0\n"
        "-1 -2 -3 0.4 0.5 0.6 2 0.1 0.2 0.3 1 1 0 0\n";

    const auto cloud = vulkax::gaussian::parse3dgsPly(data);
    check(cloud.size() == 2, "ASCII 3DGS PLY must load every vertex");
    check(near(cloud.splats[0].position.x, 1.0) && near(cloud.splats[0].position.z, 3.0),
          "Gaussian positions must preserve PLY coordinates");
    const auto scale = cloud.splats[0].linearScale();
    check(near(scale[0], 1.0) && near(scale[1], 2.0, 1.0e-5) && near(scale[2], 0.5, 1.0e-5),
          "3DGS log scales must decode exponentially");
    check(near(cloud.splats[0].opacity(), 0.5), "opacity logits must decode with a stable sigmoid");
    check(near(cloud.splats[0].rotation[0], 1.0), "Gaussian rotations must be normalized");
    check(near(cloud.splats[1].rotation[0], std::sqrt(0.5), 1.0e-6) &&
              near(cloud.splats[1].rotation[1], std::sqrt(0.5), 1.0e-6),
          "non-unit Gaussian quaternions must be normalized on ingestion");
}

void testBinary3dgsPly() {
    std::string data = plyHeader("binary_little_endian", 1);
    const float values[] = {4.0F, 5.0F, 6.0F, 0.1F, 0.2F, 0.3F, 0.0F,
                            0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F};
    for (const float value : values) appendFloatLE(data, value);

    const auto cloud = vulkax::gaussian::parse3dgsPly(data);
    check(cloud.size() == 1, "binary 3DGS PLY must load its vertex payload");
    check(near(cloud.splats[0].position.y, 5.0), "binary little-endian scalar decoding must preserve values");
    check(near(cloud.splats[0].opacity(), 0.5), "binary opacity parameter must decode correctly");
}

void testCorrespondenceAndLocalRewrite() {
    using namespace vulkax;
    world::WorldIR world;
    world.id = "captured-object";
    world.entities.push_back({10, "rubber", std::nullopt, {{"young_modulus", 1.0e6}}});
    world.entities.push_back({20, "background", std::nullopt, {}});
    world.appearance.splats.resize(3);
    world.appearance.splats[0].position = {0.0, 0.0, 0.0};
    world.appearance.splats[1].position = {1.0, 0.0, 0.0};
    world.appearance.splats[2].position = {10.0, 0.0, 0.0};

    world::WorldCorrespondenceGraph graph;
    graph.bindGaussian(0, 10);
    graph.bindGaussian(1, 10);
    graph.bindGaussian(2, 20);
    graph.bindPhysical(10, {world::PhysicalKind::MpmParticle, 42, 0.65});
    graph.bindPhysical(10, {world::PhysicalKind::MpmParticle, 43, 0.35});

    const auto validation = graph.validate(world);
    check(validation.valid, "appearance/semantic/physics correspondence must validate");
    check(graph.gaussiansForEntity(10).size() == 2, "entity must expose its appearance support");
    check(graph.physicalBindings(10).size() == 2, "entity must expose its physical support");

    const auto before = world.appearance;
    const world::WorldTransaction transaction{
        "rewrite-0001",
        "test",
        "translate rubber object and change stiffness",
        {world::TranslateEntity{10, {0.0, 2.0, 0.0}}, world::SetMaterialParameter{10, "young_modulus", 2.0e6}}};
    const auto receipt = world::applyTransaction(world, graph, transaction);

    check(receipt.touchedGaussians.size() == 2, "transaction receipt must expose edited Gaussian support");
    check(near(world.appearance.splats[0].position.y, 2.0) && near(world.appearance.splats[1].position.y, 2.0),
          "entity translation must move only its mapped appearance");
    check(near(world.appearance.splats[2].position.x, 10.0) && near(world.appearance.splats[2].position.y, 0.0),
          "unrelated appearance must not move during a local rewrite");
    check(near(world::unaffectedPositionDrift(before, world.appearance, receipt.touchedGaussians), 0.0),
          "unaffected-region drift must be exactly zero for a local rigid rewrite");
    check(world.revision == 1 && world.provenance.size() == 1,
          "committed rewrite must advance revision and record provenance");
    check(near(world.findEntity(10)->materialParameters.at("young_modulus"), 2.0e6),
          "material rewrite must update semantic physical metadata");

    world::rollbackTransaction(world, receipt);
    check(world.revision == 0 && world.provenance.empty(), "rollback must restore revision/provenance state");
    check(near(world.appearance.splats[0].position.y, 0.0) && near(world.appearance.splats[1].position.y, 0.0),
          "rollback must restore edited Gaussian positions");
    check(near(world.findEntity(10)->materialParameters.at("young_modulus"), 1.0e6),
          "rollback must restore material metadata");
}

} // namespace

int main() {
    testAscii3dgsPly();
    testBinary3dgsPly();
    testCorrespondenceAndLocalRewrite();
    if (failures != 0) {
        std::cerr << failures << " world/Gaussian test(s) failed\n";
        return 1;
    }
    std::cout << "All captured-world Gaussian tests passed\n";
    return 0;
}
