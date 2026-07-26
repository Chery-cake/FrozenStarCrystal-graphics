module;

#include <cstdint>
#include <stdexcept>

module graphics.vulkan.devices;

import std.compat;
import vulkan;

namespace graphics::vulkan::devices {

namespace {

// Helper: pick a queue from the device – prefer transfer, then graphics
vk::Queue selectQueue(Device &device) {
  const auto &qfi = device.getQueueFamilies();
  if (qfi.hasTransfer()) {
    return device.getTransferQueue();
  }
  if (qfi.hasGraphics()) {
    return device.getGraphicsQueue();
  }
  throw std::runtime_error("Device has no transfer or graphics queue");
}

// Helper: create a one‑time command buffer and submit it, wait for completion
void executeOneShot(Device &device, vk::Queue queue,
                    std::invocable<vk::CommandBuffer> auto &&record) {
  auto &pool = (queue == device.getTransferQueue())   ? device.getTransferPool()
               : (queue == device.getGraphicsQueue()) ? device.getGraphicsPool()
                                                      : device.getComputePool();

  pool.allocate(1);
  auto &cmd = pool.buffers.front();
  cmd.begin(vk::CommandBufferBeginInfo{
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
  record(cmd);
  cmd.end();

  vk::SubmitInfo submit;
  submit.setCommandBuffers(*cmd);
  vk::raii::Fence fence{device.getRaiiDevice(), vk::FenceCreateInfo{}};
  queue.submit(submit, *fence);
  auto result =
      device.getRaiiDevice().waitForFences(*fence, vk::True, UINT64_MAX);
  if (result != vk::Result::eSuccess) {
    throw std::runtime_error("Transfer submission failed");
  }
  pool.reset(); // reclaim command buffer
}

// Helper: memory barrier for image layout transitions
void pipelineBarrier(vk::CommandBuffer cmd, vk::Image image,
                     vk::AccessFlags srcAccess, vk::AccessFlags dstAccess,
                     vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                     vk::ImageSubresourceRange subresource) {
  vk::ImageMemoryBarrier barrier{srcAccess,
                                 dstAccess,
                                 oldLayout,
                                 newLayout,
                                 vk::QueueFamilyIgnored,
                                 vk::QueueFamilyIgnored,
                                 image,
                                 subresource};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                      vk::PipelineStageFlagBits::eAllCommands,
                      vk::DependencyFlags{}, {}, {}, barrier);
}

// create a staging buffer that is host‑visible and coherent
AllocatedBuffer createStagingBuffer(Device &device, vk::DeviceSize size) {
  BufferCreateInfo info{
      .size = size,
      .usage = vk::BufferUsageFlagBits::eTransferSrc |
               vk::BufferUsageFlagBits::eTransferDst,
      .memoryUsage = vma::MemoryUsage::eAuto,
      .flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
      .debugName = "staging"};
  return device.createBuffer(info);
}

vk::DeviceSize formatSize(vk::Format format) {
  switch (format) {
  case vk::Format::eR8G8B8A8Unorm:
  case vk::Format::eR8G8B8A8Srgb:
    return 4;
  case vk::Format::eR32G32B32A32Sfloat:
    return 16;
  default:
    throw std::runtime_error("Unsupported format");
  }
}

vk::ImageAspectFlags aspectFromFormat(vk::Format fmt) {
  if (fmt >= vk::Format::eD16Unorm && fmt <= vk::Format::eD32SfloatS8Uint) {
    return vk::ImageAspectFlagBits::eDepth |
           (fmt == vk::Format::eD24UnormS8Uint ||
                    fmt == vk::Format::eD32SfloatS8Uint
                ? vk::ImageAspectFlagBits::eStencil
                : vk::ImageAspectFlags{});
  }
  return vk::ImageAspectFlagBits::eColor;
}

} // namespace

// ===================== intra‑device implementations =====================

void transfer(Device &device, const AllocatedBuffer &src,
              vk::DeviceSize srcOffset, AllocatedBuffer &dst,
              vk::DeviceSize dstOffset, vk::DeviceSize size) {
  auto queue = selectQueue(device);
  executeOneShot(
      device, queue,
      [&srcOffset, &dstOffset, &size, &src, &dst](vk::CommandBuffer cmd) {
        vk::BufferCopy region{srcOffset, dstOffset, size};
        cmd.copyBuffer(src.getBuffer(), dst.getBuffer(), region);
      });
}

void transfer(Device &device, const AllocatedBuffer &src,
              vk::DeviceSize bufferOffset, AllocatedImage &dst,
              vk::ImageLayout dstFinalLayout) {
  auto queue = selectQueue(device);
  const auto aspect = aspectFromFormat(dst.format);
  executeOneShot(
      device, queue,
      [&src, &dst, &bufferOffset, &dstFinalLayout,
       &aspect](vk::CommandBuffer cmd) {
        vk::ImageSubresourceRange subresource{aspect, 0, 1, 0, 1};
        pipelineBarrier(cmd, dst.getImage(), vk::AccessFlagBits::eNone,
                        vk::AccessFlagBits::eTransferWrite,
                        vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eTransferDstOptimal, subresource);

        vk::BufferImageCopy region{bufferOffset,
                                   0,
                                   0,
                                   vk::ImageSubresourceLayers{aspect, 0, 0, 1},
                                   vk::Offset3D{0, 0, 0},
                                   dst.extent};
        cmd.copyBufferToImage(src.getBuffer(), dst.getImage(),
                              vk::ImageLayout::eTransferDstOptimal, region);

        pipelineBarrier(cmd, dst.getImage(), vk::AccessFlagBits::eTransferWrite,
                        vk::AccessFlagBits::eShaderRead,
                        vk::ImageLayout::eTransferDstOptimal, dstFinalLayout,
                        subresource);
      });
}

void transfer(Device &device, const AllocatedImage &src,
              vk::ImageLayout srcCurrentLayout, AllocatedBuffer &dst,
              vk::DeviceSize bufferOffset) {
  auto queue = selectQueue(device);
  const auto aspect = aspectFromFormat(src.format);
  executeOneShot(
      device, queue,
      [&src, &dst, &srcCurrentLayout, &bufferOffset,
       &aspect](vk::CommandBuffer cmd) {
        vk::ImageSubresourceRange subresource{aspect, 0, 1, 0, 1};
        // transition to transfer source
        pipelineBarrier(cmd, src.getImage(), vk::AccessFlagBits::eMemoryRead,
                        vk::AccessFlagBits::eTransferRead, srcCurrentLayout,
                        vk::ImageLayout::eTransferSrcOptimal, subresource);

        vk::BufferImageCopy region{bufferOffset,
                                   0,
                                   0,
                                   vk::ImageSubresourceLayers{aspect, 0, 0, 1},
                                   vk::Offset3D{0, 0, 0},
                                   src.extent};
        cmd.copyImageToBuffer(src.getImage(),
                              vk::ImageLayout::eTransferSrcOptimal,
                              dst.getBuffer(), region);

        // transition back
        pipelineBarrier(cmd, src.getImage(), vk::AccessFlagBits::eTransferRead,
                        vk::AccessFlagBits::eMemoryRead,
                        vk::ImageLayout::eTransferSrcOptimal, srcCurrentLayout,
                        subresource);
      });
}

void transfer(Device &device, const AllocatedImage &src,
              vk::ImageLayout srcCurrentLayout, AllocatedImage &dst,
              vk::ImageLayout dstCurrentLayout,
              vk::ImageLayout dstFinalLayout) {
  auto queue = selectQueue(device);
  const auto srcAspect = aspectFromFormat(src.format);
  const auto dstAspect = aspectFromFormat(dst.format);
  executeOneShot(
      device, queue,
      [&src, &dst, &srcCurrentLayout, &dstCurrentLayout, &dstFinalLayout,
       &srcAspect, &dstAspect](vk::CommandBuffer cmd) {
        // src -> transfer source
        vk::ImageSubresourceRange srcSubresource{srcAspect, 0, 1, 0, 1};
        pipelineBarrier(cmd, src.getImage(), vk::AccessFlagBits::eMemoryRead,
                        vk::AccessFlagBits::eTransferRead, srcCurrentLayout,
                        vk::ImageLayout::eTransferSrcOptimal, srcSubresource);
        // dst -> transfer destination
        vk::ImageSubresourceRange dstSubresource{dstAspect, 0, 1, 0, 1};
        pipelineBarrier(cmd, dst.getImage(), vk::AccessFlagBits::eMemoryWrite,
                        vk::AccessFlagBits::eTransferWrite, dstCurrentLayout,
                        vk::ImageLayout::eTransferDstOptimal, dstSubresource);

        vk::ImageCopy region{vk::ImageSubresourceLayers{srcAspect, 0, 0, 1},
                             vk::Offset3D{0, 0, 0},
                             vk::ImageSubresourceLayers{dstAspect, 0, 0, 1},
                             vk::Offset3D{0, 0, 0}, src.extent};
        cmd.copyImage(src.getImage(), vk::ImageLayout::eTransferSrcOptimal,
                      dst.getImage(), vk::ImageLayout::eTransferDstOptimal,
                      region);

        // transition back src and dst
        pipelineBarrier(cmd, src.getImage(), vk::AccessFlagBits::eTransferRead,
                        vk::AccessFlagBits::eMemoryRead,
                        vk::ImageLayout::eTransferSrcOptimal, srcCurrentLayout,
                        srcSubresource);
        pipelineBarrier(cmd, dst.getImage(), vk::AccessFlagBits::eTransferWrite,
                        vk::AccessFlagBits::eShaderRead,
                        vk::ImageLayout::eTransferDstOptimal, dstFinalLayout,
                        dstSubresource);
      });
}

// ===================== swapchain record‑only helpers =====================

void recordTransfer(vk::CommandBuffer cmd, const AllocatedBuffer &src,
                    vk::DeviceSize srcOffset, vk::Image swapchainImage,
                    vk::Extent2D imageExtent, vk::Format imageFormat,
                    vk::ImageLayout currentLayout,
                    vk::ImageLayout dstFinalLayout) {
  const auto aspect = aspectFromFormat(imageFormat);
  const vk::ImageSubresourceRange subresource{aspect, 0, 1, 0, 1};

  // 1. Transition swapchain image to transfer destination
  pipelineBarrier(cmd, swapchainImage,
                  vk::AccessFlagBits::eNone, // src access (undefined layout)
                  vk::AccessFlagBits::eTransferWrite, currentLayout,
                  vk::ImageLayout::eTransferDstOptimal, subresource);

  // 2. Copy buffer to image
  vk::BufferImageCopy region{
      srcOffset, // buffer offset
      0,
      0, // buffer row length / height (0 = tightly packed)
      vk::ImageSubresourceLayers{aspect, 0, 0, 1},           // subresource
      vk::Offset3D{0, 0, 0},                                 // image offset
      vk::Extent3D{imageExtent.width, imageExtent.height, 1} // extent
  };
  cmd.copyBufferToImage(src.getBuffer(), swapchainImage,
                        vk::ImageLayout::eTransferDstOptimal, region);

  // 3. Transition to final layout
  pipelineBarrier(cmd, swapchainImage, vk::AccessFlagBits::eTransferWrite,
                  vk::AccessFlagBits::eMemoryRead, // will be read by next stage
                  vk::ImageLayout::eTransferDstOptimal, dstFinalLayout,
                  subresource);
}

// ----------

void recordTransfer(vk::CommandBuffer cmd, vk::Image swapchainImage,
                    vk::Extent2D imageExtent, vk::Format imageFormat,
                    vk::ImageLayout currentLayout, AllocatedBuffer &dst,
                    vk::DeviceSize dstOffset, vk::ImageLayout finalLayout) {
  const auto aspect = aspectFromFormat(imageFormat);
  const vk::ImageSubresourceRange subresource{aspect, 0, 1, 0, 1};

  // 1. Transition to transfer source
  pipelineBarrier(cmd, swapchainImage, vk::AccessFlagBits::eMemoryRead,
                  vk::AccessFlagBits::eTransferRead, currentLayout,
                  vk::ImageLayout::eTransferSrcOptimal, subresource);

  // 2. Copy image to buffer
  vk::BufferImageCopy region{
      dstOffset,
      0,
      0,
      vk::ImageSubresourceLayers{aspect, 0, 0, 1},
      vk::Offset3D{0, 0, 0},
      vk::Extent3D{imageExtent.width, imageExtent.height, 1}};
  cmd.copyImageToBuffer(swapchainImage, vk::ImageLayout::eTransferSrcOptimal,
                        dst.getBuffer(), region);

  // 3. Transition back to final layout
  pipelineBarrier(cmd, swapchainImage, vk::AccessFlagBits::eTransferRead,
                  vk::AccessFlagBits::eMemoryRead,
                  vk::ImageLayout::eTransferSrcOptimal, finalLayout,
                  subresource);
}

// ----------

void recordTransfer(vk::CommandBuffer cmd, const AllocatedImage &src,
                    vk::ImageLayout srcCurrentLayout, vk::Image swapchainImage,
                    vk::Extent2D imageExtent, vk::Format imageFormat,
                    vk::ImageLayout currentLayout,
                    vk::ImageLayout dstFinalLayout) {
  const auto srcAspect = aspectFromFormat(src.format);
  const auto dstAspect = aspectFromFormat(imageFormat);

  // Source: transition to transfer source
  pipelineBarrier(cmd, src.getImage(), vk::AccessFlagBits::eMemoryRead,
                  vk::AccessFlagBits::eTransferRead, srcCurrentLayout,
                  vk::ImageLayout::eTransferSrcOptimal,
                  vk::ImageSubresourceRange{srcAspect, 0, 1, 0, 1});

  // Destination (swapchain): transition to transfer destination
  pipelineBarrier(cmd, swapchainImage, vk::AccessFlagBits::eMemoryRead,
                  vk::AccessFlagBits::eTransferWrite, currentLayout,
                  vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageSubresourceRange{dstAspect, 0, 1, 0, 1});

  vk::ImageCopy region{
      vk::ImageSubresourceLayers{srcAspect, 0, 0, 1}, vk::Offset3D{0, 0, 0},
      vk::ImageSubresourceLayers{dstAspect, 0, 0, 1}, vk::Offset3D{0, 0, 0},
      vk::Extent3D{imageExtent.width, imageExtent.height, 1}};
  cmd.copyImage(src.getImage(), vk::ImageLayout::eTransferSrcOptimal,
                swapchainImage, vk::ImageLayout::eTransferDstOptimal, region);

  // Transition both back
  pipelineBarrier(cmd, src.getImage(), vk::AccessFlagBits::eTransferRead,
                  vk::AccessFlagBits::eMemoryRead,
                  vk::ImageLayout::eTransferSrcOptimal,
                  srcCurrentLayout, // restore source's original layout
                  vk::ImageSubresourceRange{srcAspect, 0, 1, 0, 1});

  pipelineBarrier(cmd, swapchainImage, vk::AccessFlagBits::eTransferWrite,
                  vk::AccessFlagBits::eMemoryRead,
                  vk::ImageLayout::eTransferDstOptimal, dstFinalLayout,
                  vk::ImageSubresourceRange{dstAspect, 0, 1, 0, 1});
}

// ----------

void recordTransfer(vk::CommandBuffer cmd, vk::Image swapchainImage,
                    vk::Extent2D imageExtent, vk::Format imageFormat,
                    vk::ImageLayout currentLayout, AllocatedImage &dst,
                    vk::ImageLayout dstCurrentLayout,
                    vk::ImageLayout dstFinalLayout) {
  const auto srcAspect = aspectFromFormat(imageFormat);
  const auto dstAspect = aspectFromFormat(dst.format);

  // Source: transition to transfer source
  pipelineBarrier(cmd, swapchainImage, vk::AccessFlagBits::eMemoryRead,
                  vk::AccessFlagBits::eTransferRead, currentLayout,
                  vk::ImageLayout::eTransferSrcOptimal,
                  vk::ImageSubresourceRange{srcAspect, 0, 1, 0, 1});

  // Destination: transition to transfer destination
  pipelineBarrier(cmd, dst.getImage(), vk::AccessFlagBits::eMemoryWrite,
                  vk::AccessFlagBits::eTransferWrite, dstCurrentLayout,
                  vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageSubresourceRange{dstAspect, 0, 1, 0, 1});

  // Copy the whole image
  vk::ImageCopy region{
      vk::ImageSubresourceLayers{srcAspect, 0, 0, 1}, vk::Offset3D{0, 0, 0},
      vk::ImageSubresourceLayers{dstAspect, 0, 0, 1}, vk::Offset3D{0, 0, 0},
      vk::Extent3D{imageExtent.width, imageExtent.height, 1}};
  cmd.copyImage(swapchainImage, vk::ImageLayout::eTransferSrcOptimal,
                dst.getImage(), vk::ImageLayout::eTransferDstOptimal, region);

  // Transition both back
  pipelineBarrier(
      cmd, swapchainImage, vk::AccessFlagBits::eTransferRead,
      vk::AccessFlagBits::eMemoryRead, vk::ImageLayout::eTransferSrcOptimal,
      vk::ImageLayout::ePresentSrcKHR, // typical final for swapchain
      vk::ImageSubresourceRange{srcAspect, 0, 1, 0, 1});

  pipelineBarrier(cmd, dst.getImage(), vk::AccessFlagBits::eTransferWrite,
                  vk::AccessFlagBits::eShaderRead,
                  vk::ImageLayout::eTransferDstOptimal, dstFinalLayout,
                  vk::ImageSubresourceRange{dstAspect, 0, 1, 0, 1});
}

// ===================== inter‑device implementations =====================
// All inter‑device copies go through a staging buffer on the source device,
// then a memcpy to a staging buffer on the destination device, and finally
// a intra‑device copy to the target resource.

void transfer(Device &srcDevice, const AllocatedBuffer &src,
              vk::DeviceSize srcOffset, Device &dstDevice, AllocatedBuffer &dst,
              vk::DeviceSize dstOffset, vk::DeviceSize size) {
  // If we can directly map both buffers (host‑visible), just memcpy.
  // But they might be device‑local, so we stage.
  auto srcStaging = createStagingBuffer(srcDevice, size);
  // copy src -> staging (src device)
  transfer(srcDevice, src, srcOffset, srcStaging, 0, size);
  // read staging to host
  void *srcData = srcStaging.map();
  srcStaging.invalidate(0, size);

  // destination staging
  auto dstStaging = createStagingBuffer(dstDevice, size);
  void *dstData = dstStaging.map();
  std::memcpy(dstData, srcData, size);
  dstStaging.flush(0, size);
  dstStaging.unmap();
  srcStaging.unmap();

  // copy staging -> dst (dst device)
  transfer(dstDevice, dstStaging, 0, dst, dstOffset, size);
}

void transfer(Device &srcDevice, const AllocatedBuffer &src,
              vk::DeviceSize srcOffset, Device &dstDevice, AllocatedImage &dst,
              vk::ImageLayout dstFinalLayout) {
  // Use a staging buffer on the source device
  vk::DeviceSize imageSize =
      static_cast<vk::DeviceSize>(dst.extent.width * dst.extent.height) *
      formatSize(dst.format);
  auto staging = createStagingBuffer(srcDevice, imageSize);
  // copy src buffer -> staging (intra src device)
  transfer(srcDevice, src, srcOffset, staging, 0, imageSize);
  // copy from staging to dst image via dstDevice intra‑device transfer
  transfer(dstDevice, staging, 0, dst, dstFinalLayout);
}

void transfer(Device &srcDevice, const AllocatedImage &src,
              vk::ImageLayout srcCurrentLayout, Device &dstDevice,
              AllocatedBuffer &dst, vk::DeviceSize dstOffset) {
  vk::DeviceSize imageSize =
      static_cast<vk::DeviceSize>(src.extent.width * src.extent.height) *
      formatSize(src.format);
  auto staging = createStagingBuffer(srcDevice, imageSize);
  // intra src device: image -> staging buffer
  transfer(srcDevice, src, srcCurrentLayout, staging, 0);
  // copy staging -> dst buffer via dstDevice intra‑device
  transfer(dstDevice, staging, 0, dst, dstOffset, imageSize);
}

void transfer(Device &srcDevice, const AllocatedImage &src,
              vk::ImageLayout srcCurrentLayout, Device &dstDevice,
              AllocatedImage &dst, vk::ImageLayout dstFinalLayout) {
  vk::DeviceSize imageSize =
      static_cast<vk::DeviceSize>(src.extent.width * src.extent.height) *
      formatSize(src.format);
  auto staging = createStagingBuffer(srcDevice, imageSize);
  transfer(srcDevice, src, srcCurrentLayout, staging, 0);
  transfer(dstDevice, staging, 0, dst, dstFinalLayout);
}

} // namespace graphics::vulkan::devices
