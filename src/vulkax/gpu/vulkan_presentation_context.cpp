#include "vulkax/gpu/vulkan_presentation_context.hpp"

#include <utility>

namespace vulkax::gpu {

struct VulkanPresentationContext::Impl {
  Impl(
      int width,
      int height,
      std::string title,
      const lve::BenchmarkConfig& benchmarkConfig)
      : window{width, height, std::move(title)},
        device{window, benchmarkConfig},
        renderer{window, device} {}

  lve::LveWindow window;
  lve::LveDevice device;
  lve::LveRenderer renderer;
};

VulkanPresentationContext::VulkanPresentationContext(
    int width,
    int height,
    std::string title,
    const lve::BenchmarkConfig& benchmarkConfig)
    : impl_{std::make_unique<Impl>(width, height, std::move(title), benchmarkConfig)} {}

VulkanPresentationContext::~VulkanPresentationContext() = default;

lve::LveWindow& VulkanPresentationContext::window() noexcept { return impl_->window; }
lve::LveDevice& VulkanPresentationContext::device() noexcept { return impl_->device; }
lve::LveRenderer& VulkanPresentationContext::renderer() noexcept { return impl_->renderer; }
const lve::LveWindow& VulkanPresentationContext::window() const noexcept { return impl_->window; }
const lve::LveDevice& VulkanPresentationContext::device() const noexcept { return impl_->device; }
const lve::LveRenderer& VulkanPresentationContext::renderer() const noexcept { return impl_->renderer; }

}  // namespace vulkax::gpu
