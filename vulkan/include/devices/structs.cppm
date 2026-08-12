module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.devices:structs;

import std.compat;
import vulkan;
import vk_mem_alloc;

export namespace graphics::vulkan::devices {

class Device;
class Swapchain;

struct FROZENSTARCRYSTAL_GRAPHICS_API QueueFamilyIndices {
  std::optional<uint32_t> graphicsQueue;
  std::optional<uint32_t> computeQueue;
  std::optional<uint32_t> transferQueue;
  std::optional<uint32_t> sparseBindingQueue;
  std::optional<uint32_t> protectedQueue;

  constexpr auto
  operator<=>(const QueueFamilyIndices &) const noexcept = default;

  [[nodiscard]] bool hasGraphics() const { return graphicsQueue.has_value(); }
  [[nodiscard]] bool hasCompute() const { return computeQueue.has_value(); }
  [[nodiscard]] bool hasTransfer() const { return transferQueue.has_value(); }
  [[nodiscard]] bool hasSparseBinding() const {
    return sparseBindingQueue.has_value();
  }
  [[nodiscard]] bool hasProtected() const { return protectedQueue.has_value(); }

  [[nodiscard]] bool isComplete() const {
    return hasGraphics() && hasCompute() && hasTransfer() &&
           hasSparseBinding() && hasProtected();
  }
  [[nodiscard]] bool isMinimumComplete() const {
    return hasGraphics() && hasCompute() && hasTransfer();
  }
};

struct FROZENSTARCRYSTAL_GRAPHICS_API GPUInfo {
  uint32_t index;
  std::string name;
  vk::PhysicalDeviceType type;
  uint32_t vendorId;
  uint32_t deviceId;
  vk::DeviceSize totalMemory;
  uint32_t apiVersion;
  uint32_t driverVersion;
  QueueFamilyIndices queueFamilies;

