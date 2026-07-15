module;

#include <cstdio>

module graphics.vulkan;

import std.compat;
import vk_mem_alloc;

namespace graphics::vulkan {

void *AllocatedBuffer::map() const {
    if (!buffer) {
        return nullptr;
    }
    return buffer->getAllocation().map();
}

void AllocatedBuffer::unmap() const {
    if (buffer) {
        buffer->getAllocation().unmap();
    }
}

void AllocatedBuffer::flush(vk::DeviceSize offset,
                            vk::DeviceSize flushSize) const {
    if (buffer) {
        buffer->getAllocation().flush(offset, flushSize);
    }
}

void AllocatedBuffer::invalidate(vk::DeviceSize offset,
                                 vk::DeviceSize invalSize) const {
    if (buffer) {
        buffer->getAllocation().invalidate(offset, invalSize);
    }
}

bool VMAAllocator::initialize(const vk::raii::Instance &instance,
                              GPUDevice &device) {
    std::lock_guard<std::mutex> lock(mtx);
    if (initialized_) {
        std::println(stderr, "[VMAAllocator] Already initialized");
        return false;
    }

    device_ = device.getDevicePtr();

    // Setup VMA allocator create info using Vulkan-Hpp dynamic dispatch
    vma::AllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = Config::minApiVersion;
    allocatorInfo.instance = nullptr; // must be null
    allocatorInfo.physicalDevice = device.getPhysicalDevice();
    allocatorInfo.device = nullptr; // must be null

    // Use dynamic function dispatch
    allocatorInfo.flags = vma::AllocatorCreateFlagBits::eExtMemoryBudget;

    // Create the Vulkan functions struct for dynamic dispatch
    allocatorInfo.pVulkanFunctions = nullptr; // must be null

    try {
        allocator_ =
            std::make_unique<vma::raii::Allocator>(vma::raii::createAllocator(
                instance, device.getRaiiDevice(), allocatorInfo));
    } catch (const vk::SystemError &e) {
        std::println(stderr, "[VMAAllocator] Failed to create allocator: {}",
                     e.what());
        return false;
    }

    initialized_ = true;
    std::println("[VMAAllocator] Initialized for device: {}",
                 device.getInfo().name);
    return true;
}

void VMAAllocator::shutdown() {
    std::lock_guard<std::mutex> lock(mtx);
    if (!initialized_) {
        return;
    }

    // RAII handles cleanup - just reset the unique_ptr
    allocator_.reset();
    device_.reset();
    initialized_ = false;
    std::println("[VMAAllocator] Shutdown complete");
}

AllocatedBuffer VMAAllocator::createBuffer(const BufferCreateInfo &info) {
    std::lock_guard<std::mutex> lock(mtx);

    AllocatedBuffer result{};
    result.size = info.size;
    result.name = info.debugName;

    if (!allocator_) {
        std::println(stderr, "[VMAAllocator] Allocator not initialized");
        return result;
    }

    vk::BufferCreateInfo bufferInfo{
        {}, info.size, info.usage, vk::SharingMode::eExclusive};

    vma::AllocationCreateInfo allocInfo{};
    allocInfo.usage = info.memoryUsage;
    allocInfo.flags = info.flags;

    try {
        result.buffer = std::make_unique<vma::raii::Buffer>(
            *allocator_, bufferInfo, allocInfo);
    } catch (const vk::SystemError &e) {
        std::println(stderr, "[VMAAllocator] Failed to create buffer: {}",
                     e.what());
        return result;
    }

    return result;
}

AllocatedBuffer
VMAAllocator::createStagingBuffer(vk::DeviceSize size,
                                  const std::string &debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eTransferSrc;
    info.memoryUsage = vma::MemoryUsage::eCpuOnly;
    info.flags = vma::AllocationCreateFlagBits::eMapped;
    info.debugName = debugName.empty() ? "StagingBuffer" : debugName;
    return createBuffer(info);
}

AllocatedBuffer VMAAllocator::createVertexBuffer(vk::DeviceSize size,
                                                 const std::string &debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                 vk::BufferUsageFlagBits::eTransferDst;
    info.memoryUsage = vma::MemoryUsage::eGpuOnly;
    info.debugName = debugName.empty() ? "VertexBuffer" : debugName;
    return createBuffer(info);
}

AllocatedBuffer VMAAllocator::createIndexBuffer(vk::DeviceSize size,
                                                const std::string &debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                 vk::BufferUsageFlagBits::eTransferDst;
    info.memoryUsage = vma::MemoryUsage::eGpuOnly;
    info.debugName = debugName.empty() ? "IndexBuffer" : debugName;
    return createBuffer(info);
}

AllocatedBuffer
VMAAllocator::createUniformBuffer(vk::DeviceSize size,
                                  const std::string &debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eUniformBuffer;
    info.memoryUsage = vma::MemoryUsage::eCpuToGpu;
    info.flags = vma::AllocationCreateFlagBits::eMapped;
    info.debugName = debugName.empty() ? "UniformBuffer" : debugName;
    return createBuffer(info);
}

AllocatedBuffer
VMAAllocator::createStorageBuffer(vk::DeviceSize size,
                                  const std::string &debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                 vk::BufferUsageFlagBits::eTransferDst;
    info.memoryUsage = vma::MemoryUsage::eGpuOnly;
    info.debugName = debugName.empty() ? "StorageBuffer" : debugName;
    return createBuffer(info);
}

AllocatedBuffer
VMAAllocator::createIndexStorageBuffer(vk::DeviceSize size,
                                       const std::string &debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                 vk::BufferUsageFlagBits::eStorageBuffer |
                 vk::BufferUsageFlagBits::eTransferDst;
    info.memoryUsage = vma::MemoryUsage::eCpuToGpu;
    info.flags = vma::AllocationCreateFlagBits::eMapped;
    info.debugName = debugName.empty() ? "IndexStorageBuffer" : debugName;
    return createBuffer(info);
}

AllocatedBuffer
VMAAllocator::createHostVisibleStorageBuffer(vk::DeviceSize size,
                                             const std::string &debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eStorageBuffer;
    info.memoryUsage = vma::MemoryUsage::eCpuToGpu;
    info.flags = vma::AllocationCreateFlagBits::eMapped;
    info.debugName = debugName.empty() ? "HostVisibleStorageBuffer" : debugName;
    return createBuffer(info);
}

AllocatedImage VMAAllocator::createImage(const ImageCreateInfo &info) {
    std::lock_guard<std::mutex> lock(mtx);

    AllocatedImage result{};
    result.format = info.format;
    result.extent = info.extent;
    result.mipLevels = info.mipLevels;
    result.arrayLayers = info.arrayLayers;
    result.name = info.debugName;

    if (!allocator_) {
        std::println(stderr, "[VMAAllocator] Allocator not initialized");
        return result;
    }

    vk::ImageCreateInfo imageInfo{{},
                                  info.imageType,
                                  info.format,
                                  info.extent,
                                  info.mipLevels,
                                  info.arrayLayers,
                                  info.samples,
                                  info.tiling,
                                  info.usage,
                                  vk::SharingMode::eExclusive};

    vma::AllocationCreateInfo allocInfo{};
    allocInfo.usage = info.memoryUsage;
    allocInfo.flags = info.flags;

    try {
        result.image = std::make_unique<vma::raii::Image>(*allocator_,
                                                          imageInfo, allocInfo);
    } catch (const vk::SystemError &e) {
        std::println(stderr, "[VMAAllocator] Failed to create image: {}",
                     e.what());
        return result;
    }

    return result;
}

AllocatedImage VMAAllocator::createImage2D(uint32_t width, uint32_t height,
                                           vk::Format format,
                                           vk::ImageUsageFlags usage,
                                           const std::string &debugName) {
    ImageCreateInfo info{};
    info.imageType = vk::ImageType::e2D;
    info.format = format;
    info.extent = vk::Extent3D{width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = vk::SampleCountFlagBits::e1;
    info.tiling = vk::ImageTiling::eOptimal;
    info.usage = usage;
    info.memoryUsage = vma::MemoryUsage::eGpuOnly;
    info.debugName = debugName.empty() ? "Image2D" : debugName;
    return createImage(info);
}

bool VMAAllocator::createImageView(AllocatedImage &image,
                                   vk::ImageAspectFlags aspectMask) {
    if (!image.isValid() || !device_) {
        return false;
    }

    vk::ImageViewType viewType = vk::ImageViewType::e2D;
    if (image.arrayLayers > 1) {
        viewType = vk::ImageViewType::e2DArray;
    }

    vk::ImageViewCreateInfo viewInfo{
        {},
        image.getImage(),
        viewType,
        image.format,
        {}, // Component mapping
        vk::ImageSubresourceRange{aspectMask, 0, image.mipLevels, 0,
                                  image.arrayLayers}};

    try {
        image.view = std::make_unique<vk::raii::ImageView>(*device_, viewInfo);
        return true;
    } catch (const vk::SystemError &e) {
        std::println(stderr, "[VMAAllocator] Failed to create image view: {}",
                     e.what());
        return false;
    }
}

VMAAllocator::MemoryStats VMAAllocator::getStats() const {
    std::lock_guard<std::mutex> lock(mtx);

    MemoryStats stats{};
    if (!initialized_ || !allocator_) {
        return stats;
    }

    auto budget = allocator_->getHeapBudgets();
    for (const auto &heap : budget) {
        stats.totalAllocated += heap.statistics.blockBytes;
        stats.totalUsed += heap.statistics.allocationBytes;
        stats.allocationCount += heap.statistics.allocationCount;
    }

    return stats;
}

void VMAAllocator::setDebugName(const AllocatedBuffer &buffer,
                                const std::string &name) {
    if (!initialized_ || !buffer.isValid() || !allocator_) {
        return;
    }
    buffer.buffer->getAllocation().setName(name.c_str());
}

void VMAAllocator::setDebugName(const AllocatedImage &image,
                                const std::string &name) {
    if (!initialized_ || !image.isValid() || !allocator_) {
        return;
    }
    image.image->getAllocation().setName(name.c_str());
}

} // namespace graphics::vulkan
