from pathlib import Path

header = Path('src/lve_renderer.hpp')
text = header.read_text()
if '#include <cstdint>' not in text:
    text = text.replace('#include "lve_window.hpp"\n', '#include "lve_window.hpp"\n\n#include <cstdint>\n', 1)
if 'uint64_t getSwapChainGeneration() const' not in text:
    text = text.replace(
        '  VkRenderPass getSwapChainRenderPass() const { return lveSwapChain->getRenderPass(); }\n',
        '  VkRenderPass getSwapChainRenderPass() const { return lveSwapChain->getRenderPass(); }\n  uint64_t getSwapChainGeneration() const { return swapChainGeneration; }\n', 1)
    text = text.replace(
        '  bool isFrameStarted{false};\n',
        '  bool isFrameStarted{false};\n  uint64_t swapChainGeneration{0};\n', 1)
header.write_text(text)

renderer = Path('src/lve_renderer.cpp')
text = renderer.read_text()
marker = '''  if (lveSwapChain == nullptr) {
    lveSwapChain = std::make_unique<LveSwapChain>(lveDevice, extent);
  } else {
'''
if '++swapChainGeneration;' not in text:
    if marker not in text: raise SystemExit('renderer swapchain marker missing')
    tail = '''    if (!oldSwapChain->compareSwapFormats(*lveSwapChain.get())) {
      throw std::runtime_error("Swap chain image(or depth) format has changed!");
    }
  }
}
'''
    replacement = '''    if (!oldSwapChain->compareSwapFormats(*lveSwapChain.get())) {
      throw std::runtime_error("Swap chain image(or depth) format has changed!");
    }
  }
  ++swapChainGeneration;
}
'''
    if tail not in text: raise SystemExit('renderer recreation tail marker missing')
    text = text.replace(tail, replacement, 1)
renderer.write_text(text)

direct = Path('tools/vulkax_physics_vulkan_direct.cpp')
text = direct.read_text()
needle = '''      if (auto commandBuffer = renderer_.beginFrame()) {
        const std::array<float, 4> parameters{1.0f, 1.0f, 1.0f, spin_};
'''
replacement = '''      if (auto commandBuffer = renderer_.beginFrame()) {
        ensureGraphicsPipelineCompatible();
        const std::array<float, 4> parameters{1.0f, 1.0f, 1.0f, spin_};
'''
if 'ensureGraphicsPipelineCompatible();' not in text:
    if needle not in text: raise SystemExit('direct presenter frame marker missing')
    text = text.replace(needle, replacement, 1)

old_pipeline = '''    lve::PipelineConfigInfo config{};
    lve::LvePipeline::defaultPipelineConfigInfo(config);
    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.renderPass = renderer_.getSwapChainRenderPass();
    config.pipelineLayout = graphicsPipelineLayout_;
    pipeline_ = std::make_unique<lve::LvePipeline>(
        device_,
        "shaders/vulkax_direct_wave.vert.spv",
        "shaders/vulkax_direct_wave.frag.spv",
        config);
  }

  [[nodiscard]] VkExtent2D renderExtent(VkExtent2D drawableExtent) const {
'''
new_pipeline = '''    rebuildGraphicsPipeline();
  }

  void rebuildGraphicsPipeline() {
    // LveRenderer owns the render pass and may replace it when the swapchain is
    // recreated. Rebuild the fullscreen presentation pipeline at the same
    // generation boundary; format equality alone is not enough to keep a
    // pipeline created against a destroyed render-pass object valid.
    pipeline_.reset();
    lve::PipelineConfigInfo config{};
    lve::LvePipeline::defaultPipelineConfigInfo(config);
    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.renderPass = renderer_.getSwapChainRenderPass();
    config.pipelineLayout = graphicsPipelineLayout_;
    pipeline_ = std::make_unique<lve::LvePipeline>(
        device_,
        "shaders/vulkax_direct_wave.vert.spv",
        "shaders/vulkax_direct_wave.frag.spv",
        config);
    graphicsPipelineSwapChainGeneration_ = renderer_.getSwapChainGeneration();
  }

  void ensureGraphicsPipelineCompatible() {
    if (!pipeline_ || graphicsPipelineSwapChainGeneration_ != renderer_.getSwapChainGeneration()) {
      rebuildGraphicsPipeline();
    }
  }

  [[nodiscard]] VkExtent2D renderExtent(VkExtent2D drawableExtent) const {
'''
if 'void ensureGraphicsPipelineCompatible()' not in text:
    if old_pipeline not in text: raise SystemExit('direct graphics pipeline marker missing')
    text = text.replace(old_pipeline, new_pipeline, 1)

member = '  std::unique_ptr<lve::LvePipeline> pipeline_;\n'
member_declaration = '  uint64_t graphicsPipelineSwapChainGeneration_ = 0;\n'
if member_declaration not in text:
    if member not in text: raise SystemExit('direct pipeline member marker missing')
    text = text.replace(member, member + member_declaration, 1)
direct.write_text(text)
