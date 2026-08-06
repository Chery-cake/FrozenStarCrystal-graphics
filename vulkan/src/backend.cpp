module;

module graphics.vulkan;

import std.compat;
import vulkan;
import concurrency;

namespace graphics::vulkan {

Backend::Backend(const std::shared_ptr<concurrency::pool::Manager> &poolManager)
    : poolManager_(poolManager) {
  gpuPool_ = poolManager_->getPool(&gpuPoolDesc).lock();

  instance_ = std::make_unique<vulkan::instances::Instance>();
  deviceManager_ = std::make_unique<vulkan::devices::Manager>(
      instance_->getInstancePtr(), gpuPool_);
  shaderManager_ = std::make_unique<vulkan::shaders::Manager>();
  // Use a no-op deleter shared_ptr: pipelineManager_ is always destroyed
  // before shaderManager_ (shutdown() / destructor ordering guarantees this).
  pipelineManager_ = std::make_unique<vulkan::pipelines::Manager>(
      std::shared_ptr<vulkan::shaders::Manager>(shaderManager_.get(),
                                                [](auto *) {}));
}

Backend::~Backend() {
  currentFrame_ = RenderContext{};
  frameCmdPools_.clear();
  pipelineManager_.reset();
  shaderManager_.reset();
  deviceManager_.reset();
  instance_.reset();
  gpuPool_.reset();
  poolManager_.reset();

  std::cout << "[Backend] Cleared\n";
}

RenderContext Backend::beginFrame() {
  currentFrame_ = RenderContext{};

  auto devicesWithWindows = deviceManager_->getDevicesWithWindows();

  std::function prepareWindow =
      [&frameCmdPools = frameCmdPools_](
          const std::shared_ptr<vulkan::devices::Device> &device,
          const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo)
      -> RenderContext::WindowFrame {
    RenderContext::WindowFrame frame;
    frame.device = device;
    frame.windowInfo = windowInfo;
    frame.valid = false;

    // Quick check – swapchain must exist and be healthy
    if (!windowInfo->swapchain || windowInfo->swapchain->needRecreation()) {
      return frame;
    }

    auto acqResult = windowInfo->swapchain->acquireNextImage();
    if (!acqResult) {
      if (acqResult.error().code ==
          vulkan::devices::Swapchain::PresentError::Code::outOfDate) {
        // TODO
        // Swapchain is out of date – caller will resize later
      }
      return frame;
    }
    frame.imageIndex = *acqResult;

    auto imageData =
        windowInfo->swapchain->getSwapchainImageData(frame.imageIndex);
    if (!imageData) {
      return frame;
    }

    // Command buffer pool management
    // Retrieve the pre‑allocated command buffer for this image index
    auto &cmdData = frameCmdPools.at(windowInfo);
    vk::CommandBuffer cmd = *cmdData.pool.buffers[frame.imageIndex];

    // Record commands
    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    cmd.begin(beginInfo);

    vk::Image swapchainImage = imageData->image;
    vk::Extent2D extent = imageData->extent;
    vk::Format format = imageData->format;

    // Barrier: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    vk::ImageMemoryBarrier preBarrier{
        vk::AccessFlagBits::eNone,
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::QueueFamilyIgnored,
        vk::QueueFamilyIgnored,
        swapchainImage,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput, {},
                        {}, {}, preBarrier);

    // Create the swapchain image view for this frame
    vk::ImageViewCreateInfo viewInfo{
        {},
        swapchainImage,
        vk::ImageViewType::e2D,
        format,
        {},
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    auto ownedView = std::make_shared<vk::raii::ImageView>(
        *device->getDevicePtr(), viewInfo);
    vk::ImageView viewHandle = **ownedView;

    // beginRendering with clear-to-black
    vk::RenderingAttachmentInfo colorAttachment{
        viewHandle,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ResolveModeFlagBits::eNone,
        nullptr,
        vk::ImageLayout::eUndefined,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eStore,
        vk::ClearValue{std::array<float, 4>{0.0F, 0.0F, 0.0F, 1.0F}}};
    vk::RenderingInfo renderingInfo{
        {}, vk::Rect2D{{0, 0}, extent}, 1U,      0U,
        1U, &colorAttachment,           nullptr, nullptr};
    cmd.beginRendering(renderingInfo);

    frame.cmd = cmd;
    frame.image = swapchainImage;
    frame.view = viewHandle;
    frame.extent = extent;
    frame.format = format;
    frame.ownedView = std::move(ownedView);
    frame.valid = true;
    return frame;
  };

  // Flatten devices→windows and transform each into a WindowFrame
  auto allWindows = devicesWithWindows |
                    std::views::transform([&](const auto &entry) {
                      return entry.device->getWindows() |
                             std::views::transform([&](const auto &win) {
                               return prepareWindow(entry.device, win);
                             });
                    }) |
                    std::views::join;

  // Collect all results (valid or not) into the context
  std::ranges::move(allWindows, std::back_inserter(currentFrame_.windows));

  return currentFrame_;
}

void Backend::endFrame() {

  auto validWindows =
      currentFrame_.windows |
      std::views::filter([](const auto &wf) { return wf.valid; });

  std::ranges::for_each(validWindows, [](RenderContext::WindowFrame &wf) {
    vk::CommandBuffer cmd = wf.cmd;
    cmd.endRendering();

    // Barrier: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
    vk::ImageMemoryBarrier postBarrier{
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::AccessFlagBits::eMemoryRead,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::QueueFamilyIgnored,
        vk::QueueFamilyIgnored,
        wf.image,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {},
                        postBarrier);

    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.setCommandBuffers(cmd);

    vk::Queue graphicsQueue{};
    graphicsQueue = wf.device->getGraphicsQueue();

    if (graphicsQueue) {
      auto presentResult = wf.windowInfo->swapchain->submitAndPresent(
          graphicsQueue, std::span<vk::SubmitInfo>(&submitInfo, 1),
          wf.imageIndex);
      if (!presentResult) {
        // Mark for recreation on next frame
        wf.windowInfo->swapchain->markForRecreation();
      }
    }
  });

  currentFrame_ = RenderContext{};
}

void Backend::waitIdle() {
  if (!deviceManager_) {
    return;
  }
  auto entries = deviceManager_->getDeviceEntries();
  std::ranges::for_each(entries,
                        [](const auto &entry) { entry.device->waitIdle(); });
}

void Backend::addRequiredExtensions(std::span<const char *const> extensions) {
  std::ranges::for_each(extensions, [](const char *ext) {
    vulkan::instances::Config::instance().addInstanceExtension(ext);
  });
}

void Backend::createWindow(
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
    uint32_t framesInFlight, vk::Extent2D extent) {
  auto entries = deviceManager_->getDeviceEntries();
  if (entries.empty()) {
    throw std::runtime_error("[Backend] No Vulkan device available");
  }
  // Register on the first (highest-scored) device
  createWindow(entries.front().device, windowInfo, framesInFlight, extent);
}

void Backend::createWindow(
    const std::shared_ptr<vulkan::devices::Device> &device,
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
    uint32_t framesInFlight, vk::Extent2D extent) {
  if (!device) {
    throw std::runtime_error("[Backend] No Vulkan device available");
  }

  // Build swapchain info with the actual size
  vulkan::devices::Swapchain::SwapchainInfo info;
  info.extent = extent;
  device->createWindow(windowInfo, framesInFlight, info);

  // Determine how many command buffers we need (= swapchain image count)
  auto swapInfo = windowInfo->swapchain->getinfo();
  uint32_t imageCount = swapInfo.imageCount;

  // Create the command pool and allocate one buffer per swapchain image
  uint32_t queueFamily = device->getQueueFamilies().graphicsQueue.value_or(0);
  vulkan::devices::CommandPoolCreateInfo cpInfo{
      .queueFamily = queueFamily,
      .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
  };
  auto [it, inserted] = frameCmdPools_.emplace(
      windowInfo, WindowCmdData{.pool =
                                    vulkan::devices::CommandBufferPool{
                                        device->getDevicePtr(), cpInfo},
                                .framesInFlight = framesInFlight});
  it->second.pool.allocate(
      imageCount); // one command buffer per swapchain image
}

void Backend::resizeWindow(
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
    uint32_t newWidth, uint32_t newHeight) {
  if (windowInfo->swapchain) {
    windowInfo->swapchain->recreateSwapchain(newWidth, newHeight);
  }
}

bool Backend::removeWindow(
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo) {
  // Remove from cmd pool map
  frameCmdPools_.erase(windowInfo);

  auto entries = deviceManager_->getDeviceEntries();
  return std::ranges::any_of(entries, [&](const auto &entry) {
    return entry.device->removeWindow(windowInfo);
  });
}

void Backend::reloadShader(const vulkan::shaders::Shader *tag) {
  shaderManager_->reloadShader(tag);
  pipelineManager_->invalidateShader(tag);
}

} // namespace graphics::vulkan
