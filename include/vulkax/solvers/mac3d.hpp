#pragma once

#include "vulkax/geometry/voxelize.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vulkax::solvers {

class MacGrid3D {
public:
    MacGrid3D(std::size_t nx,std::size_t ny,std::size_t nz,double dx,double dy,double dz);
    [[nodiscard]] std::size_t nx()const noexcept{return nx_;}
    [[nodiscard]] std::size_t ny()const noexcept{return ny_;}
    [[nodiscard]] std::size_t nz()const noexcept{return nz_;}
    [[nodiscard]] double dx()const noexcept{return dx_;}
    [[nodiscard]] double dy()const noexcept{return dy_;}
    [[nodiscard]] double dz()const noexcept{return dz_;}
    double& u(std::size_t xFace,std::size_t y,std::size_t z);
    double& v(std::size_t x,std::size_t yFace,std::size_t z);
    double& w(std::size_t x,std::size_t y,std::size_t zFace);
    double& pressure(std::size_t x,std::size_t y,std::size_t z);
    std::uint8_t& solid(std::size_t x,std::size_t y,std::size_t z);
    [[nodiscard]] double u(std::size_t xFace,std::size_t y,std::size_t z)const;
    [[nodiscard]] double v(std::size_t x,std::size_t yFace,std::size_t z)const;
    [[nodiscard]] double w(std::size_t x,std::size_t y,std::size_t zFace)const;
    [[nodiscard]] double pressure(std::size_t x,std::size_t y,std::size_t z)const;
    [[nodiscard]] bool solid(std::size_t x,std::size_t y,std::size_t z)const;
    void setSolidMask(const geometry::VoxelMask& mask);
private:
    [[nodiscard]] std::size_t cellIndex(std::size_t x,std::size_t y,std::size_t z)const;
    [[nodiscard]] std::size_t uIndex(std::size_t x,std::size_t y,std::size_t z)const;
    [[nodiscard]] std::size_t vIndex(std::size_t x,std::size_t y,std::size_t z)const;
    [[nodiscard]] std::size_t wIndex(std::size_t x,std::size_t y,std::size_t z)const;
    std::size_t nx_{},ny_{},nz_{};double dx_{},dy_{},dz_{};
    std::vector<double> u_,v_,w_,pressure_;std::vector<std::uint8_t> solid_;
};

struct MacProjectionConfig{double dt{0.001};double density{1.0};std::size_t pressureIterations{100};};
struct MacDiagnostics{double divergenceL2Before{};double divergenceL2After{};double maxSpeed{};};
[[nodiscard]] double divergenceL2(const MacGrid3D& grid);
[[nodiscard]] MacDiagnostics projectMacGrid(MacGrid3D& grid,const MacProjectionConfig& config);

} // namespace vulkax::solvers
