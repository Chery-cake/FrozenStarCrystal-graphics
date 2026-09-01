module;

export module vulkan_helper;

export import std.compat;
export import graphics;
export import std.compat;
export import vulkan;
export import vk_mem_alloc;

export import concurrency;

export {

  using namespace graphics::vulkan;

  class SwapchainRenderTarget {
  public:
    SwapchainRenderTarget(std::shared_ptr<devices::WindowInfo> win,
                          std::shared_ptr<devices::Device> dev)
        : windowInfo_(win), device_(dev) {
      createImageViews();
      defaultClearColor_ = {0.0f, 0.0f, 0.0f, 1.0f}; // black
    }

    // RenderTargetPolicy requirements
    graphics::vulkan::compositors::FrameContext beginFrame() {
      auto &sc = *windowInfo_->swapchain;
      if (sc.needRecreation())
        return {}; // invalid frame

      auto acq = sc.acquireNextImage();
      if (!acq)
        return {};

      uint32_t imageIndex = *acq;
      auto data = sc.getSwapchainImageData(imageIndex);
      if (!data)
        return {};

      // Ensure image views exist (recreate if swapchain changed)
      if (imageViews_.empty())
        createImageViews();

      // Get a command buffer from the device
      auto &pool = device_->getGraphicsPool();
      pool.allocatePrimary(1); // we'll leak them for simplicity
      vk::CommandBuffer cmd = *pool.primary.back();
      cmd.begin(vk::CommandBufferBeginInfo{
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

      // Transition swapchain image from UNDEFINED to
      // COLOR_ATTACHMENT_OPTIMAL
      vk::ImageMemoryBarrier preBarrier{
          vk::AccessFlagBits::eNone,
          vk::AccessFlagBits::eColorAttachmentWrite,
          vk::ImageLayout::eUndefined,
          vk::ImageLayout::eColorAttachmentOptimal,
          vk::QueueFamilyIgnored,
          vk::QueueFamilyIgnored,
          data->image,
          vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                    1}};
      cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                          vk::PipelineStageFlagBits::eColorAttachmentOutput,
                          vk::DependencyFlags{}, {}, {}, preBarrier);

      // Perform default clear to black
      vk::RenderingAttachmentInfo clearAtt{
          *imageViews_[imageIndex],
          vk::ImageLayout::eColorAttachmentOptimal,
          vk::ResolveModeFlagBits::eNone,
          nullptr,
          vk::ImageLayout::eUndefined,
          vk::AttachmentLoadOp::eClear,
          vk::AttachmentStoreOp::eStore,
          vk::ClearValue{defaultClearColor_}};
      vk::RenderingInfo clearRenderInfo{
          {},     vk::Rect2D{{0, 0}, data->extent}, 1, 0, 1, &clearAtt, nullptr,
          nullptr};
      cmd.beginRendering(clearRenderInfo);
      cmd.endRendering();

      // Build FrameContext
      graphics::vulkan::compositors::FrameContext fc;
      fc.cmd = cmd;
      fc.image = data->image;
      fc.view = *imageViews_[imageIndex];
      fc.extent = data->extent;
      fc.format = data->format;
      fc.imageIndex = imageIndex;
      fc.valid = true;

      // Store current image for later transition in endFrame
      currentImage_ = data->image;
      currentIndex_ = imageIndex;

      return fc;
    }

    void endFrame(graphics::vulkan::compositors::FrameContext &fc) {
      if (!fc.valid)
        return;

      // Transition from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
      vk::ImageMemoryBarrier postBarrier{
          vk::AccessFlagBits::eColorAttachmentWrite,
          vk::AccessFlagBits::eMemoryRead,
          vk::ImageLayout::eColorAttachmentOptimal,
          vk::ImageLayout::ePresentSrcKHR,
          vk::QueueFamilyIgnored,
          vk::QueueFamilyIgnored,
          currentImage_,
          vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                    1}};
      fc.cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                             vk::PipelineStageFlagBits::eBottomOfPipe,
                             vk::DependencyFlags{}, {}, {}, postBarrier);

      fc.cmd.end();

      vk::SubmitInfo submit{};
      submit.setCommandBuffers(fc.cmd);
      auto queue = device_->getGraphicsQueue();
      auto result = windowInfo_->swapchain->submitAndPresent(
          queue, std::span<vk::SubmitInfo>(&submit, 1), fc.imageIndex);
      // ignore result
    }

    void recreate() { createImageViews(); }

    vk::Extent2D extent() const {
      return windowInfo_->swapchain->getinfo().extent;
    }

    vk::Format colorFormat() const {
      return windowInfo_->swapchain->getinfo().surfaceFormat.format;
    }

    void setDefaultClearColor(std::array<float, 4> color) {
      defaultClearColor_ = color;
    }

  private:
    void createImageViews() {
      imageViews_.clear();
      auto swapInfo = windowInfo_->swapchain->getinfo();
      uint32_t count = swapInfo.imageCount;
      imageViews_.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        auto data = windowInfo_->swapchain->getSwapchainImageData(i);
        if (!data)
          continue;
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = data->image;
        viewInfo.viewType = swapInfo.imageViewType;
        viewInfo.format = data->format;
        viewInfo.components = swapInfo.imageViewComponents;
        viewInfo.subresourceRange = swapInfo.imageViewSubresourceRange;
        imageViews_.push_back(std::make_unique<vk::raii::ImageView>(
            *device_->getDevicePtr(), viewInfo));
      }
    }

    std::array<float, 4> defaultClearColor_;
    std::shared_ptr<devices::WindowInfo> windowInfo_;
    std::shared_ptr<devices::Device> device_;
    std::vector<std::unique_ptr<vk::raii::ImageView>> imageViews_;
    vk::Image currentImage_ = nullptr;
    uint32_t currentIndex_ = 0;
  };

  // ClearScene remains unchanged; it now correctly uses frame.view.
  class ClearScene {
  public:
    ClearScene(std::array<float, 4> clearColor = {0.2f, 0.3f, 0.4f, 1.0f})
        : clearColor_(clearColor) {}

    void beginRecord(const graphics::vulkan::compositors::FrameContext &frame) {
      // No-op; the render target already set up the layout.
    }

    concurrency::pool::coroutine::CoroutineTask<
        concurrency::pool::coroutine::policy::Suspend::Never, void>
    record(const graphics::vulkan::compositors::FrameContext &frame) {
      // Record drawing commands into frame.cmd
      vk::RenderingAttachmentInfo colorAtt{
          frame.view,
          vk::ImageLayout::eColorAttachmentOptimal,
          vk::ResolveModeFlagBits::eNone,
          nullptr,
          vk::ImageLayout::eUndefined,
          vk::AttachmentLoadOp::eClear,
          vk::AttachmentStoreOp::eStore,
          vk::ClearValue{clearColor_}};
      vk::RenderingInfo renderInfo{
          {},     vk::Rect2D{{0, 0}, frame.extent}, 1, 0, 1, &colorAtt, nullptr,
          nullptr};
      frame.cmd.beginRendering(renderInfo);
      frame.cmd.endRendering();
      co_return;
    }

    void endRecord(const graphics::vulkan::compositors::FrameContext &frame) {
      // No-op
    }

    graphics::vulkan::compositors::SceneRenderContext sceneContext() const {
      return {}; // empty for now
    }
    void
    setSceneContext(const graphics::vulkan::compositors::SceneRenderContext &) {
    }

  private:
    std::array<float, 4> clearColor_;
  };

  // New: RegionClearScene – clears a subrect with a color
  class RegionClearScene {
  public:
    RegionClearScene(std::array<float, 4> clearColor, vk::Rect2D region)
        : clearColor_(clearColor), region_(region) {}

    void beginRecord(const graphics::vulkan::compositors::FrameContext &frame) {
      // No-op
    }

    concurrency::pool::coroutine::CoroutineTask<
        concurrency::pool::coroutine::policy::Suspend::Never, void>
    record(const graphics::vulkan::compositors::FrameContext &frame) {
      // Begin rendering with loadOp=eLoad to preserve existing content
      vk::RenderingAttachmentInfo colorAtt{
          frame.view,
          vk::ImageLayout::eColorAttachmentOptimal,
          vk::ResolveModeFlagBits::eNone,
          nullptr,
          vk::ImageLayout::eUndefined,
          vk::AttachmentLoadOp::eLoad,
          vk::AttachmentStoreOp::eStore,
          vk::ClearValue{clearColor_}};
      vk::RenderingInfo renderInfo{
          {},     vk::Rect2D{{0, 0}, frame.extent}, 1, 0, 1, &colorAtt, nullptr,
          nullptr};
      frame.cmd.beginRendering(renderInfo);

      // Clear only the specified region
      vk::ClearRect clearRect{region_, 0, 1};
      frame.cmd.clearAttachments(
          vk::ClearAttachment{vk::ImageAspectFlagBits::eColor, 0,
                              vk::ClearValue{clearColor_}},
          clearRect);

      frame.cmd.endRendering();
      co_return;
    }

    void endRecord(const graphics::vulkan::compositors::FrameContext &frame) {
      // No-op
    }

    graphics::vulkan::compositors::SceneRenderContext sceneContext() const {
      return {};
    }
    void
    setSceneContext(const graphics::vulkan::compositors::SceneRenderContext &) {
    }

  private:
    std::array<float, 4> clearColor_;
    vk::Rect2D region_;
  };

  // New shader for fullscreen blend
  shaders::Shader g_fullscreenShader{
      .entryPoints = {{"vs_main", vk::ShaderStageFlagBits::eVertex},
                      {"fs_main", vk::ShaderStageFlagBits::eFragment}},
      .sourcePath = "fullscreen.slang"}; // assumes this file exists

  // BlendScene – draws fullscreen triangle with blending
  class BlendScene {
  public:
    BlendScene(std::shared_ptr<pipelines::Manager> pipelineManager,
               std::shared_ptr<devices::Device> device,
               std::array<float, 4> color)
        : pipelineManager_(pipelineManager), device_(device), color_(color) {
      createPipeline();
    }

    void beginRecord(const graphics::vulkan::compositors::FrameContext &frame) {
      // No-op
    }

    concurrency::pool::coroutine::CoroutineTask<
        concurrency::pool::coroutine::policy::Suspend::Never, void>
    record(const graphics::vulkan::compositors::FrameContext &frame) {
      // Begin rendering with loadOp=eLoad (preserve existing)
      vk::RenderingAttachmentInfo colorAtt{
          frame.view,
          vk::ImageLayout::eColorAttachmentOptimal,
          vk::ResolveModeFlagBits::eNone,
          nullptr,
          vk::ImageLayout::eUndefined,
          vk::AttachmentLoadOp::eLoad,
          vk::AttachmentStoreOp::eStore,
          {}};
      vk::RenderingInfo renderInfo{
          {},     vk::Rect2D{{0, 0}, frame.extent}, 1, 0, 1, &colorAtt, nullptr,
          nullptr};
      frame.cmd.beginRendering(renderInfo);

      frame.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline_);
      vk::Viewport viewport{
          0, 0, (float)frame.extent.width, (float)frame.extent.height, 0, 1};
      frame.cmd.setViewport(0, viewport);
      frame.cmd.setScissor(0, vk::Rect2D{{0, 0}, frame.extent});
      frame.cmd.pushConstants<vk::ArrayWrapper1D<float, 4>>(
          *pipelineLayout_, vk::ShaderStageFlagBits::eFragment, 0,
          vk::ArrayWrapper1D<float, 4>{color_});
      frame.cmd.draw(3, 1, 0, 0);

      frame.cmd.endRendering();
      co_return;
    }

    void endRecord(const graphics::vulkan::compositors::FrameContext &frame) {}

    graphics::vulkan::compositors::SceneRenderContext sceneContext() const {
      return {};
    }
    void
    setSceneContext(const graphics::vulkan::compositors::SceneRenderContext &) {
    }

  private:
    void createPipeline() {
      // Push constant range for color
      vk::PushConstantRange pushRange{vk::ShaderStageFlagBits::eFragment, 0,
                                      sizeof(float) * 4};
      vk::PipelineLayoutCreateInfo layoutCI{};
      layoutCI.setPushConstantRanges(pushRange);
      pipelineLayout_ = std::make_unique<vk::raii::PipelineLayout>(
          device_->getRaiiDevice(), layoutCI);

      // Blend state: enable alpha blending
      vk::PipelineColorBlendAttachmentState blendAttachment{};
      blendAttachment.blendEnable = vk::True;
      blendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
      blendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
      blendAttachment.colorBlendOp = vk::BlendOp::eAdd;
      blendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
      blendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
      blendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
      blendAttachment.colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

      pipelines::DynamicPipelineInfo dynInfo;
      dynInfo.tag.shaderTag = &g_fullscreenShader;
      dynInfo.tag.layout = **pipelineLayout_;
      dynInfo.inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
      dynInfo.rasterization.cullMode = vk::CullModeFlagBits::eNone;
      dynInfo.depthStencil.depthTest = vk::False;
      dynInfo.attachments.color = {
          vk::Format::eB8G8R8A8Srgb}; // should match swapchain
      dynInfo.colorBlendAttachments = {blendAttachment};

      auto result =
          pipelineManager_->getOrCreate(dynInfo, device_->getDevicePtr());
      if (!result) {
        throw std::runtime_error("Failed to create blend pipeline: " +
                                 result.error().message);
      }
      pipeline_ = *result;
    }

    std::shared_ptr<pipelines::Manager> pipelineManager_;
    std::shared_ptr<devices::Device> device_;
    std::array<float, 4> color_;
    std::unique_ptr<vk::raii::PipelineLayout> pipelineLayout_;
    std::shared_ptr<vk::raii::Pipeline> pipeline_;
  };
}
