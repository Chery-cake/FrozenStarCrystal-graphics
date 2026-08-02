module;

module graphics.vulkan.media;

import std.compat;
import vulkan;

import graphics.vulkan.devices;

namespace graphics::vulkan::media {

std::future<std::shared_ptr<Resource>>
Registry::load(const Tag *tag, std::span<devices::Device *> targetDevices,
               ImageArrayRegistry &bindless) {
  if (tag == nullptr || targetDevices.empty()) {
    std::promise<std::shared_ptr<Resource>> p;
    p.set_value(nullptr);
    return p.get_future();
  }

  // Reuse an existing live resource if already loaded.
  if (auto existing = get(tag)) {
    std::promise<std::shared_ptr<Resource>> p;
    p.set_value(existing);
    return p.get_future();
  }

  // Offload the GPU upload to the first device's thread pool.
  return targetDevices[0]->submit(
      [this, tag, targetDevices, &bindless]() -> std::shared_ptr<Resource> {
        auto resource = std::make_shared<Resource>();
        resource->kind = tag->kind;

        // TODO: Load pixel data from tag->sourcePath using a file-loader
        // (e.g. stb_image for eImage2D/eImageAtlas, OpenEXR for eImageHDR,
        // a video decoder for eVideoFrame/eVideoStream).
        // For each device, create an AllocatedImage, upload via
        // devices::transfer(), and register in bindless.

        // Register the resource under this tag.
        add(tag, resource);
        return resource;
      });
}

std::future<std::shared_ptr<Resource>>
Registry::loadCrossDevice(const Tag *tag, devices::Device &computeDevice,
                          devices::Device &displayDevice,
                          ImageArrayRegistry &bindless) {
  if (tag == nullptr) {
    std::promise<std::shared_ptr<Resource>> p;
    p.set_value(nullptr);
    return p.get_future();
  }

  if (auto existing = get(tag)) {
    std::promise<std::shared_ptr<Resource>> p;
    p.set_value(existing);
    return p.get_future();
  }

  return computeDevice.submit([this, tag, &computeDevice, &displayDevice,
                               &bindless]() -> std::shared_ptr<Resource> {
    auto resource = std::make_shared<Resource>();
    resource->kind = tag->kind;

    // TODO: Load on computeDevice, then use
    // devices::transferAsync(computeDevice, src, displayDevice, dst)
    // to mirror the resource to displayDevice, and commit bindless
    // handles for both.

    add(tag, resource);
    return resource;
  });
}

void Registry::advanceFrame(const Tag *tag, devices::Device &device) {
  if (tag == nullptr) {
    return;
  }

  auto resource = get(tag);
  if (resource == nullptr) {
    return;
  }

  if (resource->totalFrames <= 1) {
    return;
  }

  resource->currentFrame = (resource->currentFrame + 1) % resource->totalFrames;

  // TODO: Upload the newly decoded frame at resource->currentFrame to the
  // device using devices::transfer() and update the bindless handle.
  (void)device;
}

} // namespace graphics::vulkan::media
