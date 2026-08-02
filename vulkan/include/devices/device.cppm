module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.devices:device;

import std.compat;
import vulkan;
import vk_mem_alloc;
import concurrency.pool;
import concurrency.pool.coroutine;

import :structs;
import :swapchain;

export namespace graphics::vulkan::devices {

class FROZENSTARCRYSTAL_GRAPHICS_API Device {
private:
  friend struct ThreadPoolCleanup;
  friend void transfer(Device &device, const AllocatedBuffer &src,
                       vk::DeviceSize srcOffset, AllocatedBuffer &dst,
                       vk::DeviceSize dstOffset, vk::DeviceSize size);
  friend void transfer(Device &device, const AllocatedBuffer &src,
                       vk::DeviceSize bufferOffset, AllocatedImage &dst,
                       vk::ImageLayout dstFinalLayout);
  friend void transfer(Device &device, const AllocatedImage &src,
                       AllocatedBuffer &dst, vk::DeviceSize bufferOffset);
  friend void transfer(Device &device, const AllocatedImage &src,
                       AllocatedImage &dst,
                       vk::ImageLayout dstFinalLayout);

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
  std::unordered_set<std::thread::id> touchedThreads_;
  std::shared_mutex poolsMtx_;
  std::unique_ptr<concurrency::pool::ThreadPool> gpuPool_;
  std::shared_ptr<uint8_t> aliveToken_;

  std::vector<std::string> getAvailableExtensions();

  void markThreadTouched();
  void removeThreadPools(std::thread::id tid);

  template <std::invocable<vk::CommandBuffer> F>
  void submitOneShot(vk::Queue queue, uint32_t queueFamily, F &&recordFn);

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

  template <concurrency::pool::coroutine::policy::Queue QP =
                concurrency::pool::coroutine::policy::Queue::Inline>
  [[nodiscard]] concurrency::pool::coroutine::Scheduler<QP> schedule() noexcept;

  template <typename F, typename... Args>
  [[nodiscard]] auto submit(F &&f, Args &&...args)
      -> std::future<std::invoke_result_t<F, Args...>>;

  [[nodiscard]] size_t workerCount() const noexcept;

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

template <std::invocable<vk::CommandBuffer> F>
void Device::submitOneShot(vk::Queue queue, uint32_t queueFamily, F &&recordFn) {
  const CommandPoolCreateInfo createInfo{.queueFamily = queueFamily};
  CommandBufferPool pool{device_, createInfo};
  pool.allocate(1);
  auto &cmd = pool.buffers.front();
  cmd.begin(vk::CommandBufferBeginInfo{
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
  std::forward<F>(recordFn)(*cmd);
  cmd.end();

  vk::SubmitInfo submit;
  submit.setCommandBuffers(*cmd);
  vk::raii::Fence fence{*device_, vk::FenceCreateInfo{}};
  queue.submit(submit, *fence);
  auto result = device_->waitForFences(*fence, vk::True, UINT64_MAX);
  if (result != vk::Result::eSuccess) {
    throw std::runtime_error("Transfer submission failed");
  }
}

template <concurrency::pool::coroutine::policy::Queue QP>
concurrency::pool::coroutine::Scheduler<QP> Device::schedule() noexcept {
  return gpuPool_->schedule<QP>();
}

template <typename F, typename... Args>
auto Device::submit(F &&f, Args &&...args)
    -> std::future<std::invoke_result_t<F, Args...>> {
  return gpuPool_->submit(std::forward<F>(f), std::forward<Args>(args)...);
}

} // namespace graphics::vulkan::devices
