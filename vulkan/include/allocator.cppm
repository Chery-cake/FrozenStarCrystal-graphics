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
    vma::AllocationCreateFlags flags = {};
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

} // namespace graphics::vulkan
