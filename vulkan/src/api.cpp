module;

module graphics.vulkan;

import std.compat;
import vulkan;
import concurrency;

namespace graphics::vulkan {

Api::Api(const std::shared_ptr<concurrency::pool::Manager> &poolManager)
    : poolManager_(poolManager) {
  bool created = poolManager_->createPool(&gpuPoolDesc);
  gpuPool_ = poolManager_->getPool(&gpuPoolDesc).lock();

  instance_ = std::make_unique<vulkan::instances::Instance>();
  deviceManager_ = std::make_unique<vulkan::devices::Manager>(
      instance_->getInstancePtr(), gpuPool_);
  shaderManager_ = std::make_unique<vulkan::shaders::Manager>();
  pipelineManager_ = std::make_unique<vulkan::pipelines::Manager>(
      std::shared_ptr<vulkan::shaders::Manager>(shaderManager_.get(),
                                                [](auto *) {}));

  if (created) {
    poolManager_->resizePool(&gpuPoolDesc,
                             deviceManager_->getDeviceEntries().size());
  }
  std::println("[Api] Initialized - GPU thread pool have {} workers",
               gpuPool_->size());
}

Api::~Api() {
  frameCmdPools_.clear();
  pipelineManager_.reset();
  shaderManager_.reset();
  deviceManager_.reset();
  instance_.reset();
  gpuPool_.reset();
  poolManager_.reset();

  std::cout << "[Api] Cleared\n";
}

WindowFrame
Api::beginFrame(const std::shared_ptr<devices::Device> &device,
                const std::shared_ptr<devices::WindowInfo> &windowInfo) {
  WindowFrame frame;
  frame.device = device;
  frame.windowInfo = windowInfo;
  frame.valid = false;

  if (!windowInfo->swapchain || windowInfo->swapchain->needRecreation()) {
    return frame;
  }

  auto acqResult = windowInfo->swapchain->acquireNextImage();
  if (!acqResult) {
    if (acqResult.error().code ==
        vulkan::devices::Swapchain::PresentError::Code::outOfDate) {
      // Caller will resize later
    }
    return frame;
  }
  frame.imageIndex = *acqResult;

  auto imageData =
      windowInfo->swapchain->getSwapchainImageData(frame.imageIndex);
  if (!imageData) {
    return frame;
  }

  // Retrieve the pre-allocated command buffer for this image index
  auto it = frameCmdPools_.find(windowInfo);
  if (it == frameCmdPools_.end()) {
    return frame; // window not fully initialized
  }
  auto &cmdData = it->second;
  vk::CommandBuffer cmd = *cmdData.pool.primary[frame.imageIndex];

  // Begin command buffer
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
                      vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {},
                      {}, preBarrier);

