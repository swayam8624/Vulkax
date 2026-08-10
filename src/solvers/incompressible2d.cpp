#include "vulkax/solvers/incompressible2d.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::solvers {

FlowGrid2D::FlowGrid2D(std::size_t nx, std::size_t ny, double dx, double dy)
    : nx_(nx), ny_(ny), dx_(dx), dy_(dy), u_(nx * ny), v_(nx * ny),
      pressure_(nx * ny), solid_(nx * ny) {
    if (nx < 3 || ny < 3 || dx <= 0.0 || dy <= 0.0) {
        throw std::invalid_argument("flow grid requires at least 3x3 positive-spacing cells");
    }
}

std::size_t FlowGrid2D::index(std::size_t x, std::size_t y) const {
    if (x >= nx_ || y >= ny_) throw std::out_of_range("FlowGrid2D index out of range");
    return y * nx_ + x;
}

double& FlowGrid2D::u(std::size_t x, std::size_t y) { return u_[index(x, y)]; }
double& FlowGrid2D::v(std::size_t x, std::size_t y) { return v_[index(x, y)]; }
double& FlowGrid2D::pressure(std::size_t x, std::size_t y) { return pressure_[index(x, y)]; }
std::uint8_t& FlowGrid2D::solid(std::size_t x, std::size_t y) { return solid_[index(x, y)]; }
double FlowGrid2D::u(std::size_t x, std::size_t y) const { return u_[index(x, y)]; }
double FlowGrid2D::v(std::size_t x, std::size_t y) const { return v_[index(x, y)]; }
double FlowGrid2D::pressure(std::size_t x, std::size_t y) const { return pressure_[index(x, y)]; }
bool FlowGrid2D::solid(std::size_t x, std::size_t y) const { return solid_[index(x, y)] != 0u; }

namespace {

double divergenceAt(const FlowGrid2D& grid, std::size_t x, std::size_t y) {
    if (grid.solid(x, y)) return 0.0;
    const double du = (grid.u(x + 1, y) - grid.u(x - 1, y)) / (2.0 * grid.dx());
    const double dv = (grid.v(x, y + 1) - grid.v(x, y - 1)) / (2.0 * grid.dy());
    return du + dv;
}

void enforceBoundaries(FlowGrid2D& grid) {
    for (std::size_t x = 0; x < grid.nx(); ++x) {
        grid.u(x, 0) = 0.0; grid.v(x, 0) = 0.0;
        grid.u(x, grid.ny() - 1) = 0.0; grid.v(x, grid.ny() - 1) = 0.0;
    }
    for (std::size_t y = 0; y < grid.ny(); ++y) {
        grid.u(0, y) = 0.0; grid.v(0, y) = 0.0;
        grid.u(grid.nx() - 1, y) = 0.0; grid.v(grid.nx() - 1, y) = 0.0;
    }
    for (std::size_t y = 1; y + 1 < grid.ny(); ++y) {
        for (std::size_t x = 1; x + 1 < grid.nx(); ++x) {
            if (grid.solid(x, y)) {
                grid.u(x, y) = 0.0;
                grid.v(x, y) = 0.0;
            }
        }
    }
}

} // namespace

double divergenceL2(const FlowGrid2D& grid) {
    double squared = 0.0;
    std::size_t count = 0;
    for (std::size_t y = 1; y + 1 < grid.ny(); ++y) {
        for (std::size_t x = 1; x + 1 < grid.nx(); ++x) {
            if (grid.solid(x, y)) continue;
            const double divergence = divergenceAt(grid, x, y);
            squared += divergence * divergence;
            ++count;
        }
    }
    return count > 0 ? std::sqrt(squared / static_cast<double>(count)) : 0.0;
}

FlowDiagnostics projectIncompressible(FlowGrid2D& grid, const Incompressible2DConfig& config) {
    if (config.dt <= 0.0 || config.density <= 0.0 || config.pressureIterations == 0) {
        throw std::invalid_argument("invalid incompressible flow configuration");
    }
    enforceBoundaries(grid);
    FlowDiagnostics diagnostics;
    diagnostics.divergenceL2Before = divergenceL2(grid);

    const double dx2 = grid.dx() * grid.dx();
    const double dy2 = grid.dy() * grid.dy();
    const double denominator = 2.0 * (dx2 + dy2);
    std::vector<double> next(grid.nx() * grid.ny(), 0.0);
    for (std::size_t iteration = 0; iteration < config.pressureIterations; ++iteration) {
        for (std::size_t y = 1; y + 1 < grid.ny(); ++y) {
            for (std::size_t x = 1; x + 1 < grid.nx(); ++x) {
                const std::size_t index = y * grid.nx() + x;
                if (grid.solid(x, y)) {
                    next[index] = 0.0;
                    continue;
                }
                const double rhs = config.density / config.dt * divergenceAt(grid, x, y);
                const double left = grid.solid(x - 1, y) ? grid.pressure(x, y) : grid.pressure(x - 1, y);
                const double right = grid.solid(x + 1, y) ? grid.pressure(x, y) : grid.pressure(x + 1, y);
                const double bottom = grid.solid(x, y - 1) ? grid.pressure(x, y) : grid.pressure(x, y - 1);
                const double top = grid.solid(x, y + 1) ? grid.pressure(x, y) : grid.pressure(x, y + 1);
                next[index] = ((left + right) * dy2 + (bottom + top) * dx2 - rhs * dx2 * dy2) /
                              denominator;
            }
        }
        for (std::size_t y = 1; y + 1 < grid.ny(); ++y) {
            for (std::size_t x = 1; x + 1 < grid.nx(); ++x) {
                grid.pressure(x, y) = next[y * grid.nx() + x];
            }
        }
    }

    for (std::size_t y = 1; y + 1 < grid.ny(); ++y) {
        for (std::size_t x = 1; x + 1 < grid.nx(); ++x) {
            if (grid.solid(x, y)) continue;
            const double left = grid.solid(x - 1, y) ? grid.pressure(x, y) : grid.pressure(x - 1, y);
            const double right = grid.solid(x + 1, y) ? grid.pressure(x, y) : grid.pressure(x + 1, y);
            const double bottom = grid.solid(x, y - 1) ? grid.pressure(x, y) : grid.pressure(x, y - 1);
            const double top = grid.solid(x, y + 1) ? grid.pressure(x, y) : grid.pressure(x, y + 1);
            grid.u(x, y) -= config.dt / config.density * (right - left) / (2.0 * grid.dx());
            grid.v(x, y) -= config.dt / config.density * (top - bottom) / (2.0 * grid.dy());
        }
    }
    enforceBoundaries(grid);
    diagnostics.divergenceL2After = divergenceL2(grid);
    for (std::size_t y = 0; y < grid.ny(); ++y) {
        for (std::size_t x = 0; x < grid.nx(); ++x) {
            diagnostics.maxSpeed = std::max(diagnostics.maxSpeed,
                                            std::hypot(grid.u(x, y), grid.v(x, y)));
        }
    }
    return diagnostics;
}

} // namespace vulkax::solvers
