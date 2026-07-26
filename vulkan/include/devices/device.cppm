module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.devices:device;

import std.compat;
import vulkan;
import vk_mem_alloc;

import :info;
import :swapchain;

namespace graphics::vulkan::devices {

struct GlobalThreadCleanup;

} // namespace graphics::vulkan::devices

export namespace graphics::vulkan::devices {

class FROZENSTARCRYSTAL_GRAPHICS_API Device {
private:
  std::shared_ptr<vk::raii::PhysicalDevice> physicalDevice_;
  std::shared_ptr<vk::raii::Device> device_;
  GPUInfo info_;
  std::unique_ptr<vma::raii::Allocator> allocator_;

  vk::Queue graphicsQueue_;
  vk::Queue computeQueue_;
  vk::Queue transferQueue_;
  vk::Queue sparseBidingQueue_;
  vk::Queue protectedQueue_;

  mutable std::mutex deviceMtx_;

  std::vector<AllocatedImage> computeImages_;
  mutable std::mutex computeImageMtx_;

  // Registered windows on this device
  std::vector<std::shared_ptr<WindowInfo>> windows_;
  mutable std::mutex windowMtx_;

  std::unordered_map<std::thread::id, std::unique_ptr<CommandBufferPool>>
      graphicsPools_; // thread_local
  std::unordered_map<std::thread::id, std::unique_ptr<CommandBufferPool>>
      computePools_; // thread_local
  std::unordered_map<std::thread::id, std::unique_ptr<CommandBufferPool>>
      transferPools_; // thread_local
  std::unordered_map<std::thread::id, std::unique_ptr<CommandBufferPool>>
      sparseBidingPools_; // thread_local
  std::unordered_map<std::thread::id, std::unique_ptr<CommandBufferPool>>
      protectedPools_; // thread_local
  std::shared_mutex poolsMtx_;

  std::vector<std::string> getAvailableExtensions();

  void ensureThreadCleanup();
  void removeThreadPools(std::thread::id tid);
  friend GlobalThreadCleanup;

public:
  Device(const vk::raii::Instance &instance, vk::PhysicalDevice physicalDevice,
         const GPUInfo &info);
  ~Device();

  // Disable copy & move
  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;
  Device(Device &&other) = delete;
  Device &operator=(Device &&other) = delete;

  void createWindow(const std::shared_ptr<WindowInfo> &windowInfo,
                    uint32_t framesInFlight,
                    Swapchain::SwapchainInfo &swapInfo);
  void createWindow(const std::shared_ptr<WindowInfo> &windowInfo,
                    uint32_t framesInFlight);
  bool removeWindow(const std::shared_ptr<WindowInfo> &info);

  [[nodiscard]] AllocatedBuffer
  createBuffer(const BufferCreateInfo &info); // TODO return expected
  [[nodiscard]] AllocatedImage
  createImage(const ImageCreateInfo &info); // TODO return expected

  [[nodiscard]] CommandBufferPool &getGraphicsPool();
  [[nodiscard]] CommandBufferPool &getComputePool();
  [[nodiscard]] CommandBufferPool &getTransferPool();
  [[nodiscard]] CommandBufferPool &getSparseBidingPool();
  [[nodiscard]] CommandBufferPool &getProtectedPool();

  [[nodiscard]] const GPUInfo &getInfo() const { return info_; }
  [[nodiscard]] std::vector<std::shared_ptr<WindowInfo>> getWindows() const {
    std::unique_lock lock(windowMtx_);
    return windows_;
  }

  [[nodiscard]] std::shared_ptr<vk::raii::PhysicalDevice>
  getPhysicalDevicePtr() {
    return physicalDevice_;
  }
  [[nodiscard]] vk::PhysicalDevice getPhysicalDevice() const {
    return physicalDevice_ ? **physicalDevice_ : vk::PhysicalDevice{};
  }
  [[nodiscard]] const vk::raii::PhysicalDevice &getRaiiPhysicalDevice() const {
    return *physicalDevice_;
  }

  [[nodiscard]] std::shared_ptr<vk::raii::Device> getDevicePtr() const {
    return device_;
  }
  [[nodiscard]] vk::Device getDevice() const {
    return device_ ? **device_ : vk::Device{};
  }
  [[nodiscard]] const vk::raii::Device &getRaiiDevice() const {
    return *device_;
  }

  [[nodiscard]] vma::Allocator getAllocator() const {
    return allocator_ ? **allocator_ : vma::Allocator{};
  }
  [[nodiscard]] const vma::raii::Allocator &getRaiiAllocator() const {
    return *allocator_;
  }

  [[nodiscard]] vk::Queue getGraphicsQueue() const { return graphicsQueue_; }
  [[nodiscard]] vk::Queue getComputeQueue() const { return computeQueue_; }
  [[nodiscard]] vk::Queue getTransferQueue() const { return transferQueue_; }
  [[nodiscard]] vk::Queue getSparseBidingQueue() const {
    return sparseBidingQueue_;
  }
  [[nodiscard]] vk::Queue getProtectedQueue() const { return protectedQueue_; }
  [[nodiscard]] const QueueFamilyIndices &getQueueFamilies() const {
    return info_.queueFamilies;
  }

  void waitIdle() const { device_->waitIdle(); };
};

} // namespace graphics::vulkan::devices
