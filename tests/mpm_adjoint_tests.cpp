#include "vulkax/autodiff/mpm_adjoint.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

using namespace vulkax;

math::Vec3 applyAffine(const solvers::Matrix3& matrix, math::Vec3 translation, math::Vec3 point) {
    return {
        matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + translation.x,
        matrix[3] * point.x + matrix[4] * point.y + matrix[5] * point.z + translation.y,
        matrix[6] * point.x + matrix[7] * point.y + matrix[8] * point.z + translation.z,
    };
}

std::vector<solvers::MpmParticle> makeBody() {
    std::vector<solvers::MpmParticle> particles;
    constexpr double spacing = 0.13;
    constexpr double volume = spacing * spacing * spacing;
    constexpr double density = 1000.0;
    const solvers::Matrix3 deformation{
        1.05, 0.03, 0.00,
        0.01, 0.96, 0.02,
        0.00, 0.01, 1.02,
    };
    const math::Vec3 translation{0.027, -0.019, 0.013};
    std::uint64_t id = 1;
    for (int iz = 0; iz < 2; ++iz)
        for (int iy = 0; iy < 2; ++iy)
            for (int ix = 0; ix < 2; ++ix) {
                solvers::MpmParticle particle;
                particle.id = id++;
                particle.restPosition = {
                    (static_cast<double>(ix) - 0.5) * spacing,
                    (static_cast<double>(iy) - 0.5) * spacing,
                    (static_cast<double>(iz) - 0.5) * spacing,
                };
                particle.position = applyAffine(deformation, translation, particle.restPosition);
                particle.deformationGradient = deformation;
                particle.mass = density * volume;
                particle.restVolume = volume;
                particles.push_back(particle);
            }
    return particles;
}

solvers::MpmGridSettings makeGrid() {
    solvers::MpmGridSettings grid;
    grid.origin = {-0.91, -0.87, -0.93};
    grid.nx = 24;
    grid.ny = 24;
    grid.nz = 24;
    grid.cellSize = 0.08;
    grid.boundaryCells = 0;
    return grid;
}

double runObjective(
    std::vector<solvers::MpmParticle> particles,
    const solvers::MpmGridSettings& grid,
    const solvers::MpmMaterial& material,
    std::size_t steps,
    std::size_t objectiveParticle,
    math::Vec3 direction) {
    direction = direction / math::length(direction);
    const auto initial = particles[objectiveParticle].position;
    for (std::size_t step = 0; step < steps; ++step)
        (void)solvers::stepMpm(
            particles, grid, material, 1.0e-4, {}, solvers::MpmTransferScheme::APIC, 0.0);
    return math::dot(particles[objectiveParticle].position - initial, direction);
}

} // namespace

int main() {
    using namespace vulkax;

    const auto body = makeBody();
    const auto grid = makeGrid();
    const solvers::MpmMaterial material{1000.0, 1.5e4, 0.30};
    constexpr std::size_t steps = 6;
    constexpr std::size_t objectiveParticle = 7;
    const math::Vec3 direction{1.0, 0.7, -0.4};

    const auto adjoint = autodiff::differentiateMpmApicMaterialScales(
        body, grid, material, 1.0e-4, steps, objectiveParticle, direction);
    assert(adjoint.particleScaleGradient.size() == body.size());
    assert(adjoint.minimumStencilKnotMargin > 1.0e-4);
    assert(std::isfinite(adjoint.objective));

    const double directObjective = runObjective(body, grid, material, steps, objectiveParticle, direction);
    assert(std::abs(adjoint.objective - directObjective) < 1.0e-13);

    constexpr double h = 1.0e-4;
    double maximumAbsGradient = 0.0;
    for (std::size_t particle = 0; particle < body.size(); ++particle) {
        auto plus = body;
        auto minus = body;
        plus[particle].youngModulusScale += h;
        minus[particle].youngModulusScale -= h;
        const double finiteDifference = (
            runObjective(plus, grid, material, steps, objectiveParticle, direction) -
            runObjective(minus, grid, material, steps, objectiveParticle, direction)) / (2.0 * h);
        const double analytic = adjoint.particleScaleGradient[particle];
        assert(std::isfinite(analytic));
        const double scale = std::max({1.0e-10, std::abs(finiteDifference), std::abs(analytic)});
        assert(std::abs(analytic - finiteDifference) / scale < 2.0e-4);
        maximumAbsGradient = std::max(maximumAbsGradient, std::abs(analytic));
    }
    assert(maximumAbsGradient > 1.0e-8);
    return 0;
}
