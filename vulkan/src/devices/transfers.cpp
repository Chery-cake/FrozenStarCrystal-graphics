module;

#include <cstdint>
#include <stdexcept>

module graphics.vulkan.devices;

import std.compat;
import vulkan;

namespace graphics::vulkan::devices {

namespace {

vk::Queue selectQueue(Device &device, uint32_t &family) {
  const auto &qfi = device.getQueueFamilies();
  if (qfi.hasTransfer()) {
    family = qfi.transferQueue.value();
    return device.getTransferQueue();
  }
  if (qfi.hasGraphics()) {
    family = qfi.graphicsQueue.value();
    return device.getGraphicsQueue();
  }
  throw std::runtime_error("Device has no transfer or graphics queue");
}

void pipelineBarrier(
    vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout, vk::ImageSubresourceRange subresource,
    vk::PipelineStageFlags2 srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
    vk::PipelineStageFlags2 dstStageMask = vk::PipelineStageFlagBits2::eAllTransfer,
    vk::AccessFlags2 srcAccessMask = {}, vk::AccessFlags2 dstAccessMask = {}) {
  vk::ImageMemoryBarrier2 barrier{};
  barrier.srcStageMask = srcStageMask;
  barrier.dstStageMask = dstStageMask;
  barrier.srcAccessMask = srcAccessMask;
  barrier.dstAccessMask = dstAccessMask;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  barrier.image = image;
  barrier.subresourceRange = subresource;

  vk::DependencyInfo depInfo{};
  depInfo.setImageMemoryBarriers(barrier);
  cmd.pipelineBarrier2(depInfo);
}

AllocatedBuffer createStagingBuffer(Device &device, vk::DeviceSize size) {
  BufferCreateInfo info{
      .size = size,
      .usage = vk::BufferUsageFlagBits::eTransferSrc |
               vk::BufferUsageFlagBits::eTransferDst,
      .access = BufferCreateInfo::Access::eStagingUpload,
      .debugName = "staging"};
  return device.createBuffer(info);
}

vk::DeviceSize formatSize(vk::Format format,
                          std::optional<vk::DeviceSize> fallback = std::nullopt) {
  switch (format) {
  case vk::Format::eR8Unorm:
  case vk::Format::eR8Srgb:
    return 1;
  case vk::Format::eR16Sfloat:
    return 2;
  case vk::Format::eR32Sfloat:
    return 4;

  case vk::Format::eR8G8Unorm:
    return 2;
  case vk::Format::eR16G16Sfloat:
    return 4;
  case vk::Format::eR32G32Sfloat:
    return 8;

  case vk::Format::eR8G8B8Unorm:
  case vk::Format::eR8G8B8Srgb:
  case vk::Format::eB8G8R8Unorm:
  case vk::Format::eB8G8R8Srgb:
    return 3;

  case vk::Format::eR8G8B8A8Unorm:
  case vk::Format::eR8G8B8A8Srgb:
  case vk::Format::eB8G8R8A8Unorm:
  case vk::Format::eB8G8R8A8Srgb:
  case vk::Format::eA8B8G8R8UnormPack32:
  case vk::Format::eA2R10G10B10UnormPack32:
  case vk::Format::eA2B10G10R10UnormPack32:
  case vk::Format::eB10G11R11UfloatPack32:
    return 4;
  case vk::Format::eR16G16B16A16Sfloat:
    return 8;
  case vk::Format::eR32G32B32A32Sfloat:
    return 16;

  case vk::Format::eD16Unorm:
    return 2;
  case vk::Format::eD32Sfloat:
  case vk::Format::eD24UnormS8Uint:
    return 4;
  case vk::Format::eD32SfloatS8Uint:
    return 5;

  // Planar formats: returned size is per-plane byte estimate.
  case vk::Format::eG8B8R82Plane420Unorm:
  case vk::Format::eG8B8R83Plane420Unorm:
    return 1;

  // Block-compressed formats: returned size is bytes per 4×4 texel block.
  // Use imageDataSize() for correct total buffer size calculations.
  case vk::Format::eBc1RgbUnormBlock:
  case vk::Format::eBc1RgbSrgbBlock:
  case vk::Format::eBc1RgbaUnormBlock:
  case vk::Format::eBc1RgbaSrgbBlock:
  case vk::Format::eBc4UnormBlock:
  case vk::Format::eBc4SnormBlock:
    return 8; // 8 bytes per 4×4 block
  case vk::Format::eBc2UnormBlock:
  case vk::Format::eBc2SrgbBlock:
  case vk::Format::eBc3UnormBlock:
  case vk::Format::eBc3SrgbBlock:
  case vk::Format::eBc5UnormBlock:
  case vk::Format::eBc5SnormBlock:
  case vk::Format::eBc6HUfloatBlock:
  case vk::Format::eBc6HSfloatBlock:
  case vk::Format::eBc7UnormBlock:
  case vk::Format::eBc7SrgbBlock:
    return 16; // 16 bytes per 4×4 block

  default:
    if (fallback.has_value()) {
      return fallback.value();
    }
    throw std::runtime_error("Unsupported format");
  }
}

bool isBlockCompressed(vk::Format fmt) {
  switch (fmt) {
  case vk::Format::eBc1RgbUnormBlock:
  case vk::Format::eBc1RgbSrgbBlock:
  case vk::Format::eBc1RgbaUnormBlock:
  case vk::Format::eBc1RgbaSrgbBlock:
  case vk::Format::eBc2UnormBlock:
  case vk::Format::eBc2SrgbBlock:
  case vk::Format::eBc3UnormBlock:
  case vk::Format::eBc3SrgbBlock:
  case vk::Format::eBc4UnormBlock:
  case vk::Format::eBc4SnormBlock:
  case vk::Format::eBc5UnormBlock:
  case vk::Format::eBc5SnormBlock:
  case vk::Format::eBc6HUfloatBlock:
  case vk::Format::eBc6HSfloatBlock:
  case vk::Format::eBc7UnormBlock:
  case vk::Format::eBc7SrgbBlock:
    return true;
  default:
    return false;
  }
}

vk::DeviceSize imageDataSize(vk::Format format, vk::Extent3D extent) {
  if (isBlockCompressed(format)) {
    // BCn formats pack 4×4 texel blocks; align dimensions up to block boundary
    const vk::DeviceSize blocksX = (extent.width  + 3u) / 4u;
    const vk::DeviceSize blocksY = (extent.height + 3u) / 4u;
    return blocksX * blocksY * extent.depth * formatSize(format);
  }
  return static_cast<vk::DeviceSize>(extent.width) *
         static_cast<vk::DeviceSize>(extent.height) *
         static_cast<vk::DeviceSize>(extent.depth) * formatSize(format);
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

void transfer(Device &device, const AllocatedBuffer &src,
              vk::DeviceSize srcOffset, AllocatedBuffer &dst,
              vk::DeviceSize dstOffset, vk::DeviceSize size) {
  uint32_t family = 0;
  auto queue = selectQueue(device, family);
  device.submitOneShot(queue, family, [&srcOffset, &dstOffset, &size, &src,
                                       &dst](vk::CommandBuffer cmd) {
    vk::BufferCopy region{srcOffset, dstOffset, size};
    cmd.copyBuffer(src.getBuffer(), dst.getBuffer(), region);
  });
}

void transfer(Device &device, const AllocatedBuffer &src,
              vk::DeviceSize bufferOffset, AllocatedImage &dst,
              vk::ImageLayout dstFinalLayout) {
  uint32_t family = 0;
  auto queue = selectQueue(device, family);
  const auto aspect = aspectFromFormat(dst.format);
  const auto srcLayout = dst.currentLayout;
  device.submitOneShot(queue, family, [&src, &dst, &bufferOffset, &dstFinalLayout,
                                       &aspect, srcLayout](vk::CommandBuffer cmd) {
    vk::ImageSubresourceRange subresource{aspect, 0, 1, 0, 1};
    pipelineBarrier(cmd, dst.getImage(), srcLayout,
                    vk::ImageLayout::eTransferDstOptimal, subresource, {},
                    vk::PipelineStageFlagBits2::eAllTransfer, {},
                    vk::AccessFlagBits2::eTransferWrite);

    vk::BufferImageCopy region{bufferOffset,
                               0,
                               0,
                               vk::ImageSubresourceLayers{aspect, 0, 0, 1},
                               vk::Offset3D{0, 0, 0},
                               dst.extent};
    cmd.copyBufferToImage(src.getBuffer(), dst.getImage(),
                          vk::ImageLayout::eTransferDstOptimal, region);

    pipelineBarrier(cmd, dst.getImage(), vk::ImageLayout::eTransferDstOptimal,
                    dstFinalLayout, subresource,
                    vk::PipelineStageFlagBits2::eAllTransfer,
                    vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::AccessFlagBits2::eMemoryRead);
  });
  dst.currentLayout = dstFinalLayout;
}

void transfer(Device &device, const AllocatedImage &src, AllocatedBuffer &dst,
              vk::DeviceSize bufferOffset) {
  uint32_t family = 0;
  auto queue = selectQueue(device, family);
  const auto aspect = aspectFromFormat(src.format);
  const auto srcLayout = src.currentLayout;
  device.submitOneShot(queue, family, [&src, &dst, &bufferOffset, &aspect,
                                       srcLayout](vk::CommandBuffer cmd) {
    vk::ImageSubresourceRange subresource{aspect, 0, 1, 0, 1};
    pipelineBarrier(cmd, src.getImage(), srcLayout,
                    vk::ImageLayout::eTransferSrcOptimal, subresource, {},
                    vk::PipelineStageFlagBits2::eAllTransfer, {},
                    vk::AccessFlagBits2::eTransferRead);

    vk::BufferImageCopy region{bufferOffset,
                               0,
                               0,
                               vk::ImageSubresourceLayers{aspect, 0, 0, 1},
                               vk::Offset3D{0, 0, 0},
                               src.extent};
    cmd.copyImageToBuffer(src.getImage(), vk::ImageLayout::eTransferSrcOptimal,
                          dst.getBuffer(), region);

    pipelineBarrier(cmd, src.getImage(), vk::ImageLayout::eTransferSrcOptimal,
                    srcLayout, subresource,
                    vk::PipelineStageFlagBits2::eAllTransfer,
                    vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eTransferRead,
                    vk::AccessFlagBits2::eMemoryRead);
  });
}

void transfer(Device &device, const AllocatedImage &src, AllocatedImage &dst,
              vk::ImageLayout dstFinalLayout) {
  uint32_t family = 0;
  auto queue = selectQueue(device, family);
  const auto srcAspect = aspectFromFormat(src.format);
  const auto dstAspect = aspectFromFormat(dst.format);
  const auto srcLayout = src.currentLayout;
  const auto dstLayout = dst.currentLayout;
  device.submitOneShot(
      queue, family, [&src, &dst, srcLayout, dstLayout, &dstFinalLayout,
                      &srcAspect, &dstAspect](vk::CommandBuffer cmd) {
        vk::ImageSubresourceRange srcSubresource{srcAspect, 0, 1, 0, 1};
        pipelineBarrier(cmd, src.getImage(), srcLayout,
                        vk::ImageLayout::eTransferSrcOptimal, srcSubresource, {},
                        vk::PipelineStageFlagBits2::eAllTransfer, {},
                        vk::AccessFlagBits2::eTransferRead);

        vk::ImageSubresourceRange dstSubresource{dstAspect, 0, 1, 0, 1};
        pipelineBarrier(cmd, dst.getImage(), dstLayout,
                        vk::ImageLayout::eTransferDstOptimal, dstSubresource, {},
                        vk::PipelineStageFlagBits2::eAllTransfer, {},
                        vk::AccessFlagBits2::eTransferWrite);

        vk::ImageCopy region{vk::ImageSubresourceLayers{srcAspect, 0, 0, 1},
                             vk::Offset3D{0, 0, 0},
                             vk::ImageSubresourceLayers{dstAspect, 0, 0, 1},
                             vk::Offset3D{0, 0, 0},
                             src.extent};
        cmd.copyImage(src.getImage(), vk::ImageLayout::eTransferSrcOptimal,
                      dst.getImage(), vk::ImageLayout::eTransferDstOptimal,
                      region);

        pipelineBarrier(cmd, src.getImage(), vk::ImageLayout::eTransferSrcOptimal,
                        srcLayout, srcSubresource,
                        vk::PipelineStageFlagBits2::eAllTransfer,
                        vk::PipelineStageFlagBits2::eAllCommands,
                        vk::AccessFlagBits2::eTransferRead,
                        vk::AccessFlagBits2::eMemoryRead);
        pipelineBarrier(cmd, dst.getImage(), vk::ImageLayout::eTransferDstOptimal,
                        dstFinalLayout, dstSubresource,
                        vk::PipelineStageFlagBits2::eAllTransfer,
                        vk::PipelineStageFlagBits2::eAllCommands,
                        vk::AccessFlagBits2::eTransferWrite,
                        vk::AccessFlagBits2::eMemoryRead);
      });
  dst.currentLayout = dstFinalLayout;
}

void recordTransfer(vk::CommandBuffer cmd, const AllocatedBuffer &src,
                    vk::DeviceSize srcOffset, vk::Image swapchainImage,
                    vk::Extent2D imageExtent, vk::Format imageFormat,
                    vk::ImageLayout currentLayout,
                    vk::ImageLayout dstFinalLayout) {
  const auto aspect = aspectFromFormat(imageFormat);
  const vk::ImageSubresourceRange subresource{aspect, 0, 1, 0, 1};

  pipelineBarrier(cmd, swapchainImage, currentLayout,
                  vk::ImageLayout::eTransferDstOptimal, subresource, {},
                  vk::PipelineStageFlagBits2::eAllTransfer, {},
                  vk::AccessFlagBits2::eTransferWrite);

  vk::BufferImageCopy region{srcOffset,
                             0,
                             0,
                             vk::ImageSubresourceLayers{aspect, 0, 0, 1},
                             vk::Offset3D{0, 0, 0},
                             vk::Extent3D{imageExtent.width, imageExtent.height, 1}};
  cmd.copyBufferToImage(src.getBuffer(), swapchainImage,
                        vk::ImageLayout::eTransferDstOptimal, region);

  pipelineBarrier(cmd, swapchainImage, vk::ImageLayout::eTransferDstOptimal,
                  dstFinalLayout, subresource,
                  vk::PipelineStageFlagBits2::eAllTransfer,
                  vk::PipelineStageFlagBits2::eAllCommands,
                  vk::AccessFlagBits2::eTransferWrite,
                  vk::AccessFlagBits2::eMemoryRead);
}

void recordTransfer(vk::CommandBuffer cmd, vk::Image swapchainImage,
                    vk::Extent2D imageExtent, vk::Format imageFormat,
                    vk::ImageLayout currentLayout, AllocatedBuffer &dst,
                    vk::DeviceSize dstOffset, vk::ImageLayout finalLayout) {
  const auto aspect = aspectFromFormat(imageFormat);
  const vk::ImageSubresourceRange subresource{aspect, 0, 1, 0, 1};

  pipelineBarrier(cmd, swapchainImage, currentLayout,
                  vk::ImageLayout::eTransferSrcOptimal, subresource, {},
                  vk::PipelineStageFlagBits2::eAllTransfer, {},
                  vk::AccessFlagBits2::eTransferRead);

  vk::BufferImageCopy region{dstOffset,
                             0,
                             0,
                             vk::ImageSubresourceLayers{aspect, 0, 0, 1},
                             vk::Offset3D{0, 0, 0},
                             vk::Extent3D{imageExtent.width, imageExtent.height, 1}};
  cmd.copyImageToBuffer(swapchainImage, vk::ImageLayout::eTransferSrcOptimal,
                        dst.getBuffer(), region);

  pipelineBarrier(cmd, swapchainImage, vk::ImageLayout::eTransferSrcOptimal,
                  finalLayout, subresource,
                  vk::PipelineStageFlagBits2::eAllTransfer,
                  vk::PipelineStageFlagBits2::eAllCommands,
                  vk::AccessFlagBits2::eTransferRead,
                  vk::AccessFlagBits2::eMemoryRead);
}

void recordTransfer(vk::CommandBuffer cmd, const AllocatedImage &src,
                    vk::ImageLayout srcCurrentLayout, vk::Image swapchainImage,
                    vk::Extent2D imageExtent, vk::Format imageFormat,
                    vk::ImageLayout currentLayout,
                    vk::ImageLayout dstFinalLayout) {
  const auto srcAspect = aspectFromFormat(src.format);
  const auto dstAspect = aspectFromFormat(imageFormat);

  pipelineBarrier(cmd, src.getImage(), srcCurrentLayout,
                  vk::ImageLayout::eTransferSrcOptimal,
                  vk::ImageSubresourceRange{srcAspect, 0, 1, 0, 1}, {},
                  vk::PipelineStageFlagBits2::eAllTransfer, {},
                  vk::AccessFlagBits2::eTransferRead);

  pipelineBarrier(cmd, swapchainImage, currentLayout,
                  vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageSubresourceRange{dstAspect, 0, 1, 0, 1}, {},
                  vk::PipelineStageFlagBits2::eAllTransfer, {},
                  vk::AccessFlagBits2::eTransferWrite);

  vk::ImageCopy region{
      vk::ImageSubresourceLayers{srcAspect, 0, 0, 1}, vk::Offset3D{0, 0, 0},
      vk::ImageSubresourceLayers{dstAspect, 0, 0, 1}, vk::Offset3D{0, 0, 0},
      vk::Extent3D{imageExtent.width, imageExtent.height, 1}};
  cmd.copyImage(src.getImage(), vk::ImageLayout::eTransferSrcOptimal,
                swapchainImage, vk::ImageLayout::eTransferDstOptimal, region);

  pipelineBarrier(cmd, src.getImage(), vk::ImageLayout::eTransferSrcOptimal,
                  srcCurrentLayout, vk::ImageSubresourceRange{srcAspect, 0, 1, 0, 1},
                  vk::PipelineStageFlagBits2::eAllTransfer,
                  vk::PipelineStageFlagBits2::eAllCommands,
                  vk::AccessFlagBits2::eTransferRead,
                  vk::AccessFlagBits2::eMemoryRead);

  pipelineBarrier(cmd, swapchainImage, vk::ImageLayout::eTransferDstOptimal,
                  dstFinalLayout, vk::ImageSubresourceRange{dstAspect, 0, 1, 0, 1},
                  vk::PipelineStageFlagBits2::eAllTransfer,
                  vk::PipelineStageFlagBits2::eAllCommands,
                  vk::AccessFlagBits2::eTransferWrite,
                  vk::AccessFlagBits2::eMemoryRead);
}

void recordTransfer(vk::CommandBuffer cmd, vk::Image swapchainImage,
                    vk::Extent2D imageExtent, vk::Format imageFormat,
                    vk::ImageLayout currentLayout, AllocatedImage &dst,
                    vk::ImageLayout dstCurrentLayout,
                    vk::ImageLayout dstFinalLayout) {
  const auto srcAspect = aspectFromFormat(imageFormat);
  const auto dstAspect = aspectFromFormat(dst.format);

  pipelineBarrier(cmd, swapchainImage, currentLayout,
                  vk::ImageLayout::eTransferSrcOptimal,
                  vk::ImageSubresourceRange{srcAspect, 0, 1, 0, 1}, {},
                  vk::PipelineStageFlagBits2::eAllTransfer, {},
                  vk::AccessFlagBits2::eTransferRead);

  pipelineBarrier(cmd, dst.getImage(), dstCurrentLayout,
                  vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageSubresourceRange{dstAspect, 0, 1, 0, 1}, {},
                  vk::PipelineStageFlagBits2::eAllTransfer, {},
                  vk::AccessFlagBits2::eTransferWrite);

  vk::ImageCopy region{
      vk::ImageSubresourceLayers{srcAspect, 0, 0, 1}, vk::Offset3D{0, 0, 0},
      vk::ImageSubresourceLayers{dstAspect, 0, 0, 1}, vk::Offset3D{0, 0, 0},
      vk::Extent3D{imageExtent.width, imageExtent.height, 1}};
  cmd.copyImage(swapchainImage, vk::ImageLayout::eTransferSrcOptimal,
                dst.getImage(), vk::ImageLayout::eTransferDstOptimal, region);

  pipelineBarrier(cmd, swapchainImage, vk::ImageLayout::eTransferSrcOptimal,
                  vk::ImageLayout::ePresentSrcKHR,
                  vk::ImageSubresourceRange{srcAspect, 0, 1, 0, 1},
                  vk::PipelineStageFlagBits2::eAllTransfer,
                  vk::PipelineStageFlagBits2::eAllCommands,
                  vk::AccessFlagBits2::eTransferRead,
                  vk::AccessFlagBits2::eMemoryRead);

  pipelineBarrier(cmd, dst.getImage(), vk::ImageLayout::eTransferDstOptimal,
                  dstFinalLayout, vk::ImageSubresourceRange{dstAspect, 0, 1, 0, 1},
                  vk::PipelineStageFlagBits2::eAllTransfer,
                  vk::PipelineStageFlagBits2::eAllCommands,
                  vk::AccessFlagBits2::eTransferWrite,
                  vk::AccessFlagBits2::eMemoryRead);
}

void transfer(Device &srcDevice, const AllocatedBuffer &src,
              vk::DeviceSize srcOffset, Device &dstDevice, AllocatedBuffer &dst,
              vk::DeviceSize dstOffset, vk::DeviceSize size) {
  auto srcStaging = createStagingBuffer(srcDevice, size);
  transfer(srcDevice, src, srcOffset, srcStaging, 0, size);
  void *srcData = srcStaging.map();
  srcStaging.invalidate(0, size);

  auto dstStaging = createStagingBuffer(dstDevice, size);
  void *dstData = dstStaging.map();
  std::memcpy(dstData, srcData, size);
  dstStaging.flush(0, size);
  dstStaging.unmap();
  srcStaging.unmap();

  transfer(dstDevice, dstStaging, 0, dst, dstOffset, size);
}

void transfer(Device &srcDevice, const AllocatedBuffer &src,
              vk::DeviceSize srcOffset, Device &dstDevice, AllocatedImage &dst,
              vk::ImageLayout dstFinalLayout) {
  vk::DeviceSize imageSize = imageDataSize(dst.format, dst.extent);
  auto staging = createStagingBuffer(srcDevice, imageSize);
  transfer(srcDevice, src, srcOffset, staging, 0, imageSize);
  transfer(dstDevice, staging, 0, dst, dstFinalLayout);
}

void transfer(Device &srcDevice, const AllocatedImage &src, Device &dstDevice,
              AllocatedBuffer &dst, vk::DeviceSize dstOffset) {
  vk::DeviceSize imageSize = imageDataSize(src.format, src.extent);
  auto staging = createStagingBuffer(srcDevice, imageSize);
  transfer(srcDevice, src, staging, 0);
  transfer(dstDevice, staging, 0, dst, dstOffset, imageSize);
}

void transfer(Device &srcDevice, const AllocatedImage &src, Device &dstDevice,
              AllocatedImage &dst, vk::ImageLayout dstFinalLayout) {
  vk::DeviceSize imageSize = imageDataSize(src.format, src.extent);
  auto staging = createStagingBuffer(srcDevice, imageSize);
  transfer(srcDevice, src, staging, 0);
  transfer(dstDevice, staging, 0, dst, dstFinalLayout);
}

// ---------- async variants ----------

std::future<void> transferAsync(Device &device, const AllocatedBuffer &src,
                                vk::DeviceSize srcOffset, AllocatedBuffer &dst,
                                vk::DeviceSize dstOffset, vk::DeviceSize size) {
  return device.submit([&device, &src, srcOffset, &dst, dstOffset, size]() {
    transfer(device, src, srcOffset, dst, dstOffset, size);
  });
}

std::future<void> transferAsync(Device &device, const AllocatedBuffer &src,
                                vk::DeviceSize bufferOffset, AllocatedImage &dst,
                                vk::ImageLayout dstFinalLayout) {
  return device.submit([&device, &src, bufferOffset, &dst, dstFinalLayout]() {
    transfer(device, src, bufferOffset, dst, dstFinalLayout);
  });
}

std::future<void> transferAsync(Device &device, const AllocatedImage &src,
                                AllocatedBuffer &dst,
                                vk::DeviceSize bufferOffset) {
  return device.submit([&device, &src, &dst, bufferOffset]() {
    transfer(device, src, dst, bufferOffset);
  });
}

std::future<void> transferAsync(Device &device, const AllocatedImage &src,
                                AllocatedImage &dst,
                                vk::ImageLayout dstFinalLayout) {
  return device.submit([&device, &src, &dst, dstFinalLayout]() {
    transfer(device, src, dst, dstFinalLayout);
  });
}

std::future<void> transferAsync(Device &srcDevice, const AllocatedBuffer &src,
                                vk::DeviceSize srcOffset, Device &dstDevice,
                                AllocatedBuffer &dst, vk::DeviceSize dstOffset,
                                vk::DeviceSize size) {
  return srcDevice.submit(
      [&srcDevice, &src, srcOffset, &dstDevice, &dst, dstOffset, size]() {
        transfer(srcDevice, src, srcOffset, dstDevice, dst, dstOffset, size);
      });
}

std::future<void> transferAsync(Device &srcDevice, const AllocatedBuffer &src,
                                vk::DeviceSize srcOffset, Device &dstDevice,
                                AllocatedImage &dst,
                                vk::ImageLayout dstFinalLayout) {
  return srcDevice.submit(
      [&srcDevice, &src, srcOffset, &dstDevice, &dst, dstFinalLayout]() {
        transfer(srcDevice, src, srcOffset, dstDevice, dst, dstFinalLayout);
      });
}

std::future<void> transferAsync(Device &srcDevice, const AllocatedImage &src,
                                Device &dstDevice, AllocatedBuffer &dst,
                                vk::DeviceSize dstOffset) {
  return srcDevice.submit(
      [&srcDevice, &src, &dstDevice, &dst, dstOffset]() {
        transfer(srcDevice, src, dstDevice, dst, dstOffset);
      });
}

std::future<void> transferAsync(Device &srcDevice, const AllocatedImage &src,
                                Device &dstDevice, AllocatedImage &dst,
                                vk::ImageLayout dstFinalLayout) {
  return srcDevice.submit(
      [&srcDevice, &src, &dstDevice, &dst, dstFinalLayout]() {
        transfer(srcDevice, src, dstDevice, dst, dstFinalLayout);
      });
}

std::future<void> transferAsync(Device &device,
                                std::shared_ptr<AllocatedBuffer> src,
                                vk::DeviceSize srcOffset,
                                std::shared_ptr<AllocatedBuffer> dst,
                                vk::DeviceSize dstOffset,
                                vk::DeviceSize size) {
  return device.submit([src = std::move(src), srcOffset, dst = std::move(dst),
                        dstOffset, size, &device]() {
    transfer(device, *src, srcOffset, *dst, dstOffset, size);
  });
}

std::future<void> transferAsync(Device &device,
                                std::shared_ptr<AllocatedBuffer> src,
                                vk::DeviceSize bufferOffset,
                                std::shared_ptr<AllocatedImage> dst,
                                vk::ImageLayout dstFinalLayout) {
  return device.submit([src = std::move(src), bufferOffset, dst = std::move(dst),
                        dstFinalLayout, &device]() {
    transfer(device, *src, bufferOffset, *dst, dstFinalLayout);
  });
}

std::future<void> transferAsync(Device &device,
                                std::shared_ptr<AllocatedImage> src,
                                std::shared_ptr<AllocatedBuffer> dst,
                                vk::DeviceSize bufferOffset) {
  return device.submit([src = std::move(src), dst = std::move(dst),
                        bufferOffset, &device]() {
    transfer(device, *src, *dst, bufferOffset);
  });
}

std::future<void> transferAsync(Device &device,
                                std::shared_ptr<AllocatedImage> src,
                                std::shared_ptr<AllocatedImage> dst,
                                vk::ImageLayout dstFinalLayout) {
  return device.submit([src = std::move(src), dst = std::move(dst),
                        dstFinalLayout, &device]() {
    transfer(device, *src, *dst, dstFinalLayout);
  });
}

std::future<void> transferAsync(std::shared_ptr<Device> srcDevice,
                                std::shared_ptr<AllocatedBuffer> src,
                                vk::DeviceSize srcOffset,
                                std::shared_ptr<Device> dstDevice,
                                std::shared_ptr<AllocatedBuffer> dst,
                                vk::DeviceSize dstOffset,
                                vk::DeviceSize size) {
  return srcDevice->submit([srcDevice = std::move(srcDevice), src = std::move(src),
                            srcOffset, dstDevice = std::move(dstDevice),
                            dst = std::move(dst), dstOffset, size]() {
    transfer(*srcDevice, *src, srcOffset, *dstDevice, *dst, dstOffset, size);
  });
}

std::future<void> transferAsync(std::shared_ptr<Device> srcDevice,
                                std::shared_ptr<AllocatedBuffer> src,
                                vk::DeviceSize srcOffset,
                                std::shared_ptr<Device> dstDevice,
                                std::shared_ptr<AllocatedImage> dst,
                                vk::ImageLayout dstFinalLayout) {
  return srcDevice->submit([srcDevice = std::move(srcDevice), src = std::move(src),
                            srcOffset, dstDevice = std::move(dstDevice),
                            dst = std::move(dst), dstFinalLayout]() {
    transfer(*srcDevice, *src, srcOffset, *dstDevice, *dst, dstFinalLayout);
  });
}

std::future<void> transferAsync(std::shared_ptr<Device> srcDevice,
                                std::shared_ptr<AllocatedImage> src,
                                std::shared_ptr<Device> dstDevice,
                                std::shared_ptr<AllocatedBuffer> dst,
                                vk::DeviceSize dstOffset) {
  return srcDevice->submit([srcDevice = std::move(srcDevice), src = std::move(src),
                            dstDevice = std::move(dstDevice), dst = std::move(dst),
                            dstOffset]() {
    transfer(*srcDevice, *src, *dstDevice, *dst, dstOffset);
  });
}

std::future<void> transferAsync(std::shared_ptr<Device> srcDevice,
                                std::shared_ptr<AllocatedImage> src,
                                std::shared_ptr<Device> dstDevice,
                                std::shared_ptr<AllocatedImage> dst,
                                vk::ImageLayout dstFinalLayout) {
  return srcDevice->submit([srcDevice = std::move(srcDevice), src = std::move(src),
                            dstDevice = std::move(dstDevice), dst = std::move(dst),
                            dstFinalLayout]() {
    transfer(*srcDevice, *src, *dstDevice, *dst, dstFinalLayout);
  });
}

} // namespace graphics::vulkan::devices
