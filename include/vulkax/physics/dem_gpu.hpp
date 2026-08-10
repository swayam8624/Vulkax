#pragma once

#include "vulkax/backend/backend.hpp"
#include "vulkax/physics/dem.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace vulkax::physics::dem {

struct GpuSettings {
    std::uint32_t maximumParticlesPerCell{64};
};

struct GpuResult {
    bool ok{false};
    backend::BackendKind backend{backend::BackendKind::Vulkan};
    std::string deviceName;
    std::string diagnostic;
    double wallMilliseconds{};
    std::size_t overflowingCells{};
    std::size_t particleContacts{};
    field::ParticleSet particles;
};

[[nodiscard]] GpuResult stepGpu(const field::ParticleSet& particles, const Settings& settings,
                                double dt, const GpuSettings& gpuSettings = {});

} // namespace vulkax::physics::dem
