module;

export module graphics.vulkan.devices:transfers;

import std.compat;
import vulkan;

import :structs;
import :device;
import :swapchain;

import concurrency;

export namespace graphics::vulkan::devices {

// ---------- intra‑device transfers (same Device&) ----------

// Buffer -> Buffer
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
transfer(const std::shared_ptr<Device> &device, const AllocatedBuffer &src,
         vk::DeviceSize srcOffset, AllocatedBuffer &dst,
         vk::DeviceSize dstOffset, vk::DeviceSize size);

// Buffer -> Image (whole image, one mip level 0, layer 0, whole extent)
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
transfer(
    const std::shared_ptr<Device> &device, const AllocatedBuffer &src,
    vk::DeviceSize bufferOffset, AllocatedImage &dst,
    vk::ImageLayout dstFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal);

// Image -> Buffer (whole image, mip 0, layer 0)
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
transfer(const std::shared_ptr<Device> &device, const AllocatedImage &src,
         AllocatedBuffer &dst, vk::DeviceSize bufferOffset);

// Image -> Image (whole images, mip 0, layer 0)
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
transfer(
    const std::shared_ptr<Device> &device, const AllocatedImage &src,
    AllocatedImage &dst,
    vk::ImageLayout dstFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal);

// ---------- Swapchain helpers (record into existing command buffer) ----------

// Buffer -> Swapchain image
// Transitions the swapchain image from currentLayout to
// VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copies, then transitions to
// dstFinalLayout.
void recordTransfer(
    vk::CommandBuffer cmd, const AllocatedBuffer &src, vk::DeviceSize srcOffset,
    vk::Image swapchainImage, vk::Extent2D imageExtent, vk::Format imageFormat,
    vk::ImageLayout
        currentLayout, // e.g., VK_IMAGE_LAYOUT_UNDEFINED after acquire
    vk::ImageLayout dstFinalLayout = vk::ImageLayout::ePresentSrcKHR);

// Swapchain image -> Buffer
void recordTransfer(
    vk::CommandBuffer cmd, vk::Image swapchainImage, vk::Extent2D imageExtent,
    vk::Format imageFormat, vk::ImageLayout currentLayout, AllocatedBuffer &dst,
    vk::DeviceSize dstOffset,
    vk::ImageLayout finalLayout = vk::ImageLayout::ePresentSrcKHR);

// Image -> Swapchain image
void recordTransfer(
    vk::CommandBuffer cmd, const AllocatedImage &src,
    vk::ImageLayout srcCurrentLayout, vk::Image swapchainImage,
    vk::Extent2D imageExtent, vk::Format imageFormat,
    vk::ImageLayout currentLayout,
    vk::ImageLayout dstFinalLayout = vk::ImageLayout::ePresentSrcKHR);

// Swapchain image -> Image
void recordTransfer(
    vk::CommandBuffer cmd, vk::Image swapchainImage, vk::Extent2D imageExtent,
    vk::Format imageFormat, vk::ImageLayout currentLayout, AllocatedImage &dst,
    vk::ImageLayout dstCurrentLayout,
    vk::ImageLayout dstFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal);

// ---------- inter‑device transfers (two Device&) ----------

// Buffer -> Buffer (different devices)
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
transfer(const std::shared_ptr<Device> &srcDevice, const AllocatedBuffer &src,
         vk::DeviceSize srcOffset, const std::shared_ptr<Device> &dstDevice,
         AllocatedBuffer &dst, vk::DeviceSize dstOffset, vk::DeviceSize size);

// Buffer -> Image  (different devices)
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
transfer(
    const std::shared_ptr<Device> &srcDevice, const AllocatedBuffer &src,
    vk::DeviceSize srcOffset,

    const std::shared_ptr<Device> &dstDevice, AllocatedImage &dst,
    vk::ImageLayout dstFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal);

// Image -> Buffer  (different devices)
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
transfer(const std::shared_ptr<Device> &srcDevice, const AllocatedImage &src,
         const std::shared_ptr<Device> &dstDevice, AllocatedBuffer &dst,
         vk::DeviceSize dstOffset);

// Image -> Image  (different devices)
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
transfer(
    const std::shared_ptr<Device> &srcDevice, const AllocatedImage &src,
    const std::shared_ptr<Device> &dstDevice, AllocatedImage &dst,
    vk::ImageLayout dstFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal);

// (Swapchain inter‑device not provided – swapchain is tightly bound to its
// device)

// ---------- format / size helpers ----------

// Returns bytes per texel for uncompressed formats; bytes per 4×4 texel block
// for BCn block-compressed formats.  Pass a fallback to suppress the exception
// for unknown formats.
vk::DeviceSize
formatSize(vk::Format format,
           std::optional<vk::DeviceSize> fallback = std::nullopt);

// Returns true when format is a BCn block-compressed format.
bool isBlockCompressed(vk::Format format);

// Correct byte size for a staging/readback buffer covering the whole image.
// Handles BCn by aligning dimensions up to the 4×4 block boundary.
vk::DeviceSize imageDataSize(vk::Format format, vk::Extent3D extent);

} // namespace graphics::vulkan::devices

// TODO
// check overloads
//
// add more overloads for images transfers
// for more ranges, mip levels, regions, and multiple images at a time
//
// verify vk::AccessFlagBits used, add option to overwrite default values,
// especially vk::AccessFlagBits::eNone
