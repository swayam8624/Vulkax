#pragma once

#include "beacon/benchmark_config.hpp"
#include "lve_device.hpp"
#include "lve_renderer.hpp"
#include "lve_window.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace vulkax::gpu {

// Vulkax-owned presentation boundary for the direct Physics Studio Vulkan
// backend. The first implementation deliberately wraps the proven legacy
// swapchain substrate; product code owns this context rather than constructing
// LVE window/device/renderer objects itself. Those internals can now be
// replaced behind this boundary without rewriting simulation/presentation code.
class VulkanPresentationContext {
 public:
  VulkanPresentationContext(
      int width,
      int height,
      std::string title,
      const lve::BenchmarkConfig& benchmarkConfig);
  ~VulkanPresentationContext();

  VulkanPresentationContext(const VulkanPresentationContext&) = delete;
  VulkanPresentationContext& operator=(const VulkanPresentationContext&) = delete;

  [[nodiscard]] lve::LveWindow& window() noexcept;
  [[nodiscard]] lve::LveDevice& device() noexcept;
  [[nodiscard]] lve::LveRenderer& renderer() noexcept;
  [[nodiscard]] const lve::LveWindow& window() const noexcept;
  [[nodiscard]] const lve::LveDevice& device() const noexcept;
  [[nodiscard]] const lve::LveRenderer& renderer() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vulkax::gpu