  // Create swapchain image view for this frame
  vk::ImageViewCreateInfo viewInfo{
      {},
      swapchainImage,
      vk::ImageViewType::e2D,
      format,
      {},
      vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
  auto ownedView =
      std::make_shared<vk::raii::ImageView>(*device->getDevicePtr(), viewInfo);
  vk::ImageView viewHandle = **ownedView;

  // Begin rendering with clear-to-black
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
}

WindowFrame
Api::beginFrame(const std::shared_ptr<devices::WindowInfo> &windowInfo) {
  auto device = findDeviceForWindow(windowInfo);
  if (!device) {
    WindowFrame frame;
    frame.windowInfo = windowInfo;
    frame.valid = false;
    return frame;
  }
  return beginFrame(device, windowInfo);
}

void Api::endFrame(WindowFrame &frame) {
  if (!frame.valid) {
    return;
  }

  vk::CommandBuffer cmd = frame.cmd;
  cmd.endRendering();

  // Barrier: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
  vk::ImageMemoryBarrier postBarrier{
      vk::AccessFlagBits::eColorAttachmentWrite,
      vk::AccessFlagBits::eMemoryRead,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::ePresentSrcKHR,
      vk::QueueFamilyIgnored,
      vk::QueueFamilyIgnored,
      frame.image,
      vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                      vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {},
                      postBarrier);

  cmd.end();

  vk::SubmitInfo submitInfo{};
  submitInfo.setCommandBuffers(cmd);

  vk::Queue graphicsQueue = frame.device->getGraphicsQueue();
  if (graphicsQueue) {
    auto presentResult = frame.windowInfo->swapchain->submitAndPresent(
        graphicsQueue, std::span<vk::SubmitInfo>(&submitInfo, 1),
        frame.imageIndex);
    if (!presentResult) {
      frame.windowInfo->swapchain->markForRecreation();
    }
  }
}

void Api::waitIdle() {
  if (!deviceManager_) {
    return;
  }
  auto entries = deviceManager_->getDeviceEntries();
  std::ranges::for_each(entries,
                        [](const auto &entry) { entry.device->waitIdle(); });
}

void Api::addRequiredExtensions(std::span<const char *const> extensions) {
  std::ranges::for_each(extensions, [](const char *ext) {
    vulkan::instances::Config::instance().addInstanceExtension(ext);
  });
}

void Api::createWindow(
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
    uint32_t framesInFlight, vk::Extent2D extent) {
  auto entries = deviceManager_->getDeviceEntries();
  if (entries.empty()) {
    throw std::runtime_error("[Api] No Vulkan device available");
  }
  // Register on the first (highest-scored) device
  createWindow(entries.front().device, windowInfo, framesInFlight, extent);
}

void Api::createWindow(
    const std::shared_ptr<vulkan::devices::Device> &device,
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
    uint32_t framesInFlight, vk::Extent2D extent) {
  if (!device) {
    throw std::runtime_error("[Api] No Vulkan device available");
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
  it->second.pool.allocatePrimary(
      imageCount); // one command buffer per swapchain image
}

void Api::resizeWindow(
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo,
    uint32_t newWidth, uint32_t newHeight) {
  if (windowInfo->swapchain) {
    windowInfo->swapchain->recreateSwapchain(newWidth, newHeight);
  }
}

bool Api::removeWindow(
    const std::shared_ptr<vulkan::devices::WindowInfo> &windowInfo) {
  // Remove from cmd pool map
  frameCmdPools_.erase(windowInfo);

  auto entries = deviceManager_->getDeviceEntries();
  return std::ranges::any_of(entries, [&](const auto &entry) {
    return entry.device->removeWindow(windowInfo);
  });
}

void Api::reloadShader(const vulkan::shaders::Shader *tag) {
  shaderManager_->reloadShader(tag);
  pipelineManager_->invalidateShader(tag);
}

std::expected<std::shared_ptr<vk::raii::Pipeline>, pipelines::PipelineError>
Api::createPipeline(const pipelines::DynamicPipelineInfo &info,
                    const std::shared_ptr<devices::Device> &device) {
  auto dev = device ? device : getFirstDevice();
  if (!dev) {
    return std::unexpected(pipelines::PipelineError{
        .code = pipelines::PipelineError::Code::creationFailed,
        .message = "No Vulkan device available"});
  }
  return pipelineManager_->getOrCreate(info, dev->getDevicePtr());
}

std::expected<std::shared_ptr<vk::raii::Pipeline>, pipelines::PipelineError>
Api::createPipeline(const pipelines::StaticPipelineInfo &info,
                    const std::shared_ptr<devices::Device> &device) {
  auto dev = device ? device : getFirstDevice();
  if (!dev) {
    return std::unexpected(pipelines::PipelineError{
        .code = pipelines::PipelineError::Code::creationFailed,
        .message = "No Vulkan device available"});
  }
  return pipelineManager_->getOrCreate(info, dev->getDevicePtr());
}

std::expected<std::shared_ptr<vk::raii::Pipeline>, pipelines::PipelineError>
Api::createPipeline(const pipelines::ComputePipelineInfo &info,
                    const std::shared_ptr<devices::Device> &device) {
  auto dev = device ? device : getFirstDevice();
  if (!dev) {
    return std::unexpected(pipelines::PipelineError{
        .code = pipelines::PipelineError::Code::creationFailed,
        .message = "No Vulkan device available"});
  }
  return pipelineManager_->getOrCreate(info, dev->getDevicePtr());
}

std::shared_ptr<devices::Device> Api::findDeviceForWindow(
    const std::shared_ptr<devices::WindowInfo> &windowInfo) const {
  auto entries = deviceManager_->getDeviceEntries();

  auto it = std::ranges::find_if(
      entries, [&windowInfo](const devices::Manager::DeviceEntry &entry) {
        auto windows = entry.device->getWindows();
        return std::ranges::find(windows, windowInfo) != windows.end();
      });

  return it != entries.end() ? it->device : nullptr;
}

} // namespace graphics::vulkan
