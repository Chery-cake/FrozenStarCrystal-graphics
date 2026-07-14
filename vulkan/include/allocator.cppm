module;

#include "FrozenStarCrystal-graphics_export.h"

export module graphics.vulkan:allocator;

import std.compat;
import vk_mem_alloc;

import :device;

export namespace graphics::vulkan {

struct FROZENSTARCRYSTAL_GRAPHICS_API AllocatedBuffer {
    std::unique_ptr<vma::raii::Buffer> buffer;
    vk::DeviceSize size = 0;
    std::string name;

    AllocatedBuffer() = default;
    ~AllocatedBuffer() = default;

    // Move only
    AllocatedBuffer(const AllocatedBuffer &) = delete;
    AllocatedBuffer &operator=(const AllocatedBuffer &) = delete;
    AllocatedBuffer(AllocatedBuffer &&) noexcept = default;
    AllocatedBuffer &operator=(AllocatedBuffer &&) noexcept = default;

    [[nodiscard]] bool isValid() const { return buffer != nullptr; }
    [[nodiscard]] vk::Buffer getBuffer() const {
        return buffer ? static_cast<vk::Buffer>(*buffer) : vk::Buffer{};
    }
    [[nodiscard]] vma::Allocation getAllocation() const {
        return buffer ? buffer->getAllocation() : vma::Allocation{};
    }

    // Map/unmap for host-visible buffers
    [[nodiscard]] void *map() const;
    void unmap() const;
    void flush(vk::DeviceSize offset = 0,
               vk::DeviceSize flushSize = vk::WholeSize) const;
    void invalidate(vk::DeviceSize offset = 0,
                    vk::DeviceSize invalSize = vk::WholeSize) const;
};

struct FROZENSTARCRYSTAL_GRAPHICS_API AllocatedImage {
    std::unique_ptr<vma::raii::Image> image;
    std::unique_ptr<vk::raii::ImageView> view;
    vk::Format format = vk::Format::eUndefined;
    vk::Extent3D extent = {};
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
        return image ? static_cast<vk::Image>(**image) : vk::Image{};
    }
    [[nodiscard]] vk::ImageView getView() const {
        return view ? static_cast<vk::ImageView>(**view) : vk::ImageView{};
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

class FROZENSTARCRYSTAL_GRAPHICS_API VMAAllocator {
  private:
    std::unique_ptr<vma::raii::Allocator> allocator_;
    std::shared_ptr<vk::raii::Device> device_;
    bool initialized_ = false;
    mutable std::mutex mtx;

  public:
    struct MemoryStats {
        vk::DeviceSize totalAllocated = 0;
        vk::DeviceSize totalUsed = 0;
        uint32_t allocationCount = 0;
    };
    [[nodiscard]] MemoryStats getStats() const;

    VMAAllocator() = default;
    ~VMAAllocator() { shutdown(); }

    // Disable copy & move
    VMAAllocator(const VMAAllocator &) = delete;
    VMAAllocator &operator=(const VMAAllocator &) = delete;
    VMAAllocator(VMAAllocator &&) = delete;
    VMAAllocator &operator=(VMAAllocator &&) = delete;

    bool initialize(const vk::raii::Instance &instance, GPUDevice &device);
    void shutdown();

    [[nodiscard]] bool isInitialized() const { return initialized_; }

    [[nodiscard]] vma::Allocator getAllocator() const {
        return allocator_ ? **allocator_ : vma::Allocator{};
    }
    [[nodiscard]] const vma::raii::Allocator &getRaiiAllocator() const {
        return *allocator_;
    }

    AllocatedBuffer createBuffer(const BufferCreateInfo &info);

    AllocatedBuffer createStagingBuffer(vk::DeviceSize size,
                                        const std::string &debugName = "");
    AllocatedBuffer createVertexBuffer(vk::DeviceSize size,
                                       const std::string &debugName = "");
    AllocatedBuffer createIndexBuffer(vk::DeviceSize size,
                                      const std::string &debugName = "");
    AllocatedBuffer createUniformBuffer(vk::DeviceSize size,
                                        const std::string &debugName = "");
    AllocatedBuffer createStorageBuffer(vk::DeviceSize size,
                                        const std::string &debugName = "");
    AllocatedBuffer createIndexStorageBuffer(vk::DeviceSize size,
                                             const std::string &debugName = "");
    AllocatedBuffer
    createHostVisibleStorageBuffer(vk::DeviceSize size,
                                   const std::string &debugName = "");

    AllocatedImage createImage(const ImageCreateInfo &info);

    AllocatedImage createImage2D(uint32_t width, uint32_t height,
                                 vk::Format format, vk::ImageUsageFlags usage,
                                 const std::string &debugName = "");
    bool createImageView(
        AllocatedImage &image,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor);

    void setDebugName(const AllocatedBuffer &buffer, const std::string &name);
    void setDebugName(const AllocatedImage &image, const std::string &name);
};

class FROZENSTARCRYSTAL_GRAPHICS_API VMAManager {
  private:
    std::vector<std::unique_ptr<VMAAllocator>> allocators_;
    uint32_t primaryIndex_ = 0;
    bool initialized_ = false;

  public:
    VMAManager() = default;
    ~VMAManager() { shutdown(); }

    // Disable copy & move
    VMAManager(const VMAManager &) = delete;
    VMAManager &operator=(const VMAManager &) = delete;
    VMAManager(VMAManager &&) = delete;
    VMAManager &operator=(VMAManager &&) = delete;

    bool initialize(const vk::raii::Instance &instance,
                    DeviceManager &deviceManager);
    void shutdown();

    [[nodiscard]] bool isInitialized() const { return initialized_; }

    [[nodiscard]] VMAAllocator &getPrimaryAllocator();
    [[nodiscard]] const VMAAllocator &getPrimaryAllocator() const;

    [[nodiscard]] VMAAllocator *getAllocator(uint32_t deviceIndex);
    [[nodiscard]] const VMAAllocator *getAllocator(uint32_t deviceIndex) const;

    [[nodiscard]] size_t getAllocatorCount() const {
        return allocators_.size();
    }
};

} // namespace graphics::vulkan
