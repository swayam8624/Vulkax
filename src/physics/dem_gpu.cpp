#include "vulkax/physics/dem_gpu.hpp"

#ifndef VULKAX_HAS_VULKAN_DEM
#define VULKAX_HAS_VULKAN_DEM 0
#endif

namespace vulkax::physics::dem {
#if VULKAX_HAS_VULKAN_DEM
GpuResult stepVulkanDem(const field::ParticleSet&, const Settings&, double, const GpuSettings&);
#endif
GpuResult stepGpu(const field::ParticleSet& particles,const Settings& settings,double dt,const GpuSettings& gpuSettings){
#if VULKAX_HAS_VULKAN_DEM
    return stepVulkanDem(particles,settings,dt,gpuSettings);
#else
    (void)particles;(void)settings;(void)dt;(void)gpuSettings;
    GpuResult result;result.diagnostic="Vulkan GPU DEM was not built on this platform/toolchain";return result;
#endif
}
} // namespace vulkax::physics::dem
