module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.devices:swapchain;

import std.compat;
import vulkan;

import :structs;

export namespace graphics::vulkan::devices {

class Device;

class FROZENSTARCRYSTAL_GRAPHICS_API Swapchain {
public:
  struct SwapchainInfo {
    vk::SwapchainCreateFlagsKHR swapchainFlags;
    uint32_t imageCount = 1;
    vk::SurfaceFormatKHR surfaceFormat = {vk::Format::eB8G8R8A8Srgb,
                                          vk::ColorSpaceKHR::eSrgbNonlinear};
    vk::Extent2D extent;
    uint32_t imageArrayLayers = 1;
    vk::ImageUsageFlags imageUseFlags =
        vk::ImageUsageFlagBits::eColorAttachment;
    vk::SharingMode sharingMode = vk::SharingMode::eExclusive;
    uint32_t presentQueueFamily = 0;
    vk::SurfaceTransformFlagBitsKHR preTransform;
    vk::CompositeAlphaFlagBitsKHR compositeAlpha =
        vk::CompositeAlphaFlagBitsKHR::eOpaque;
    vk::PresentModeKHR presentMode;
    vk::Bool32 clipped = vk::True;

    vk::ImageViewCreateFlags imageViewFlags;
    vk::ImageViewType imageViewType = vk::ImageViewType::e2D;
    vk::ComponentMapping imageViewComponents;
    vk::ImageSubresourceRange imageViewSubresourceRange =
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

    bool vsync = true;
  };

  struct SwapchainFrame {
    vk::Image image; // owned by swapchain
    std::unique_ptr<vk::raii::ImageView> imageViews;
    uint32_t index;
  };

  struct PresentError {
    enum class Code : uint8_t {
      noWorkSubmitted, // submitInfos was empty
      notAcquired,     // frameAcquired_ was false
      acquired,        // frameAcquired_ was true
      outOfDate,       // presentation failed with OUT_OF_DATE
      deviceLost,      // VK_ERROR_DEVICE_LOST etc.
      unknown
    } code;
    std::string message;
  };

  struct SwapchainImageData {
    vk::Image image;
    vk::Extent2D extent;
    vk::Format format;
  };

private:
  std::shared_ptr<vk::raii::PhysicalDevice> physicalDevice_;
  std::shared_ptr<vk::raii::Device> device_;
  std::weak_ptr<WindowInfo> windowInfo_;

  Device *owner_;

  std::unique_ptr<vk::raii::SwapchainKHR> swapchain_;

  SwapchainInfo info_;

  uint32_t currentFrame_ = 0;
  std::vector<SwapchainFrame> frames_;

  std::atomic<bool> needRecreation_;
  bool frameAcquired_ = false;

  uint32_t framesInFlight_;
  std::vector<vk::raii::Semaphore> imageAvailableSemaphores_;
  std::vector<vk::raii::Semaphore> renderFinishedSemaphores_;
  std::vector<vk::raii::Fence> inFlightFences_;

  uint32_t currentWait_ = 0;
  std::vector<vk::raii::Fence> waitFences_;

  vk::Queue presentQueue_;
  uint32_t presentQueueFamily_;

  std::mutex mtx_;

  [[nodiscard]] std::expected<void, vk::Result> waitForFrameFence();

public:
  Swapchain(Device *owner,
            const std::shared_ptr<vk::raii::PhysicalDevice> &physicalDevice,
            const std::shared_ptr<vk::raii::Device> &device,
            const std::shared_ptr<WindowInfo> &windowInfo, SwapchainInfo info,
            uint32_t framesInFlight, vk::Queue presentQueue,
            uint32_t presentQueueFamily);
  ~Swapchain();

  Swapchain(const Swapchain &) = delete;
  Swapchain &operator=(const Swapchain &) = delete;
  Swapchain(Swapchain &&) = delete;
  Swapchain &operator=(Swapchain &&) = delete;

  bool recreateSwapchain(uint32_t newWidth, uint32_t newHeight);

  // uint32_t - image index
  [[nodiscard]] std::expected<uint32_t, PresentError> acquireNextImage();

  [[nodiscard]] std::expected<vk::Result, PresentError>
  submitAndPresent(vk::Queue queue, std::span<vk::SubmitInfo> submitInfos,
                   uint32_t imageIndex,
                   vk::PipelineStageFlags imageWaitStage =
                       vk::PipelineStageFlagBits::eColorAttachmentOutput);
  vk::Result submitAndWait(vk::Queue queue,
                           std::span<vk::SubmitInfo> submitInfos);

  [[nodiscard]] bool needRecreation() const {
    return needRecreation_.load(std::memory_order_acquire);
  }
  void markForRecreation() {
    needRecreation_.store(true, std::memory_order_release);
  }

  [[nodiscard]] SwapchainInfo getinfo() {
    std::unique_lock lock(mtx_);
    return info_;
  }
  void setinfo(SwapchainInfo &info) {
    std::unique_lock lock(mtx_);
    info_ = info;
    markForRecreation();
  }

  [[nodiscard]] const vk::Queue &getPresentQueue() const {
    return presentQueue_;
  }
  [[nodiscard]] uint32_t getPresentQueueFamily() const {
    return presentQueueFamily_;
  }

  [[nodiscard]] std::optional<SwapchainImageData>
  getSwapchainImageData(uint32_t index) {
    std::unique_lock lock(mtx_);
    if (index >= frames_.size()) {
      return std::nullopt;
    }
    return SwapchainImageData{
        .image = frames_[index].image,
        .extent = info_.extent,
        .format = info_.surfaceFormat.format,
    };
  }
};

} // namespace graphics::vulkan::devices
