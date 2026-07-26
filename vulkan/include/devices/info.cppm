module;

#include <utility>

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan.devices:info;

import std.compat;
import vulkan;
import vk_mem_alloc;

export namespace graphics::vulkan::devices {

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

class Swapchain;

struct FROZENSTARCRYSTAL_GRAPHICS_API WindowInfo {
  std::unique_ptr<vk::raii::SurfaceKHR> surface;
  std::unique_ptr<Swapchain> swapchain;

  std::shared_ptr<vk::raii::Instance> instance;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API AllocatedBuffer {
  std::unique_ptr<vma::raii::Buffer> buffer;
  vk::DeviceSize size;
  std::string name;

  AllocatedBuffer() = default;
  ~AllocatedBuffer() = default;

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
    return buffer->getAllocation().map();
  }
  void unmap() const {
    if (buffer) {
      buffer->getAllocation().unmap();
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
  vk::DeviceSize size = 0;
  vk::BufferUsageFlags usage;
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
  vma::MemoryUsage memoryUsage = vma::MemoryUsage::eAuto;
  vma::AllocationCreateFlags flags;
  std::string debugName;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API CommandBufferPool {
  std::unique_ptr<vk::raii::CommandPool> pool;
  std::vector<vk::raii::CommandBuffer> buffers;
  std::shared_ptr<vk::raii::Device> device;

  CommandBufferPool(std::shared_ptr<vk::raii::Device> devicePtr,
                    uint32_t queueFamily) {
    device = std::move(devicePtr);
    vk::CommandPoolCreateInfo createInfo = {
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamily};

    pool = std::make_unique<vk::raii::CommandPool>(*device, createInfo);
  }
  CommandBufferPool(std::shared_ptr<vk::raii::Device> devicePtr,
                    vk::CommandPoolCreateInfo &createInfo) {
    device = std::move(devicePtr);
    pool = std::make_unique<vk::raii::CommandPool>(*device, createInfo);
  }
  ~CommandBufferPool() = default;

  // Delete move & copy
  CommandBufferPool(const CommandBufferPool &) = delete;
  CommandBufferPool &operator=(const CommandBufferPool &) = delete;
  CommandBufferPool(CommandBufferPool &&) = delete;
  CommandBufferPool &operator=(CommandBufferPool &&) = delete;

  void reset() {
    buffers.clear();
    pool->reset();
  }

  void allocate(uint32_t count) {
    vk::CommandBufferAllocateInfo allocInfo{
        *pool, vk::CommandBufferLevel::ePrimary, count};

    buffers = device->allocateCommandBuffers(allocInfo);
  }
  void allocate(vk::CommandBufferAllocateInfo &allocInfo) {
    buffers = device->allocateCommandBuffers(allocInfo);
  }
};

} // namespace graphics::vulkan::devices
