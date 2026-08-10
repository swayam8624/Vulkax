#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vulkax::solvers {

class FlowGrid2D {
public:
    FlowGrid2D(std::size_t nx, std::size_t ny, double dx, double dy);
    [[nodiscard]] std::size_t nx() const noexcept { return nx_; }
    [[nodiscard]] std::size_t ny() const noexcept { return ny_; }
    [[nodiscard]] double dx() const noexcept { return dx_; }
    [[nodiscard]] double dy() const noexcept { return dy_; }
    double& u(std::size_t x, std::size_t y);
    double& v(std::size_t x, std::size_t y);
    double& pressure(std::size_t x, std::size_t y);
    std::uint8_t& solid(std::size_t x, std::size_t y);
    [[nodiscard]] double u(std::size_t x, std::size_t y) const;
    [[nodiscard]] double v(std::size_t x, std::size_t y) const;
    [[nodiscard]] double pressure(std::size_t x, std::size_t y) const;
    [[nodiscard]] bool solid(std::size_t x, std::size_t y) const;

private:
    [[nodiscard]] std::size_t index(std::size_t x, std::size_t y) const;
    std::size_t nx_{};
    std::size_t ny_{};
    double dx_{1.0};
    double dy_{1.0};
    std::vector<double> u_;
    std::vector<double> v_;
    std::vector<double> pressure_;
    std::vector<std::uint8_t> solid_;
};

struct Incompressible2DConfig {
    double dt{0.001};
    double density{1.0};
    double viscosity{0.0};
    std::size_t pressureIterations{80};
};

struct FlowDiagnostics {
    double divergenceL2Before{};
    double divergenceL2After{};
    double maxSpeed{};
};

[[nodiscard]] double divergenceL2(const FlowGrid2D& grid);
[[nodiscard]] FlowDiagnostics projectIncompressible(FlowGrid2D& grid,
                                                    const Incompressible2DConfig& config);

} // namespace vulkax::solvers
