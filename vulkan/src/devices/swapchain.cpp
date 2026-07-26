module;

#include <cstdint>

module graphics.vulkan.devices;

import std.compat;
import vulkan;

namespace graphics::vulkan::devices {

namespace {

vk::SurfaceFormatKHR
chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &formats,
                    const vk::SurfaceFormatKHR useSurfaceFormat) {
  // Look for preferred format
  for (const auto &format : formats) {
    if (format.format == useSurfaceFormat.format &&
        format.colorSpace == useSurfaceFormat.colorSpace) {
      return format;
    }
  }

  // Fall back to first available
  return formats[0];
}

vk::PresentModeKHR
choosePresentMode(const std::vector<vk::PresentModeKHR> &modes,
                  const vk::PresentModeKHR useMode, bool vsync) {
  // If vsync enabled, use FIFO (always available)
  if (vsync) {
    return vk::PresentModeKHR::eFifo;
  }

  // Look for preferred mode
  for (const auto &mode : modes) {
    if (mode == useMode) {
      return mode;
    }
  }

  // Try mailbox for no vsync
  for (const auto &mode : modes) {
    if (mode == vk::PresentModeKHR::eMailbox) {
      return mode;
    }
  }

  // Try immediate
  for (const auto &mode : modes) {
    if (mode == vk::PresentModeKHR::eImmediate) {
      return mode;
    }
  }

  // Fall back to FIFO
  return vk::PresentModeKHR::eFifo;
}

vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR &capabilities,
                          uint32_t windowWidth, uint32_t windowHeight) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }

  vk::Extent2D extent = {windowWidth, windowHeight};
  extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                            capabilities.maxImageExtent.width);
  extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height);
  return extent;
}

} // namespace