  constexpr auto operator<=>(const GPUInfo &) const noexcept = default;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API WindowInfo {
  std::unique_ptr<vk::raii::SurfaceKHR> surface;
  std::unique_ptr<Swapchain> swapchain;

  std::shared_ptr<vk::raii::Instance> instance;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API AllocatedBuffer {
private:
  mutable bool mapped = false;

public:
  std::unique_ptr<vma::raii::Buffer> buffer;
  vk::DeviceSize size;
  std::string name;

  AllocatedBuffer() = default;
  ~AllocatedBuffer() {
    unmap();
    buffer.reset();
  }

  // Move only
  AllocatedBuffer(const AllocatedBuffer &) = delete;
  AllocatedBuffer &operator=(const AllocatedBuffer &) = delete;
  AllocatedBuffer(AllocatedBuffer &&) = default;
  AllocatedBuffer &operator=(AllocatedBuffer &&) = default;

  [[nodiscard]] bool isValid() const { return buffer != nullptr; }
  [[nodiscard]] vk::Buffer getBuffer() const {
    return buffer ? static_cast<vk::Buffer>(*buffer) : vk::Buffer{};
  }
  [[nodiscard]] vma::Allocation getAllocation() const {
    return buffer ? buffer->getAllocation() : vma::Allocation{};
  }

  // Map/unmap for host-visible buffers
  [[nodiscard]] void *map() const {
    if (!buffer) {
      return nullptr;
    }
    mapped = true;
    return buffer->getAllocation().map();
  }
  void unmap() const {
    if (buffer && mapped) {
      buffer->getAllocation().unmap();
      mapped = false;
    }
  }
  void flush(vk::DeviceSize offset = 0,
             vk::DeviceSize flushSize = vk::WholeSize) const {
    if (buffer) {
      buffer->getAllocation().flush(offset, flushSize);
    }
  }
  void invalidate(vk::DeviceSize offset = 0,
                  vk::DeviceSize invalSize = vk::WholeSize) const {
    if (buffer) {
      buffer->getAllocation().invalidate(offset, invalSize);
    }
  }
};

struct FROZENSTARCRYSTAL_GRAPHICS_API AllocatedImage {
  std::unique_ptr<vma::raii::Image> image;
  std::unique_ptr<vk::raii::ImageView> view;
  vk::Format format;
  vk::Extent3D extent;
  vk::ImageLayout currentLayout = vk::ImageLayout::eUndefined;
  uint32_t mipLevels = 1;
  uint32_t arrayLayers = 1;
  std::string name;

  AllocatedImage() = default;
  ~AllocatedImage() = default;

  // Move only
  AllocatedImage(const AllocatedImage &) = delete;
  AllocatedImage &operator=(const AllocatedImage &) = delete;
  AllocatedImage(AllocatedImage &&) noexcept = default;
  AllocatedImage &operator=(AllocatedImage &&) noexcept = default;

  [[nodiscard]] bool isValid() const { return image != nullptr; }
  [[nodiscard]] vk::Image getImage() const {
    return image ? static_cast<vk::Image>(*image) : vk::Image{};
  }
  [[nodiscard]] vk::ImageView getView() const {
    return view ? static_cast<vk::ImageView>(*view) : vk::ImageView{};
  }
  [[nodiscard]] vma::Allocation getAllocation() const {
    return image ? image->getAllocation() : vma::Allocation{};
  }
};

struct FROZENSTARCRYSTAL_GRAPHICS_API BufferCreateInfo {
  enum class Access : uint8_t {
    gpuOnly,
    stagingUpload,
    stagingReadback,
    persistentMapping,
  };

  vk::DeviceSize size = 0;
  vk::BufferUsageFlags usage;
  Access access = Access::gpuOnly;
  vma::MemoryUsage memoryUsage = vma::MemoryUsage::eAuto;
  vma::AllocationCreateFlags flags;
  std::string debugName;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API ImageCreateInfo {
  vk::ImageType imageType = vk::ImageType::e2D;
  vk::Format format = vk::Format::eR8G8B8A8Srgb;
  vk::Extent3D extent = {1, 1, 1};
  uint32_t mipLevels = 1;
  uint32_t arrayLayers = 1;
  vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
  vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
  vk::ImageUsageFlags usage;
  vk::ImageLayout layout = vk::ImageLayout::eUndefined;
  vma::MemoryUsage memoryUsage = vma::MemoryUsage::eAuto;
  vma::AllocationCreateFlags flags;

  // ----- View creation -----
  bool createImageView = true;
  vk::ImageViewType viewType = vk::ImageViewType::e2D;
  vk::ComponentMapping components = {
      vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG,
      vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA};
  vk::ImageSubresourceRange subresourceRange = {
      vk::ImageAspectFlagBits::eColor, // default aspect
      0,                               // base mip level
      vk::RemainingMipLevels,          // use constant from Vulkan
      0,                               // base array layer
      vk::RemainingArrayLayers};

  std::string debugName;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API CommandPoolCreateInfo {
  uint32_t queueFamily;
  vk::CommandPoolCreateFlags flags =
      vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API CommandBufferPool {
public:
  std::unique_ptr<vk::raii::CommandPool> pool;
  std::vector<vk::raii::CommandBuffer> primary;
  std::vector<vk::raii::CommandBuffer> secondary;
  std::shared_ptr<vk::raii::Device> device;

private:
  std::vector<vk::raii::CommandBuffer> freePrimary;
  std::vector<vk::raii::CommandBuffer> freeSecondary;

public:
  CommandBufferPool(const std::shared_ptr<vk::raii::Device> &devicePtr,
                    const CommandPoolCreateInfo &createInfo) {
    vk::CommandPoolCreateInfo info = {createInfo.flags, createInfo.queueFamily};
    device = devicePtr;
    pool = std::make_unique<vk::raii::CommandPool>(*devicePtr, info);
  }
  ~CommandBufferPool() = default;

  // Delete move & copy
  CommandBufferPool(const CommandBufferPool &) = delete;
  CommandBufferPool &operator=(const CommandBufferPool &) = delete;
  CommandBufferPool(CommandBufferPool &&) noexcept = default;
  CommandBufferPool &operator=(CommandBufferPool &&) noexcept = default;

  void allocatePrimary(uint32_t count) {
    vk::CommandBufferAllocateInfo allocInfo{
        *pool, vk::CommandBufferLevel::ePrimary, count};

    std::ranges::transform(device->allocateCommandBuffers(allocInfo),
                           std::back_inserter(primary),
                           [](auto &buf) { return std::move(buf); });
  }
  void allocateSecondery(uint32_t count) {
    vk::CommandBufferAllocateInfo allocInfo{
        *pool, vk::CommandBufferLevel::eSecondary, count};
    std::ranges::transform(device->allocateCommandBuffers(allocInfo),
                           std::back_inserter(secondary),
                           [](auto &buf) { return std::move(buf); });
  }

  vk::raii::CommandBuffer acquirePrimary() {
    if (!freePrimary.empty()) {
      auto cmd = std::move(freePrimary.back());
      freePrimary.pop_back();
      return cmd;
    }
    // Allocate a single new buffer from the pool.
    allocatePrimary(1);
    auto cmd = std::move(primary.back());
    primary.pop_back();
    return cmd;
  }

  vk::raii::CommandBuffer acquireSecondary() {
    if (!freeSecondary.empty()) {
      auto cmd = std::move(freeSecondary.back());
      freeSecondary.pop_back();
      return cmd;
    }
    // Allocate a single new buffer from the pool.
    allocateSecondery(1);
    auto cmd = std::move(secondary.back());
    secondary.pop_back();
    return cmd;
  }

  void releasePrimary(vk::raii::CommandBuffer cmd) {
    try {
      cmd.reset(); // calls vkResetCommandBuffer
    } catch (vk::Error &e) {
      std::cout << "Couldn't reset primary command buffer: " << e.what()
                << '\n';
    }
    primary.push_back(std::move(cmd));
  }

  void releaseSeconday(vk::raii::CommandBuffer cmd) {
    try {
      cmd.reset(); // calls vkResetCommandBuffer
    } catch (vk::Error &e) {
      std::cout << "Couldn't reset secondary command buffer: " << e.what()
                << '\n';
    }
    secondary.push_back(std::move(cmd));
  }
};

struct CommandBufferHandle {
  std::reference_wrapper<CommandBufferPool> pool;
  vk::raii::CommandBuffer buffer = nullptr;
  operator vk::CommandBuffer() const { return *buffer; }

  CommandBufferHandle(std::reference_wrapper<CommandBufferPool> pool)
      : pool(pool) {
    buffer = pool.get().acquirePrimary();
  }

  ~CommandBufferHandle() {
    if (buffer != nullptr) { // not moved‑from
      pool.get().releasePrimary(std::move(buffer));
    }
  }
  // move‑only
  CommandBufferHandle(CommandBufferHandle &&) = default;
  CommandBufferHandle &operator=(CommandBufferHandle &&) = default;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API FenceWaiter {
  struct CoroutineSubmission {
    vk::raii::Fence fence;
    CommandBufferHandle cmdBuffer;
    std::coroutine_handle<> continuation;
  };

  std::jthread fenceWaiter;
  std::once_flag start;

  std::unordered_map<std::shared_ptr<Device>, std::vector<CoroutineSubmission>>
      submissions;

  std::mutex mtx;

  FenceWaiter() {
    std::call_once(start, [&]() { loop(); });
  }
  ~FenceWaiter() {
    fenceWaiter.request_stop();
    if (fenceWaiter.joinable()) {
      fenceWaiter.join();
    }
  }

  void enqueueFence(const std::shared_ptr<Device> &device,
                    vk::raii::Fence fence, CommandBufferHandle cmd,
                    std::coroutine_handle<> continuation) {

    std::unique_lock lock(mtx);
    submissions[device].emplace_back(std::move(fence), std::move(cmd),
                                     continuation);
  }

private:
  void loop();
};

struct FenceAwaiter {
  FenceWaiter &waiter;
  std::shared_ptr<Device> device;
  vk::raii::Fence fence;
  CommandBufferHandle cmdBuffer;

  static bool await_ready() noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) {
    waiter.enqueueFence(device, std::move(fence), std::move(cmdBuffer), h);
  }

  void await_resume() const noexcept {}
};

} // namespace graphics::vulkan::devices