Swapchain::Swapchain(
    const std::shared_ptr<vk::raii::PhysicalDevice> &physicalDevice,
    const std::shared_ptr<vk::raii::Device> &device,
    const std::shared_ptr<WindowInfo> &windowInfo, SwapchainInfo info,
    uint32_t framesInFlight, vk::Queue presentQueue,
    uint32_t presentQueueFamily)
    : physicalDevice_(physicalDevice), device_(device), windowInfo_(windowInfo),
      info_(info), framesInFlight_(framesInFlight), presentQueue_(presentQueue),
      presentQueueFamily_(presentQueueFamily) {
  std::unique_lock lock(mtx_);

  auto capabilities =
      physicalDevice_->getSurfaceCapabilitiesKHR(*windowInfo->surface);
  auto formats = physicalDevice_->getSurfaceFormatsKHR(*windowInfo->surface);
  auto presentModes =
      physicalDevice_->getSurfacePresentModesKHR(*windowInfo->surface);

  uint32_t imageCount = std::max(info_.imageCount, capabilities.minImageCount);
  if (capabilities.maxImageCount > 0) {
    imageCount = std::min(imageCount, capabilities.maxImageCount);
  }
  info_.imageCount = imageCount;

  info_.surfaceFormat = chooseSurfaceFormat(formats, info.surfaceFormat);
  info_.extent =
      chooseExtent(capabilities, info_.extent.width, info_.extent.height);
  info_.presentMode =
      choosePresentMode(presentModes, info_.presentMode, info_.vsync);
  info_.preTransform = capabilities.currentTransform;

  info_.imageArrayLayers = capabilities.maxImageArrayLayers > 0
                               ? capabilities.maxImageArrayLayers
                               : info.imageArrayLayers;

  vk::SwapchainCreateInfoKHR createInfo = {info_.swapchainFlags,
                                           *windowInfo->surface,
                                           info_.imageCount,
                                           info_.surfaceFormat.format,
                                           info_.surfaceFormat.colorSpace,
                                           info_.extent,
                                           info_.imageArrayLayers,
                                           info_.imageUseFlags,
                                           info_.sharingMode,
                                           presentQueueFamily_,
                                           info_.preTransform,
                                           info_.compositeAlpha,
                                           info_.presentMode,
                                           info_.clipped,
                                           nullptr};

  swapchain_ = std::make_unique<vk::raii::SwapchainKHR>(*device, createInfo);

  auto images = swapchain_->getImages();
  frames_.resize(images.size());

  std::ranges::for_each(std::views::iota(0U, images.size()),
                        [&frame = frames_, &images](uint32_t i) {
                          frame[i].image = images[i];
                          frame[i].index = i;
                        });

  std::ranges::for_each(
      frames_, [&flags = info_.imageViewFlags, &type = info_.imageViewType,
                &format = info_.surfaceFormat.format,
                &component = info_.imageViewComponents,
                &subresource = info_.imageViewSubresourceRange,
                &device = *device_](SwapchainFrame &frame) {
        vk::ImageViewCreateInfo viewInfo{flags,  frame.image, type,
                                         format, component,   subresource};
        frame.imageViews =
            std::make_unique<vk::raii::ImageView>(device, viewInfo);
      });

  needRecreation_.store(false, std::memory_order_release);

  imageAvailableSemaphores_.reserve(framesInFlight_);
  renderFinishedSemaphores_.reserve(framesInFlight_);
  inFlightFences_.reserve(framesInFlight_);
  waitFences_.reserve(framesInFlight_);

  std::ranges::for_each(
      std::views::iota(0U, framesInFlight_),
      [&images = imageAvailableSemaphores_,
       &renders = renderFinishedSemaphores_, &flightFences = inFlightFences_,
       &waitFences = waitFences_, &device = device_](uint32_t) {
        images.emplace_back(*device, vk::SemaphoreCreateInfo{});
        renders.emplace_back(*device, vk::SemaphoreCreateInfo{});
        flightFences.emplace_back(
            *device, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
        waitFences.emplace_back(
            *device, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
      });

  std::println("[Swapchain] Created: {}x{} ({} images)", info_.extent.width,
               info_.extent.height, frames_.size());
}

Swapchain::~Swapchain() {
  try {
    device_->waitIdle();
  } catch (const vk::Error &e) {
    std::cout << "[Swapchain] Couldn't wait for device\n";
  }

  frames_.clear();

  // 1. Destroy all device‑level child objects while the device is still alive
  imageAvailableSemaphores_.clear();
  renderFinishedSemaphores_.clear();
  inFlightFences_.clear();
  waitFences_.clear();

  // 2. Now it's safe to tear down the swapchain and device
  swapchain_.reset();

  windowInfo_.reset();
  device_.reset();
  physicalDevice_.reset();

  std::cout << "[Swapchain] Cleared\n";
}

bool Swapchain::recreateSwapchain(uint32_t newWidth, uint32_t newHeight) {
  if (newWidth == 0 || newHeight == 0) {
    return false;
  }

  std::unique_lock lock(mtx_);

  device_->waitIdle();
  auto windowInfo = windowInfo_.lock();

  auto oldSwapchain = std::move(swapchain_);

  auto capabilities =
      physicalDevice_->getSurfaceCapabilitiesKHR(*windowInfo->surface);
  auto formats = physicalDevice_->getSurfaceFormatsKHR(*windowInfo->surface);
  auto presentModes =
      physicalDevice_->getSurfacePresentModesKHR(*windowInfo->surface);

  uint32_t imageCount = std::max(info_.imageCount, capabilities.minImageCount);
  if (capabilities.maxImageCount > 0) {
    imageCount = std::min(imageCount, capabilities.maxImageCount);
  }
  info_.imageCount = imageCount;

  info_.surfaceFormat = chooseSurfaceFormat(formats, info_.surfaceFormat);
  info_.extent = chooseExtent(capabilities, newWidth, newHeight);
  info_.presentMode =
      choosePresentMode(presentModes, info_.presentMode, info_.vsync);
  info_.preTransform = capabilities.currentTransform;

  info_.imageArrayLayers = capabilities.maxImageArrayLayers > 0
                               ? capabilities.maxImageArrayLayers
                               : info_.imageArrayLayers;

  vk::SwapchainCreateInfoKHR createInfo = {info_.swapchainFlags,
                                           *windowInfo->surface,
                                           info_.imageCount,
                                           info_.surfaceFormat.format,
                                           info_.surfaceFormat.colorSpace,
                                           info_.extent,
                                           info_.imageArrayLayers,
                                           info_.imageUseFlags,
                                           info_.sharingMode,
                                           presentQueueFamily_,
                                           info_.preTransform,
                                           info_.compositeAlpha,
                                           info_.presentMode,
                                           info_.clipped,
                                           *oldSwapchain};

  std::ranges::for_each(
      frames_, [](SwapchainFrame &frame) { frame.imageViews.reset(); });

  swapchain_ = std::make_unique<vk::raii::SwapchainKHR>(*device_, createInfo);

  auto images = swapchain_->getImages();
  frames_.resize(images.size());

  std::ranges::for_each(std::views::iota(0U, images.size()),
                        [&frame = frames_, &images](uint32_t i) {
                          frame[i].image = images[i];
                          frame[i].index = i;
                        });

  std::ranges::for_each(
      frames_, [&flags = info_.imageViewFlags, &type = info_.imageViewType,
                &format = info_.surfaceFormat.format,
                &component = info_.imageViewComponents,
                &subresource = info_.imageViewSubresourceRange,
                &device = *device_](SwapchainFrame &frame) {
        vk::ImageViewCreateInfo viewInfo{flags,  frame.image, type,
                                         format, component,   subresource};
        frame.imageViews =
            std::make_unique<vk::raii::ImageView>(device, viewInfo);
      });

  needRecreation_.store(false, std::memory_order_release);

  std::println("[Swapchain] Recreated: {}x{}", info_.extent.width,
               info_.extent.height);

  return true;
}

void Swapchain::waitForFrameFence() {
  // Wait until the fence for the current frame slot is signaled,
  // then reset it. This ensures we never overwrite an image still in flight.
  auto result = device_->waitForFences(*inFlightFences_[currentFrame_],
                                       vk::True, UINT64_MAX);
  if (result != vk::Result::eSuccess) {
    throw std::runtime_error("Failed to wait for in-flight fence");
  }
  device_->resetFences(*inFlightFences_[currentFrame_]);
}

std::expected<uint32_t, Swapchain::PresentError> Swapchain::acquireNextImage() {
  std::unique_lock lock(mtx_);

  if (frameAcquired_) {
    return std::unexpected<PresentError>(
        PresentError{.code = PresentError::Code::acquired,
                     .message = "acquireNextImage called without a "
                                "preceding submitAndPresent"});
  }

  waitForFrameFence();

  auto [result, index] = swapchain_->acquireNextImage(
      UINT64_MAX, *imageAvailableSemaphores_[currentFrame_]);

  switch (result) {
  case vk::Result::eSuccess:
    break;
  case vk::Result::eSuboptimalKHR:
    needRecreation_.store(true, std::memory_order_release);
    break;
  case vk::Result::eErrorOutOfDateKHR:
    needRecreation_.store(true, std::memory_order_release);
    return std::unexpected(PresentError{.code = PresentError::Code::outOfDate,
                                        .message = "Swapchain out of date"});
  default:
    throw std::runtime_error("vkAcquireNextImageKHR failed");
  }

  frameAcquired_ = true;
  return index;
}

std::expected<vk::Result, Swapchain::PresentError> Swapchain::submitAndPresent(
    vk::Queue queue, std::span<vk::SubmitInfo> submitInfos, uint32_t imageIndex,
    vk::PipelineStageFlags imageWaitStage) {

  std::unique_lock lock(mtx_);

  if (!frameAcquired_) {
    return std::unexpected(PresentError{.code = PresentError::Code::notAcquired,
                                        .message =
                                            "submitAndPresent called without a "
                                            "preceding acquireNextImage"});
  }
  if (submitInfos.empty()) {
    return std::unexpected(
        PresentError{.code = PresentError::Code::noWorkSubmitted,
                     .message = "At least one submitInfo is required"});
  }

  // Pick the semaphores for the current in‑flight slot
  vk::Semaphore imageAvailable = *imageAvailableSemaphores_[currentFrame_];
  vk::Semaphore renderFinished = *renderFinishedSemaphores_[currentFrame_];

  std::vector<vk::SubmitInfo> infos(submitInfos.begin(), submitInfos.end());

  // ---- First SubmitInfo: add the image-available semaphore ----
  std::vector<vk::Semaphore> firstWaits;
  std::vector<vk::PipelineStageFlags> firstStages;

  if (!infos.empty()) {
    auto &first = infos.front();
    // Preserve any existing waits
    if (first.waitSemaphoreCount > 0) {
      firstWaits.assign(first.pWaitSemaphores,
                        first.pWaitSemaphores + first.waitSemaphoreCount);
      firstStages.assign(first.pWaitDstStageMask,
                         first.pWaitDstStageMask + first.waitSemaphoreCount);
    }
    firstWaits.push_back(imageAvailable);
    firstStages.push_back(imageWaitStage);
    first.setWaitSemaphores(firstWaits);
    first.setWaitDstStageMask(firstStages);
  }

  // ---- Last SubmitInfo: add the render-finished semaphore ----
  std::vector<vk::Semaphore> lastSignals;
  if (!infos.empty()) {
    auto &last = infos.back();
    if (last.signalSemaphoreCount > 0) {
      lastSignals.assign(last.pSignalSemaphores,
                         last.pSignalSemaphores + last.signalSemaphoreCount);
    }
    lastSignals.push_back(renderFinished);
    last.setSignalSemaphores(lastSignals);
  }

  // Submit everything
  queue.submit(infos, inFlightFences_[currentFrame_]);

  // Present using the renderFinished semaphore
  vk::PresentInfoKHR presentInfo{};
  presentInfo.setWaitSemaphores(renderFinished);
  presentInfo.setSwapchains(**swapchain_);
  presentInfo.setImageIndices(imageIndex);

  frameAcquired_ = false;

  vk::Result result;
  try {
    result = presentQueue_.presentKHR(presentInfo);
  } catch (const vk::OutOfDateKHRError &) {
    needRecreation_.store(true, std::memory_order_release);
    return std::unexpected(PresentError{.code = PresentError::Code::outOfDate,
                                        .message = "Swapchain out of date"});
  } catch (const vk::DeviceLostError &e) {
    return std::unexpected(PresentError{.code = PresentError::Code::deviceLost,
                                        .message = e.what()});
  } catch (const std::exception &e) {
    return std::unexpected(
        PresentError{.code = PresentError::Code::unknown, .message = e.what()});
  }

  // Advance frame index only on success
  if (result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR) {
    currentFrame_ = (currentFrame_ + 1) % framesInFlight_;
  }

  if (result == vk::Result::eSuboptimalKHR) {
    needRecreation_.store(true, std::memory_order_release);
  }

  if (result == vk::Result::eErrorOutOfDateKHR) {
    needRecreation_.store(true, std::memory_order_release);
  }

  return result;
}

vk::Result Swapchain::submitAndWait(vk::Queue queue,
                                    std::span<vk::SubmitInfo> submitInfos) {

  uint32_t wait;

  {
    std::unique_lock lock(mtx_);

    std::vector<vk::SubmitInfo> infos(submitInfos.begin(), submitInfos.end());

    auto waitResult = device_->waitForFences(*waitFences_[currentWait_],
                                             vk::True, UINT64_MAX);
    if (waitResult != vk::Result::eSuccess) {
      throw std::runtime_error("Failed to wait for wait fence");
    }
    device_->resetFences(*waitFences_[currentWait_]);

    queue.submit(infos, waitFences_[currentWait_]);

    wait = currentWait_;

    currentWait_ = (currentWait_ + 1) % framesInFlight_;
  }

  auto result =
      device_->waitForFences(*waitFences_[wait], vk::True, UINT64_MAX);

  if (result == vk::Result::eSuccess) {
    device_->resetFences(*waitFences_[wait]);
  }

  return result;
}

} // namespace graphics::vulkan::devices
